#include "ControllerText.h"
#include "Feedback.h"
#include "../../Runtime/SettingsWindow.h"
#include <string.h>

/* 原版图标入口有九个栈参数，游戏自己清理36字节；fast call 包装保留ECX中的对象。 */
typedef int (__fastcall *NativeIconDraw)(void *, void *,int,int,int,int,int,int,int,int,int);
static BYTE saved_icon_call[5],saved_label_call[5];
static bool label_installed;
static bool installed,show_action,held_preview;
static void *action_world,*action_animation;
static bool runtime_ended;
static uint32_t action_actor,action_tick;
static int action_selection;

static void *hud(void) { return ReadPtr((void *)g_profile->skill_global,0); }
void Feedback_End(void) { show_action=false;held_preview=false; }
void Feedback_HoldSkill(int selection)
{
    Feedback_Start(selection,ACTION_SKILL);held_preview=show_action;
}
void Feedback_Start(int selection,ActionSource source)
{
    if (source!=ACTION_SKILL && source!=ACTION_THROW && source!=ACTION_ULTIMATE) {
        Feedback_End();return;
    }
    void *role=Game_Player();
    if (!role) return;
    /* 投掷选择码不是技能组，图标应像原版F1-F6一样取角色的投掷组，不能查错表。 */
    if (source==ACTION_THROW) {
        void *group=(void *)(uintptr_t)((This0)g_profile->throw_group)(role, NULL);
        if (!Memory_Readable(group,0x26)) return;
        selection=(int)(Read32(group,0x24)&0xFFFFu);
    }
    action_selection=selection;action_actor=Read32(role,0x14);
    action_world=ReadPtr((void *)g_profile->world_global,0);
    action_animation=NULL;runtime_ended=false;held_preview=false;action_tick=Read32((void *)g_profile->game_tick,0);show_action=true;
    /* 必杀技真正建立动作后清掉原版准备态，结束后自然恢复此前右键选择。 */
    if (source==ACTION_ULTIMATE && (int)Read32(hud(),0xBF4)==selection)
        ((This1)g_profile->prepared_set)(hud(), NULL,-1);
}

void Feedback_RuntimeEnded(void)
{
    if (!show_action) return;
    void *role=Game_Player();
    if (!role || action_actor!=Read32(role,0x14) || action_world!=ReadPtr((void *)g_profile->world_global,0)) {
        Feedback_End();return;
    }
    /* 结束通知发生在活动指针清零之后、原姿态切换之前，可以捕获尚在收尾的动画。 */
    action_animation=(void *)(uintptr_t)((This0)g_profile->animation_get)(role, NULL);
    runtime_ended=true;
}

bool Feedback_Selection(int *selection,int *icon)
{
    void *role=Game_Player(),*ui=hud();
    if (!selection || !icon || !role || !Memory_Readable(ui,0xBFC)) {Feedback_End();return false;}
    /* 先显示原版准备态。准备的有效时间由游戏HUD计数，不新增插件自己的倒计时。 */
    int prepared=(int)Read32(ui,0xBF4);
    if (prepared>=0 && !held_preview) {
        *selection=prepared;*icon=((This1)g_profile->icon_resolve)(ui, NULL,prepared);return true;
    }
    if (!show_action) return false;
    if (action_actor!=Read32(role,0x14) || action_world!=ReadPtr((void *)g_profile->world_global,0)) {
        Feedback_End();return false;
    }
    void *runtime=ReadPtr(role,g_profile->active_offset);
    void *animation=(void *)(uintptr_t)((This0)g_profile->animation_get)(role, NULL);
    if (held_preview) {
        *selection=action_selection;*icon=((This1)g_profile->icon_resolve)(ui, NULL,action_selection);return true;
    }
    if (runtime) {
        /* 同一技能的续段/派生Runtime可能换地址，继续沿用整次快捷动作图标。 */
        action_animation=animation;runtime_ended=false;
    } else {
        /* 活动Runtime已结束也可能还在播放收尾；用原版动画的末帧/时长判定，不加固定秒数。 */
        bool tail=runtime_ended && animation && animation==action_animation &&
            Memory_Readable(animation,0x20) && !((This0)g_profile->animation_finished)(animation, NULL);
        bool creating=Read32((void *)g_profile->game_tick,0)==action_tick;
        if (!tail && !creating) {Feedback_End();return false;}
    }
    *selection=action_selection;*icon=((This1)g_profile->icon_resolve)(ui, NULL,action_selection);return true;
}

static int __fastcall right_icon_hook(void *self,void *unused,
    int context,int icon,int selection,int x,int y,int shade,int alpha,int bindings,int side)
{
    (void)unused;
    /* 原HUD先按长期右键槽计算灰色模式；临时快捷动作可能来自另一技能。
     * 已经通过施放资格的临时动作不能继承空套组的灰色/半透明参数。
     * 仅当前快捷动作/落点预览覆盖这两项，结束后完整恢复原槽的绘制。 */
    bool temporary=Feedback_Selection(&selection,&icon);
    if (temporary && show_action && selection==action_selection) {shade=0;alpha=-1;}
    return ((NativeIconDraw)g_profile->icon_draw)(self, NULL,context,icon,selection,x,y,shade,alpha,bindings,side);
}

void Feedback_Ultimate(unsigned slot)
{
    if (slot>=4) return;
    void *role=Game_Player(),*ui=hud();
    void *choices=ReadPtr(role,0x193);
    if (!role || !Memory_Readable(choices,12) || !Memory_Readable(ui,0xBFC)) return;
    int count=((This1)g_profile->ui_property)(choices, NULL,1);
    if (count<0 || count>1024) return;
    unsigned index=0;
    for (int i=0;i<count;++i) {
        int id=((This1)g_profile->ui_property)(choices, NULL,i+2);
        void *group=(void *)(uintptr_t)((This1)g_profile->lookup)((void *)g_profile->skill_groups, NULL,id);
        /* 原版手势识别用组+0x32的>=1000标志识别必杀，不能把普通技能类别当必杀。 */
        if (!Memory_Readable(group,0x36) || (int)Read32(group,0x32)<1000) continue;
        if (index++!=slot) continue;
        if (((This1)g_profile->skill_eligibility)(role, NULL,id)==-1) {
            Log_Write(ControllerText_Finisher_SlotIneligibleLog,slot+1);return;
        }
        if ((int)Read32(ui,0xBF4)==id) {
            Combat_Request(id,ACTION_ULTIMATE,false);
            Log_Write(ControllerText_Finisher_ReleaseRequestedLog,slot+1,id);
        } else {
            ((This1)g_profile->prepared_set)(ui, NULL,id);
            Log_Write(ControllerText_Finisher_PreparedLog,slot+1,id);
        }
        return;
    }
    Log_Write(ControllerText_Finisher_SlotUnconfiguredLog,slot+1);
}

/* 只包装HUD十二槽的黄字调用，物品图标、数量和字体颜色设置仍由原绘制完成。
 * 原入口为thiscall并清理五个栈参数；fastcall第二参数占EDX，不额外占用游戏栈。 */
typedef int (__fastcall *NativeQuickText)(void *,void *,void *,int,int,const char *,int);
static int __fastcall quick_label_hook(void *self,void *unused,void *surface,int x,int y,const char *label,int flags)
{
    (void)unused;
    if(!RuntimeConfig_GetInt(CONFIG_CONTROLLER_ENABLED) || self!=hud())
        return ((NativeQuickText)g_profile->quick_label_draw)(self,NULL,surface,x,y,label,flags);
    /* 原代码仅在非空快捷格调用：左栏字符串1..6，右栏F1..F6。只读字符串确定栏及槽。
     * 不写原绑定、不改变数量，未知格式保持原调用，避免误吞其它插件的自定义文字。 */
    if(!Memory_Readable(label,3))return ((NativeQuickText)g_profile->quick_label_draw)(self,NULL,surface,x,y,label,flags);
    bool right=label[0]=='F';unsigned offset=right ? 1u:0u;
    if(label[offset]<'1' || label[offset]>'6' || label[offset+1])
        return ((NativeQuickText)g_profile->quick_label_draw)(self,NULL,surface,x,y,label,flags);
    unsigned slot=(unsigned)(label[offset]-'1');
    bool active=g_input.connected && g_input.focused && !SettingsWindow_Active() &&
        (right ? g_intent.layer==LAYER_ITEM && (g_input.buttons&KEY(PAD_RB)):
                 g_intent.layer==LAYER_MEDICINE && (g_input.buttons&KEY(PAD_LB)));
    if(!active)return 0;
    /* 原游戏文字入口使用GBK；箭头用对应双字节编码，避免把UTF-8当原ANSI文字解析。 */
    static const char *const buttons[]={"A","B","X","Y","\xA1\xFB","\xA1\xFA"};
    return ((NativeQuickText)g_profile->quick_label_draw)(self,NULL,surface,x,y,buttons[slot],flags);
}
bool Feedback_Initialize(void)
{
    if (installed) return true;
    uintptr_t target=g_profile->right_icon_call;
    if (!Memory_Readable((void *)target,5)) return false;
    memcpy(saved_icon_call,(void *)target,5);
    int32_t relative;memcpy(&relative,saved_icon_call+1,4);
    if (saved_icon_call[0]!=0xE8 || target+5+relative!=g_profile->icon_draw) return false;
    uintptr_t label_target=g_profile->quick_label_call;
    if(!Memory_Readable((void *)label_target,5))return false;
    memcpy(saved_label_call,(void *)label_target,5);int32_t label_relative;memcpy(&label_relative,saved_label_call+1,4);
    if(saved_label_call[0]!=0xE8 || label_target+5+label_relative!=g_profile->quick_label_draw)return false;
    BYTE replacement[5]={0xE8};relative=(int32_t)((uintptr_t)right_icon_hook-target-5);
    memcpy(replacement+1,&relative,4);
    installed=Memory_Patch((void *)target,replacement,5);if(!installed)return false;
    BYTE label_bytes[5]={0xE8};label_relative=(int32_t)((uintptr_t)quick_label_hook-label_target-5);
    memcpy(label_bytes+1,&label_relative,4);label_installed=Memory_Patch((void *)label_target,label_bytes,5);
    if(!label_installed){Feedback_Shutdown();return false;}
    return true;
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
    if(label_installed){BYTE bytes[5]={0xE8};relative=(int32_t)((uintptr_t)quick_label_hook-g_profile->quick_label_call-5);
        memcpy(bytes+1,&relative,4);
        if(Memory_Readable((void *)g_profile->quick_label_call,5) && !memcmp((void *)g_profile->quick_label_call,bytes,5))
            Memory_Patch((void *)g_profile->quick_label_call,saved_label_call,5);
    }
    label_installed=false;installed=false;Feedback_End();
}
