#include "ControllerText.h"
#include "Menu.h"
#include "Combat.h"
#include <math.h>
#include <string.h>

/* 使用原动作菜单的节点和绘制。插件只保存候选编号，不在浏览过程中修改左右手槽位。 */
static struct {void *root,*world,*actor;int selector,direction;unsigned side;uint32_t repeat;} selection;
static bool active,installed;
static uintptr_t original_tick,original_hover;
typedef struct {void *object;int selector;double x,y;} ActionNode;

static bool valid_root(void *root)
{
    return g_profile && Memory_Readable(root,0xF0) && Read32(root,0)==g_profile->menu_action_vtable;
}
bool ActionMenu_Active(void) {return active && valid_root(selection.root) && Read32(selection.root,0x64);}
bool ActionMenu_Owns(void *root) {return active && root==selection.root && valid_root(root);}

static unsigned nodes(void *root,ActionNode *list)
{
    /* 原E8链节点不是CJm子按钮。只读本次菜单生成的节点，列表最多128项，下一次切侧重新取。 */
    unsigned count=0;void *node=ReadPtr(root,0xE8);
    for (unsigned n=0;node && n<128;++n) {
        if (!Memory_Readable(node,0x40)) break;
        int w=(int)Read32(node,0x1C),h=(int)Read32(node,0x20);
        int selector=(int)Read32(node,0x28);
        bool populated=selector>-1 || selector<-4 || Combat_ComboAvailable((unsigned)(-1-selector));
        if (w>0 && h>0 && populated) list[count++]=(ActionNode){node,(int)Read32(node,0x28),
            (int)Read32(node,0x14)+w/2.0,(int)Read32(node,0x18)+h/2.0};
        void *next=ReadPtr(node,8);if (next==node) break;node=next;
    }
    return count;
}
static void project(void)
{
    if (!ActionMenu_Active()) return;
    ActionNode list[128];unsigned count=nodes(selection.root,list);void *focused=NULL;
    for (unsigned i=0;i<count;++i) if (list[i].selector==selection.selector) {focused=list[i].object;break;}
    /* 原C0驱动详情与候选反馈。不存在的候选清为NULL，不把编号当指针。 */
    Write32(selection.root,0xC0,(uint32_t)(uintptr_t)focused);
}
static void seed(void)
{
    ActionNode list[128];unsigned count=nodes(selection.root,list);
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    int equipped=(int)Read32(hud,selection.side ? 0x128:0x12C);
    selection.selector=count ? list[0].selector:INT32_MIN;
    for (unsigned i=0;i<count;++i) if (list[i].selector==equipped) selection.selector=equipped;
    selection.direction=0;selection.repeat=g_input.now+350;project();
}
bool ActionMenu_Anchor(POINT *point)
{
    if (!point || !ActionMenu_Active()) return false;
    void *node=ReadPtr(selection.root,0xC0);if (!Memory_Readable(node,0x40)) return false;
    point->x=(int)Read32(node,0x14)+(int)Read32(node,0x1C)-6;
    point->y=(int)Read32(node,0x18)+(int)Read32(node,0x20)-6;return true;
}
bool ActionMenu_FocusFrame(RECT *rectangle)
{
    if (!rectangle || !ActionMenu_Active()) return false;
    project();void *node=ReadPtr(selection.root,0xC0);
    if (!Memory_Readable(node,0x40)) return false;
    int width=(int)Read32(node,0x1C),height=(int)Read32(node,0x20);
    if (width<=0 || height<=0 || width>1024 || height>1024) return false;
    int x=(int)Read32(node,0x14),y=(int)Read32(node,0x18);
    /* 技能图标每边内收1个GUI像素；Runtime补偿动画原点，最终可见边界不出按钮。
     * 只改提示矩形，不改变命中、原图标布局或选择业务。 */
    *rectangle=(RECT){x+1,y+1,x+width-1,y+height-1};return true;
}
void ActionMenu_Suspend(void)
{
    /* 来源切换只取消手柄拥有权；物理鼠标仍能继续操作这个原生菜单。 */
    active=false;memset(&selection,0,sizeof selection);
}
bool ActionMenu_Update(void)
{
    if (!installed) return false;
    if (!g_input.connected || !g_input.focused || g_intent.layer==LAYER_MOUSE ||
        g_intent.layer==LAYER_NATIVE || g_intent.layer==LAYER_NONE) {ActionMenu_Suspend();return false;}
    if (active && (!valid_root(selection.root) || !Read32(selection.root,0x64))) {
        ActionMenu_Suspend();return true;
    }
    if (active && (selection.world!=ReadPtr((void *)g_profile->world_global,0) ||
        selection.actor!=Game_Player() || !Read32(selection.world,0x58))) {
        /* 换图或角色更换后不能提交旧节点。清候选再调用原关闭路径，只释放UI捕获。 */
        Write32(selection.root,0xC0,0);((This0)g_profile->menu_action_commit)(selection.root);
        ActionMenu_Suspend();return true;
    }
    if (active && g_input.menu) {
        /* 另一个真实模态页接管时取消预览，不擅自确认被遮住的候选。 */
        Write32(selection.root,0xC0,0);((This0)g_profile->menu_action_commit)(selection.root);
        ActionMenu_Suspend();return true;
    }
    bool held=g_input.lt && g_input.rt;
    int direction=fabsf(g_intent.rx)<0.55f && fabsf(g_intent.ry)<0.55f ? 0:
        fabsf(g_intent.ry)>=fabsf(g_intent.rx) ? (g_intent.ry<0 ? 1:2):(g_intent.rx<0 ? 3:4);
    if (!active) {
        void *root=ReadPtr((void *)g_profile->menu_action_global,0);if (!valid_root(root)) return false;
        unsigned reason;bool resume_native=g_intent.layer==LAYER_MENU && Menu_Context(&reason)==root;
        if ((g_intent.layer!=LAYER_DUAL && !resume_native) || !held || !direction ||
            (g_input.menu && !resume_native) || !Game_Player()) return false;
        selection.root=root;selection.side=0;active=true;
        selection.world=ReadPtr((void *)g_profile->world_global,0);selection.actor=Game_Player();
        /* 原Open生成当前侧技能/非空连招并捕获UI，第一次拨杆只展开，不额外跳一项。 */
        ((This1)g_profile->menu_action_open)(root,0);seed();selection.direction=direction;
        g_intent.layer=LAYER_ACTION_MENU;
        Log_Write(ControllerText_ActionMenu_OpenedLog);return false;
    }
    g_intent.layer=LAYER_ACTION_MENU;
    if (!held) {
        project();((This0)g_profile->menu_action_commit)(selection.root);
        Log_Write(ControllerText_ActionMenu_SelectionConfirmedLog,selection.side ? ControllerText_LeftSideLabel:ControllerText_RightSideLabel,selection.selector);
        ActionMenu_Suspend();return true;
    }
    if (g_intent.pressed&KEY(PAD_R3)) {
        /* 同一个原菜单换侧只重建候选链，未聚焦侧的浏览不会顺带提交。 */
        selection.side^=1;((This1)g_profile->menu_action_rebuild)(selection.root,(int)selection.side);seed();
        Log_Write(ControllerText_ActionMenu_FocusSideChangedLog,selection.side ? ControllerText_LeftSideLabel:ControllerText_RightSideLabel);return false;
    }
    if (direction && (direction!=selection.direction || (int32_t)(g_input.now-selection.repeat)>=0)) {
        ActionNode list[128];unsigned count=nodes(selection.root,list),at=0;
        for (unsigned i=0;i<count;++i) if (list[i].selector==selection.selector) at=i;
        double best=1e30;unsigned target=at;
        for (unsigned i=0;i<count;++i) if (i!=at) {
            double dx=list[i].x-list[at].x,dy=list[i].y-list[at].y;
            double forward=direction==1 ? -dy:direction==2 ? dy:direction==3 ? -dx:dx;
            double cross=direction<=2 ? fabs(dx):fabs(dy);
            if (forward<=0.5) continue;
            double score=forward+cross*4+cross*cross/(forward+1);
            if (score<best) {best=score;target=i;}
        }
        if (count) selection.selector=list[target].selector;
        selection.repeat=g_input.now+(direction==selection.direction ? 110u:350u);project();
    }
    selection.direction=direction;project();return false;
}
static int __attribute__((fastcall)) action_tick(void *root,void *unused)
{
    (void)unused;
    if (ActionMenu_Owns(root)) {
        /* 原Tick还处理物理热键；手柄拥有时仅执行完整base更新，随后恢复候选，不读鼠标。 */
        int result=((This0)g_profile->menu_message_base_tick)(root);project();return result;
    }
    return ((This0)original_tick)(root);
}
static int __attribute__((fastcall)) action_hover(void *root,void *unused,int event,int x,void *y)
{
    (void)unused;
    if (ActionMenu_Owns(root)) {project();return 0;}
    return ((This3)original_hover)(root,event,x,y);
}
bool ActionMenu_Initialize(void)
{
    if (installed) return true;
    uintptr_t table=g_profile->menu_action_vtable;
    if (!Memory_Readable((void *)table,0x34) || Read32((void *)table,4)!=g_profile->menu_action_tick ||
        Read32((void *)table,0x30)!=g_profile->menu_action_hover) return false;
    /* 两个菜单包装属于Controller既有菜单入口组，与普通菜单使用同一所有者。 */
    if (!HookManager_Claim(SHARED_HOOK_CONTROLLER_MENU,RUNTIME_MODULE_CONTROLLER)) return false;
    original_tick=g_profile->menu_action_tick;original_hover=g_profile->menu_action_hover;
    uintptr_t tick=(uintptr_t)action_tick,hover=(uintptr_t)action_hover;
    if (!Memory_Patch((void *)(table+4),&tick,4)) return false;
    if (!Memory_Patch((void *)(table+0x30),&hover,4)) {
        Memory_Patch((void *)(table+4),&original_tick,4);return false;
    }
    installed=true;return true;
}
void ActionMenu_Shutdown(void)
{
    ActionMenu_Suspend();if (!installed || !g_profile) return;
    uintptr_t table=g_profile->menu_action_vtable;
    if (Read32((void *)table,4)==(uintptr_t)action_tick) Memory_Patch((void *)(table+4),&original_tick,4);
    if (Read32((void *)table,0x30)==(uintptr_t)action_hover) Memory_Patch((void *)(table+0x30),&original_hover,4);
    installed=false;
}
