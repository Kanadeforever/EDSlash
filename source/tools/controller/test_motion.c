#include "Control.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* 连续角度测试弥补旧测试只覆盖八个方向的缺口。
   这里只检验输入和目标几何，不把数值误差上限说成真实游戏的行走轨迹。 */
static void require(int pass, const char *reason)
{
    if (!pass) { fprintf(stderr,"连续方向检查失败：%s\n",reason);exit(1); }
}

int main(void)
{
    const float pi=3.14159265358979323846f;
    float x,y;
    Control_Stick(20000,2000,8000,&x,&y);
    require(x>0 && y>0 && fabsf(y/x-0.1f)<0.00001f,"靠近水平轴的斜向不能被吸到水平");
    Control_Stick(6000,6000,8000,&x,&y);
    require(x>0 && y>0,"圆形死区外的对角输入不能被逐轴死区吞掉");
    Control_Stick(4000,4000,8000,&x,&y);require(x==0 && y==0,"死区内应当停止");
    Control_Stick(-32768,32767,8000,&x,&y);
    require(sqrtf(x*x+y*y)<=1.00001f,"对角满推的力度必须限幅");
    float max_angle=0,max_cross=0;
    unsigned samples=0;
    const int offsets[][2]={{0,0},{13,38},{32,32},{58,6}};
    for (int power=0;power<3;++power) {
        float magnitude=power==0 ? 9000.0f:power==1 ? 18000.0f:32767.0f;
        for (int angle=0;angle<720;++angle) {
            float rad=angle*0.5f*pi/180;
            int raw_x=(int)lroundf(cosf(rad)*magnitude),raw_y=(int)lroundf(sinf(rad)*magnitude);
            Control_Stick(raw_x,raw_y,8000,&x,&y);
            float cross=fabsf(x*raw_y-y*raw_x)/magnitude;
            if (cross>max_cross)max_cross=cross;
            require(cross<0.00001f,"圆形死区必须保持原始输入角度");
            for (unsigned position=0;position<4;++position) {
                int wx=6400+offsets[position][0],wy=6400+offsets[position][1],mx,my;
                Control_MoveGoal(wx,wy,x,y,12,&mx,&my);
                float dx=mx-wx/64.0f,dy=my-wy/64.0f;
                float screen_x=(dx-dy)/2,screen_y=(dx+dy)/4;
                float delta=fabsf(atan2f(screen_y,screen_x)-atan2f((float)raw_y,(float)raw_x));
                if(delta>pi)delta=2*pi-delta;
                float degrees=delta*180/pi;
                if(degrees>max_angle)max_angle=degrees;
                require(degrees<7.5f,"默认 12 格前探的最大角度误差超出界限");
                ++samples;
            }
        }
    }
    printf("连续方向检查通过：%u 个角度/力度/位置组合，最大目标角误差 %.3f 度，死区方向叉积 %.8f。\n",samples,max_angle,max_cross);
    return 0;
}
