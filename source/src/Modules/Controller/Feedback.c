#include "Feedback.h"
#include <string.h>

/* 原版图标入口有九个栈参数，游戏自己清理36字节；fast call 包装保留ECX中的对象。 */
typedef int (__attribute__((thiscall)) *NativeIconDraw)(void *,int,int,int,int,int,int,int,int,int);
static BYTE saved_icon_call[5];
static bool installed,show_action;
static void *action_world,*action_runtime;
static uint32_t action_actor,action_tick;
static int action_selection;

static void *hud(void) { return ReadPtr((void *)g_profile->skill_global,0); }
void Feedback_End(void) { show_action=false; }
void Feedback_Start(int selection,ActionSource source)
{
    if (source!=ACTION_SKILL && source!=ACTION_THROW && source!=ACTION_ULTIMATE) {
        Feedback_End();return;
    }
    void *role=Game_Player();
    if (!role) return;
    /* 投掷选择码不是技能组，图标应像原版F1-F6一样取角色的投掷组，不能查错表。 */
    if (source==ACTION_THROW) {
        void *group=(void *)(uintptr_t)((This0)g_profile->throw_group)(role);
        if (!Memory_Readable(group,0x26)) return;
        selection=(int)(Read32(group,0x24)&0xFFFFu);
    }
    action_selection=selection;action_actor=Read32(role,0x14);
    action_world=ReadPtr((void *)g_profile->world_global,0);
    action_runtime=NULL;action_tick=Read32((void *)g_profile->game_tick,0);show_action=true;
    /* 必杀技真正建立动作后清掉原版准备态，结束后自然恢复此前右键选择。 */
    if (source==ACTION_ULTIMATE && (int)Read32(hud(),0xBF4)==selection)
        ((This1)g_profile->prepared_set)(hud(),-1);
}

bool Feedback_Selection(int *selection,int *icon)
{
    void *role=Game_Player(),*ui=hud();
    if (!selection || !icon || !role || !Memory_Readable(ui,0xBFC)) {Feedback_End();return false;}
    /* 先显示原版准备态。准备的有效时间由游戏HUD计数，不新增插件自己的倒计时。 */
    int prepared=(int)Read32(ui,0xBF4);
    if (prepared>=0) {
        *selection=prepared;*icon=((This1)g_profile->icon_resolve)(ui,prepared);return true;
    }
    if (!show_action) return false;
    if (action_actor!=Read32(role,0x14) || action_world!=ReadPtr((void *)g_profile->world_global,0)) {
        Feedback_End();return false;
    }
    /* 成功通知发生时Runtime指针可能尚未写入；只允许建立当帧暂为空，后续空值即结束。 */
    if (!ReadPtr(role,g_profile->active_offset) && Read32((void *)g_profile->game_tick,0)!=action_tick) {
        Feedback_End();return false;
    }
    void *runtime=ReadPtr(role,g_profile->active_offset);
    if (action_runtime && runtime!=action_runtime) {Feedback_End();return false;}
    if (runtime) action_runtime=runtime;
    *selection=action_selection;*icon=((This1)g_profile->icon_resolve)(ui,action_selection);return true;
}

static int __attribute__((fastcall)) right_icon_hook(void *self,void *unused,
    int context,int icon,int selection,int x,int y,int shade,int alpha,int bindings,int side)
{
    (void)unused;
    /* 只改图标及选择码；坐标、透明度、明暗和快捷提示全部保留原版绘制参数。 */
    Feedback_Selection(&selection,&icon);
    return ((NativeIconDraw)g_profile->icon_draw)(self,context,icon,selection,x,y,shade,alpha,bindings,side);
}

void Feedback_Ultimate(unsigned slot)
{
    if (slot>=4) return;
    void *role=Game_Player(),*ui=hud();
    void *choices=ReadPtr(role,0x193);
    if (!role || !Memory_Readable(choices,12) || !Memory_Readable(ui,0xBFC)) return;
    int count=((This1)g_profile->ui_property)(choices,1);
    if (count<0 || count>1024) return;
    unsigned index=0;
    for (int i=0;i<count;++i) {
        int id=((This1)g_profile->ui_property)(choices,i+2);
        void *group=(void *)(uintptr_t)((This1)g_profile->lookup)((void *)g_profile->skill_groups,id);
        /* 原版手势识别用组+0x32的>=1000标志识别必杀，不能把普通技能类别当必杀。 */
        if (!Memory_Readable(group,0x36) || (int)Read32(group,0x32)<1000) continue;
        if (index++!=slot) continue;
        if (((This1)g_profile->skill_eligibility)(role,id)==-1) {
            Log_Write("[必杀技] 槽%u目前不符合原版资格。",slot+1);return;
        }
        if ((int)Read32(ui,0xBF4)==id) {
            Combat_Request(id,ACTION_ULTIMATE,false);
            Log_Write("[必杀技] 槽%u第二次确认，请求释放组=%d。",slot+1,id);
        } else {
            ((This1)g_profile->prepared_set)(ui,id);
            Log_Write("[必杀技] 槽%u准备组=%d，沿用原版有效窗口。",slot+1,id);
        }
        return;
    }
    Log_Write("[必杀技] 角色未配置第%u个必杀组。",slot+1);
}

bool Feedback_Initialize(void)
{
    if (installed) return true;
    uintptr_t target=g_profile->right_icon_call;
    if (!Memory_Readable((void *)target,5)) return false;
    memcpy(saved_icon_call,(void *)target,5);
    int32_t relative;memcpy(&relative,saved_icon_call+1,4);
    if (saved_icon_call[0]!=0xE8 || target+5+relative!=g_profile->icon_draw) return false;
    BYTE replacement[5]={0xE8};relative=(int32_t)((uintptr_t)right_icon_hook-target-5);
    memcpy(replacement+1,&relative,4);
    installed=Memory_Patch((void *)target,replacement,5);return installed;
}
void Feedback_Shutdown(void)
{
    if (!installed) return;
    BYTE replacement[5]={0xE8};int32_t relative=(int32_t)((uintptr_t)right_icon_hook-g_profile->right_icon_call-5);
    memcpy(replacement+1,&relative,4);
    /* 只撤销仍由自己占有的CALL，避免覆盖其它插件后来安装的入口。 */
    if (Memory_Readable((void *)g_profile->right_icon_call,5) &&
        !memcmp((void *)g_profile->right_icon_call,replacement,5))
        Memory_Patch((void *)g_profile->right_icon_call,saved_icon_call,5);
    installed=false;Feedback_End();
}
