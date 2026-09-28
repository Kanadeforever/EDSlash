#include "Control.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 用输入时间线测试规则，而不是只检查代码里有没有某个字符串。 */
static PadInput in;
static ControlState state;
static unsigned checks;
#define CHECK(expression) do { ++checks; if (!(expression)) { fprintf(stderr,"检查失败，行 %d：%s\n",__LINE__,#expression); return 1; } } while(0)

static Intent step(uint32_t buttons)
{
    in.buttons=buttons; in.now+=16;
    return Control_Step(&state,&in);
}

static void reset(void)
{
    memset(&state,0,sizeof state); memset(&in,0,sizeof in);
    in.connected=in.focused=true;
    step(0); step(0);
}

int main(void)
{
    reset();
    Intent out=step(KEY(PAD_A));
    CHECK(out.layer==LAYER_GAME && out.pressed==KEY(PAD_A));
    CHECK(step(KEY(PAD_A)).pressed==0);
    step(0); out=step(KEY(PAD_X));
    CHECK(out.pressed==KEY(PAD_X));
    CHECK(!(out.held & KEY(PAD_A)));

    /* RT 层释放时，仍按住的 X 必须等到再次松开按下，才可以成为基础攻击。 */
    reset(); in.rt=true; out=step(KEY(PAD_X));
    CHECK(out.layer==LAYER_SKILL && (out.pressed&KEY(PAD_X)));
    in.rt=false; out=step(KEY(PAD_X));
    CHECK(out.layer==LAYER_GAME && !(out.held&KEY(PAD_X)));
    CHECK(!(step(KEY(PAD_X)).held&KEY(PAD_X)));
    step(0); CHECK(step(KEY(PAD_X)).pressed&KEY(PAD_X));

    /* 跑步保持经过药品层；停止以后下一次推动恢复走路。 */
    reset(); in.lx=1;
    CHECK(!step(0).run);
    CHECK(step(KEY(PAD_L3)).run);
    CHECK(step(0).run);
    CHECK(step(KEY(PAD_LB)).run);
    CHECK(step(0).run);
    in.lx=0; CHECK(!step(0).run);
    in.lx=1; CHECK(!step(0).run);
    in.rt=true; CHECK(!step(KEY(PAD_L3)).run);

    /* 同帧组合键和先 START 后 BACK 都只能切换一次，不能附带菜单操作。 */
    reset(); out=step(KEY(PAD_START)|KEY(PAD_BACK));
    CHECK(out.mode_changed && out.rumble_ms==200 && !out.menu_toggle);
    CHECK(!step(KEY(PAD_START)|KEY(PAD_BACK)).mode_changed);
    step(0); step(0);
    CHECK(step(0).layer==LAYER_MOUSE);
    out=step(KEY(PAD_START)|KEY(PAD_BACK));
    CHECK(out.mode_changed && out.rumble_ms==1500);
    reset(); out=step(KEY(PAD_START));
    CHECK(!out.menu_toggle);
    out=step(KEY(PAD_START)|KEY(PAD_BACK));
    CHECK(out.mode_changed && !out.menu_toggle);
    in.now+=300; CHECK(!step(0).menu_toggle);
    reset(); step(KEY(PAD_START)); step(0); in.now+=130;
    CHECK(step(0).menu_toggle);
    CHECK(!step(0).menu_toggle);

    /* 拔线、窗口失焦、菜单切换都不能把旧按键当作一次新确认或新攻击。 */
    reset(); step(KEY(PAD_X)); in.connected=false;
    CHECK(step(KEY(PAD_X)).reset);
    in.connected=true; CHECK(step(KEY(PAD_X)).layer==LAYER_NONE);
    step(0); CHECK(step(KEY(PAD_X)).pressed&KEY(PAD_X));
    in.focused=false; CHECK(step(0).reset);
    in.focused=true; in.lt=true; CHECK(step(0).layer==LAYER_NONE);
    in.lt=false; step(0); CHECK(step(0).layer==LAYER_GAME);
    step(KEY(PAD_A)); in.menu=true;
    CHECK(!(step(KEY(PAD_A)).pressed&KEY(PAD_A)));
    step(0); CHECK(step(KEY(PAD_A)).pressed&KEY(PAD_A));

    /* 检查等距投影的八个屏幕方向，反向投影应仍落在原来的屏幕方向上。 */
    const int vectors[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    for (unsigned i=0;i<8;++i) {
        float x,y;
        Control_WorldDirection((float)vectors[i][0],(float)vectors[i][1],&x,&y);
        float sx=(x-y)/2,sy=(x+y)/4;
        CHECK(fabsf(sx*vectors[i][1]-sy*vectors[i][0])<0.0001f);
        CHECK(sx*vectors[i][0]+sy*vectors[i][1]>0);
        int mx,my;
        Control_MoveGoal(6400,6400,(float)vectors[i][0],(float)vectors[i][1],12,&mx,&my);
        CHECK(mx>=88 && mx<=112 && my>=88 && my<=112);
        CHECK(mx!=100 || my!=100);
    }
    CHECK(Control_Axis(7999,8000)==0);
    CHECK(Control_Axis(8000,8000)==0);
    CHECK(Control_Axis(32767,8000)==1);
    CHECK(Control_Axis(-32768,8000)==-1);
    printf("输入时间线与八方向检查通过：%u 项\n",checks);
    return 0;
}
