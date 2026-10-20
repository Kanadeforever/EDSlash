#include "ControllerText.h"
#include "Combat.h"
#include "Feedback.h"
#include "Guard.h"
#include "Target.h"
#include <math.h>
#include <string.h>

static struct {
    uint32_t actor,target;
    void *world;
    ActionHistory history;
    bool owned,right,pending,preferred_right;
    int selection;
    unsigned retries;
    ActionSource source,issued_source;
    bool request_fresh,combo_ready,point_request;
    WorldPoint requested_point;
    unsigned combo_slot,combo_epoch,issued_epoch,issued_slot;
    int request_index,issued_index;
    /* 闪避及续招宽限只保护已有进度，不产生动作或命中连击数。 */
    bool offset_active;
    bool offset_dodging;
    uint32_t offset_grace_ms,offset_grace_started;
    uint32_t offset_tick,offset_timer;
    int offset_selection;
    /* 输入请求和已经提交给原版的动作分开保存：新按 Y 不能把尚未执行的旧 X 标成右手。 */
    bool issued,issued_right;
    int issued_selector,issued_combo,issued_selection;
    uint32_t execution_serial;
    int history_extra;
} combat;

static void *world(void) { return ReadPtr((void *)g_profile->world_global,0); }
static void *manager(void) { return ReadPtr(world(),0x30); }
static uint32_t tick(void) { return Read32((void *)g_profile->game_tick,0); }
static void submit(int opcode,int a,int b,int c) { ((This4)g_profile->submit)(manager(), NULL,opcode,a,b,c); }

/* 原生游标是唯一的预设位置；插件不再保存第二份可能过期的位置。 */
static bool offset_update(void *role)
{
    if (!combat.offset_active) return false;
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    void *mouse=ReadPtr((void *)g_profile->mouse_global,0);
    if (!role || combat.actor!=Read32(role,0x14) || combat.world!=world() ||
        !Combat_OwnsHistory() || RuntimeConfig_GetInt(CONFIG_COMBO_OFFSET)!=1 ||
        (int)Read32(hud,0x12C)!=combat.offset_selection || !Memory_Readable(mouse,0x7C)) {
        combat.offset_active=false;return false;
    }
    bool dodging=Guard_IsDodging(role);
    /* 被击退、死亡或其它状态抢占不视作正常闪避收尾，不把旧进度重新写回。 */
    if (!dodging && Read32(role,0x73)!=1) {combat.offset_active=false;return false;}
    uint32_t now=tick(),delta=now-combat.offset_tick;combat.offset_tick=now;
    if (combat.history.count) {
        combat.history.start_tick+=delta;
        if (combat.history.ended) combat.history.end_tick+=delta;
    }
    /* 原生计时只平移闪避耗时。宽限使用真实毫秒，结束后恢复原游戏的超时判定。 */
    if (combat.offset_dodging && combat.offset_timer) combat.offset_timer+=delta;
    Write32(mouse,0x78,combat.offset_timer);
    if (dodging) return true;
    if (combat.offset_dodging) {
        combat.offset_dodging=false;combat.offset_grace_started=g_input.now;
    }
    if (g_input.now-combat.offset_grace_started<combat.offset_grace_ms)return true;
    combat.offset_active=false;return false;
}

void Combat_DodgeStarted(void *role)
{
    if (!role || !Combat_OwnsHistory() || !Guard_IsDodging(role) ||
        RuntimeConfig_GetInt(CONFIG_COMBO_OFFSET)!=1) return;
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    void *mouse=ReadPtr((void *)g_profile->mouse_global,0);
    int selection=(int)Read32(hud,0x12C);
    if (!Memory_Readable(hud,0x130) || !Memory_Readable(mouse,0x7C) ||
        selection>-1 || selection<-4 || (int)Read32(hud,0x120)<0) return;
    /* 连续闪避先结算上一段，防止重复起算把已经暂停的时间再加一次。 */
    if (combat.offset_active) offset_update(role);
    combat.offset_active=true;combat.offset_tick=tick();
    combat.offset_dodging=true;
    combat.offset_grace_ms=(uint32_t)RuntimeConfig_GetInt(CONFIG_OFFSET_GRACE_MS);
    combat.offset_timer=Read32(mouse,0x78);combat.offset_selection=selection;
}

int __fastcall Combat_ComboResetHook(void *hud,void *unused,int selection)
{
    (void)unused;
    /* 原分支紧接着写HUD+120=-1。在玩家闪避及续招宽限内拒绝这次归零，
     * 其余输入来源、原版档和其它查询都沿原函数返回值。 */
    if (hud==ReadPtr((void *)g_profile->skill_global,0) && selection==-1 &&
        offset_update(Game_Player())) return 0;
    int result=((This1)g_profile->combo_reset_query)(hud,NULL,selection);
    if (result && Combat_OwnsHistory()) {
        ++combat.combo_epoch;
        if (combat.source==ACTION_COMBO) combat.pending=false;
        if (combat.issued_source==ACTION_COMBO) combat.issued=false;
    }
    return result;
}

void Combat_Reset(void) { Target_Reset();memset(&combat,0,sizeof combat); }
void Combat_Suspend(void) { combat.pending=false;combat.request_fresh=false;combat.offset_active=false;Target_Suspend(); }
uint32_t Combat_Target(void) { return Target_Rebuilt() ? Target_Handle():combat.target; }
void Combat_Request(int selection,ActionSource source,bool left_style)
{
    if (selection<0) return;
    void *role=Game_Player();
    if (!role) return;
    if (combat.actor!=Read32(role,0x14) || combat.world!=world()) {
        Combat_Reset();combat.actor=Read32(role,0x14);combat.world=world();combat.target=Target_Rebuilt() ? 0:Read32(role,0x143);
    }
    combat.point_request=false;combat.selection=selection;combat.source=source;combat.right=!left_style;
    combat.pending=true;combat.request_fresh=true;combat.retries=5;
}

void Combat_RequestPoint(int selection,const WorldPoint *point)
{
    if (!point || selection<0) return;
    /* 可破坏静态对象沿原基础技能解析，目标是独立世界点，不伪造鼠标或怪物身份。 */
    Combat_Request(selection,ACTION_LEFT,true);combat.point_request=true;combat.requested_point=*point;
}
void Combat_RequestSkillPoint(int selection,bool left_style,const WorldPoint *point)
{
    if(!point || selection<0)return;
    /* 原快捷请求负责来源、缓冲及角色身份，本接口只增加目标点。 */
    Combat_Request(selection,ACTION_SKILL,left_style);combat.point_request=true;combat.requested_point=*point;
}
static bool resolve_request(void *role,int selection,const WorldPoint *point,const ActionHistory *history,
                            bool restart,ResolvedSkill *out)
{
    if(Skill_Resolve(role,selection,point,history,out))return true;
    if(!restart)return false;
    /* 快捷施放允许按自己的起手解析；预览和正式请求必须共享这项规则。 */
    ActionHistory first={0};return Skill_Resolve(role,selection,point,&first,out);
}
static bool first_usable(void *role,const ResolvedSkill *resolved,void **record)
{
    *record=NULL;
    if(resolved->sequence || resolved->method>=10000)return true;
    *record=(void *)(uintptr_t)((This1)g_profile->role_method)(role, NULL,resolved->method);
    return *record && ((This1)g_profile->method_usable)(role, NULL,(int)(uintptr_t)*record);
}
bool Combat_PreviewSkill(void *role,int selection,const WorldPoint *point,ResolvedSkill *out)
{
    if(!role || !point || !out || selection<0)return false;
    ActionHistory history={0};
    if(combat.actor==Read32(role,0x14) && combat.world==world())history=combat.history;
    /* 预览不修改历史；已结束并过期的历史和正式Update一样不参与解析。 */
    uint32_t timeout=Read32((void *)g_profile->combo_timeout,0);
    if(history.count && history.ended && tick()-history.end_tick>timeout)memset(&history,0,sizeof history);
    void *record;
    return resolve_request(role,selection,point,&history,true,out) && first_usable(role,out,&record);
}
bool Combat_ComboAvailable(unsigned index)
{
    if (index>=4 || !g_profile || !g_profile->combo_get) return false;
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    void *sequence=Memory_Readable(hud,0x130) ? (void *)(uintptr_t)((This1)g_profile->combo_get)(hud, NULL,(int)index):NULL;
    unsigned count=Read32(sequence,0);
    return Memory_Readable(sequence,12) && count && count<=64 && Memory_Readable(ReadPtr(sequence,4),12);
}
void Combat_SelectCombo(unsigned index)
{
    /* 空组不改变已装备动作，也不提前改原生连招游标。 */
    if (!Combat_ComboAvailable(index)) return;
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    combat.combo_slot=index;combat.combo_ready=true;combat.offset_active=false;++combat.combo_epoch;
    if (combat.pending && combat.source==ACTION_COMBO) combat.pending=false;
    if (Memory_Readable(hud,0x130)) {
        ((This1)g_profile->right_set)(hud, NULL,-1-(int)index);Write32(hud,0x120,0xFFFFFFFFu);
    }
    Log_Write(ControllerText_Combat_ComboGroupChangedLog,index+1);
}

static int combo_selection(void *hud)
{
    int selected=(int)Read32(hud,0x12C);
    if (!combat.combo_ready || (selected<=-1 && selected>=-4 && combat.combo_slot!=(unsigned)(-1-selected))) {
        combat.combo_slot=selected<=-1 && selected>=-4 ? (unsigned)(-1-selected):0;
        combat.combo_ready=true;
    }
    if (!g_profile->combo_get) return -1;
    void *list=(void *)(uintptr_t)((This1)g_profile->combo_get)(hud, NULL,(int)combat.combo_slot);
    unsigned count=Read32(list,0);
    if (!Memory_Readable(list,12) || !count || count>64) return -1;
    int cursor=(int)Read32(hud,0x120);
    unsigned index=cursor<0 ? 0:((unsigned)cursor+1)%count;
    void *node=ReadPtr(list,4);
    for (unsigned i=0;i<index && Memory_Readable(node,12);++i) node=ReadPtr(node,0);
    if (!Memory_Readable(node,12)) return -1;
    combat.request_index=(int)index;
    return (int)Read32(node,8);
}
bool Combat_AllowsMouseRetry(void) { return !g_input.connected || g_intent.layer==LAYER_MOUSE || g_intent.layer==LAYER_NATIVE; }

bool Combat_OwnsHistory(void)
{
    /* 原版调用点已经限定当前受控玩家。此处再验证来源和场景，鼠标模式仍执行旧业务。 */
    return g_profile && combat.owned && combat.actor && combat.world==world() &&
        combat.actor==Read32(manager(),0x0C) && g_input.connected && g_input.focused &&
        g_intent.layer!=LAYER_MOUSE && g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_NONE;
}

void Combat_Record(int selector,int direction)
{
    if (!Combat_OwnsHistory()) return;
    if (!combat.issued || (combat.issued_selector>=0 && combat.issued_selector!=selector)) {
        Log_Write(ControllerText_Combat_UnmatchedNotificationLog,selector);
        return;
    }
    combat.offset_active=false;combat.issued=false;++combat.execution_serial;
    Feedback_Start(combat.issued_selection,combat.issued_source);
    /* 只由原生 Runtime 成功建立通知追加。原版自动续段关闭历史门时不会进入此调用点。 */
    if (combat.history.count==64) {
        memmove(combat.history.selectors,combat.history.selectors+1,63*sizeof(int));
        --combat.history.count;
    }
    combat.history.selectors[combat.history.count++]=selector;
    combat.history.start_tick=tick();combat.history.ended=false;combat.history.direction=direction;
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    int selected=(int)Read32(hud,0x12C);
    if (combat.issued_source==ACTION_COMBO && combat.issued_epoch==combat.combo_epoch &&
        selected==-1-(int)combat.issued_slot && Memory_Readable(hud,0xC20)) {
        /* 以保存的左右手来源推进原生自定义连招游标，不读取任何鼠标按钮全局标志。 */
        Write32(hud,0x120,(uint32_t)combat.issued_index);
    }
    Log_Write(ControllerText_Combat_NativeActionCreatedLog,
        selector,direction,combat.issued_right ? ControllerText_RightHandLabel:ControllerText_LeftHandLabel,combat.history.count);
}

void Combat_End(void)
{
    Feedback_RuntimeEnded();
    if (!Combat_OwnsHistory() || !combat.history.count) return;
    /* 原生结束回调可能和下一段创建发生在同一更新周期，不能只靠下一帧看空指针判断结束。 */
    combat.history.ended=true;combat.history.end_tick=tick();
}

void Combat_RecordExtra(int selector,int direction,int extra)
{
    uint32_t before=combat.execution_serial;
    Combat_Record(selector,direction);
    /* 不匹配通知不能偷偷改掉最后成功动作的附加语义，交接时同样要保持来源隔离。 */
    if (combat.execution_serial!=before) combat.history_extra=extra;
}

void Combat_ExportHistory(void)
{
    void *role=Game_Player(), *mouse=ReadPtr((void *)g_profile->mouse_global,0);
    if (!role || combat.actor!=Read32(role,0x14) || combat.world!=world() ||
        !Memory_Readable(mouse,0x68) || !g_profile->history_clear) return;
    /* 只交接成功动作历史，不伪造按钮、鼠标坐标或目标。使用原生容器的释放/追加接口，
     * 不能把插件数组地址塞进原版链表，更不能用两者不同的内存布局互相冒充。 */
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    uint32_t cursor=Read32(hud,0x120);
    ((This0)g_profile->history_clear)(mouse, NULL);
    for (unsigned i=0;i<combat.history.count;++i)
        ((This3)g_profile->history_record)(mouse, NULL,combat.history.selectors[i],combat.history.direction,
                                          (void *)(intptr_t)combat.history_extra);
    if (combat.history.count) {
        Write32(mouse,0x4C,combat.history.start_tick);
        Write32(mouse,0x50,combat.history.ended ? combat.history.end_tick:0xFFFFFFFFu);
    }
    /* 原记录函数可能看到真实右键并推进共享套组游标，交接不是新动作，恢复原值。 */
    if (Memory_Readable(hud,0x124)) Write32(hud,0x120,cursor);
    /* 旧鼠标阶段尚未完成的重试不能在交接后重新冒出来；成功历史与重试请求分开处理。 */
    if (Memory_Readable(mouse,0x78)) Write32(mouse,0x74,0);
    combat.pending=false;combat.owned=false;
    Log_Write(ControllerText_Combat_HandoffToMouseLog,combat.history.count);
}

void Combat_ImportHistory(void)
{
    void *role=Game_Player(), *mouse=ReadPtr((void *)g_profile->mouse_global,0);
    Combat_Reset();
    /* 键盘采样在原鼠标解析之前，切图首帧可能还是旧玩家缓存，不能导入旧角色历史。 */
    if (!role || !Memory_Readable(mouse,0x68) || ReadPtr(mouse,0x38)!=role) return;
    combat.actor=Read32(role,0x14);combat.world=world();combat.target=Target_Rebuilt() ? 0:Read32(role,0x143);
    uint32_t count=Read32(mouse,0x5C);
    void *node=ReadPtr(mouse,0x60);
    /* 原链表节点保存 next/previous/selector；限制数量并逐节点检查，拒绝损坏或循环链。 */
    if (count>64) {Log_Write(ControllerText_Combat_MouseHistoryOverflowLog);return;}
    for (unsigned i=0;i<count;++i) {
        if (!Memory_Readable(node,12)) {memset(&combat.history,0,sizeof combat.history);return;}
        combat.history.selectors[combat.history.count++]=(int)Read32(node,8);
        node=ReadPtr(node,0);
    }
    if (node) {memset(&combat.history,0,sizeof combat.history);return;}
    combat.history.start_tick=Read32(mouse,0x4C);combat.history.end_tick=Read32(mouse,0x50);
    combat.history.ended=combat.history.end_tick!=0xFFFFFFFFu;
    combat.history.direction=(int)Read32(mouse,0x54);combat.history_extra=(int)Read32(mouse,0x58);
    combat.owned=true;
    Log_Write(ControllerText_Combat_HandoffToControllerLog,combat.history.count);
}

void Combat_RecordRuntime(int selector,int direction,int extra,void *runtime)
{
    uint32_t before=combat.execution_serial;
    Combat_RecordExtra(selector,direction,extra);
    if (before!=combat.execution_serial) Target_RecordRuntime(runtime);
}

static WorldPoint aim_point(void *role)
{
    if (combat.point_request) return combat.requested_point;
    if (Target_Rebuilt()) return Target_Point(role,false);
    void *target=Game_Resolve(combat.target);
    if (Game_Enemy(role,target)) {
        WorldPoint point={(int)Read32(target,0x2C),(int)Read32(target,0x30)};
        return point;
    }
    /* 原版档采用原生输入维护得出的最终鼠标世界点，不再另选最近敌人。 */
    void *mouse=ReadPtr((void *)g_profile->mouse_global,0);
    return (WorldPoint){(int)Read32(mouse,0x2C),(int)Read32(mouse,0x30)};
}

void Combat_Update(void *role)
{
    uint32_t actor=Read32(role,0x14),now=tick();
    if (actor!=combat.actor || combat.world!=world()) {
        Combat_Reset();combat.actor=actor;combat.world=world();
        /* 原版档可读取既有战斗目标；重编档从空目标开始，由真实命中建立。 */
        combat.target=Target_Rebuilt() ? 0:Read32(role,0x143);
    }
    offset_update(role);
    bool active=Read32(role,g_profile->active_offset)!=0;
    if (combat.history.count && !active && !combat.history.ended) {
        combat.history.ended=true;combat.history.end_tick=now;
    }
    uint32_t timeout=Read32((void *)g_profile->combo_timeout,0);
    if (combat.history.count && combat.history.ended && now-combat.history.end_tick>timeout)
        memset(&combat.history,0,sizeof combat.history);

    Target_Update(role);
    bool held=g_intent.layer==LAYER_GAME && (g_intent.held&(KEY(PAD_X)|KEY(PAD_Y)));
    if (Target_Rebuilt()) combat.target=Target_Handle();
    else {
        /* 原SkillRelease无显式对象时先取MouseManager+3C；方法允许时再回退CombatTarget。 */
        void *mouse=ReadPtr((void *)g_profile->mouse_global,0),*native=ReadPtr(mouse,0x3C);
        combat.target=Game_Enemy(role,native) ? Read32(native,0x14):0;
    }

    /* RT/RB 的请求已经携带明确选择，可直接发动并有限重试；菜单/防御层不能泄漏攻击。 */
    if (g_intent.layer!=LAYER_GAME && g_intent.layer!=LAYER_SKILL && g_intent.layer!=LAYER_ITEM &&
        !((g_intent.layer==LAYER_GUARD || g_intent.layer==LAYER_DUAL) && combat.pending && combat.source==ACTION_ULTIMATE)) {
        combat.pending=false;combat.request_fresh=false;return;
    }
    bool edge=g_intent.layer==LAYER_GAME && (g_intent.pressed&(KEY(PAD_X)|KEY(PAD_Y)))!=0;
    bool fresh=combat.request_fresh;combat.request_fresh=false;
    bool both=(g_intent.held&(KEY(PAD_X)|KEY(PAD_Y)))==(KEY(PAD_X)|KEY(PAD_Y));
    bool right=(g_intent.pressed&KEY(PAD_Y)) ? true:
        (g_intent.pressed&KEY(PAD_X)) ? false:both ? combat.preferred_right:(g_intent.held&KEY(PAD_Y))!=0;
    /* 两键仍都按着时延续最后一次明确边沿的来源，不在下一帧又固定跳回 Y。 */
    if (held) combat.preferred_right=right;
    if (edge || (held && !combat.pending && !active && !fresh)) {
        void *hud=ReadPtr((void *)g_profile->skill_global,0);
        if (!Memory_Readable(hud,0xC20)) return;
        combat.point_request=false;
        int right_slot=(int)Read32(hud,0x12C);
        if (right && right_slot<=-1 && right_slot>=-4) {
            combat.selection=combo_selection(hud);combat.source=ACTION_COMBO;
        } else if (right) {
            /* Y只取长期右手技能；原right_get还会优先返回准备必杀/投掷选择，不能直接照搬。
             * 原动作菜单只列type<2的组。投掷type2仍由RB直接施放，不进入Y规则。 */
            void *group=right_slot>=0 ? (void *)(uintptr_t)((This1)g_profile->lookup)((void *)g_profile->skill_groups, NULL,right_slot):NULL;
            combat.selection=Memory_Readable(group,0x36) && Read32(group,0x32)<2 ? right_slot:-1;
            combat.source=ACTION_RIGHT;
        } else {combat.selection=((This0)g_profile->left_get)(hud, NULL);combat.source=ACTION_LEFT;}
        combat.right=right;combat.pending=combat.selection>=0;combat.retries=5;fresh=true;
        if (edge) Log_Write(ControllerText_Combat_NewPressLog,
            right ? ControllerText_RightHandLabel:ControllerText_LeftHandLabel,combat.selection,active,combat.history.count,combat.history.ended);
    }
    if (!combat.pending) return;
    if (!fresh) {
        if (!combat.retries) {combat.pending=false;return;}
        --combat.retries;
        /* 原版新按下可直接解析时间窗口，只有缓冲重试受忙碌门控制。
           不手工取消活动 Runtime，能否衔接或中断仍由原版动作协议决定。 */
        if (active) {
            if (!combat.retries) {combat.pending=false;Log_Write(ControllerText_Combat_BufferExpiredWhileBusyLog,combat.selection);}
            return;
        }
    }
    WorldPoint point=aim_point(role);
    ResolvedSkill resolved;
    bool parsed=resolve_request(role,combat.selection,&point,&combat.history,
        combat.source==ACTION_SKILL || combat.source==ACTION_THROW || combat.source==ACTION_ULTIMATE,&resolved);
    if (!parsed) {
        if (combat.history.count && combat.history.ended) {
            /* 原解析失败且历史已有结束标记时清历史，再让缓冲按起手解析；
             * 不能让旧序列持续阻止其它快捷技能。 */
            Log_Write(ControllerText_Combat_ClearEndedHistoryAndRetryLog,combat.selection,combat.history.count);
            memset(&combat.history,0,sizeof combat.history);
        }
        if (!combat.retries) {combat.pending=false;Log_Write(ControllerText_Combat_UnresolvedActionDiscardedLog,combat.selection);}
        return;
    }
    if (Target_Rebuilt() && !combat.point_request && Target_IsDirectional(resolved.method)) {
        point=Target_Point(role,true);
        /* 方向门也要使用最终发射方向，不能用黏住目标的坐标通过资格后再偷偷换方向。 */
        if (!resolve_request(role,combat.selection,&point,&combat.history,
            combat.source==ACTION_SKILL || combat.source==ACTION_THROW || combat.source==ACTION_ULTIMATE,&resolved)) return;
    } else if (!Target_Rebuilt() && !combat.target && !combat.point_request && resolved.method<10000) {
        void *method=(void *)(uintptr_t)((This1)g_profile->lookup)((void *)g_profile->methods,NULL,resolved.method);
        if (Memory_Readable(method,0x7A) && (int)Read32(method,0x76)>0 && g_profile->native_target_get) {
            void *native=(void *)(uintptr_t)((This0)g_profile->native_target_get)(role,NULL);
            if (Game_Enemy(role,native)) combat.target=Read32(native,0x14);
        }
    }
    combat.pending=false;
    /* 首动作和原版一致保留执行资格预检，后续 Runtime 仍会执行自己的完整校验。 */
    void *record;
    if(!first_usable(role,&resolved,&record)) {
        /* 和既有快捷请求一样，资格暂时失败时保留有期限的重试，不提前取消在途动作。 */
        combat.pending=record && combat.retries!=0;return;
    }
    WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
    if (!active && Read32(role,0x73)==1)
        ((This2)g_profile->facing_point)(role, NULL,(int)(uintptr_t)&point,(int)(uintptr_t)&origin);
    combat.owned=true;
    bool saved_issued=combat.issued,saved_right=combat.issued_right;
    int saved_selector=combat.issued_selector,saved_combo=combat.issued_combo,saved_selection=combat.issued_selection;
    ActionSource saved_source=combat.issued_source;
    unsigned saved_slot=combat.issued_slot,saved_epoch=combat.issued_epoch;
    int saved_index=combat.issued_index;
    uint32_t before_serial=combat.execution_serial;
    combat.issued=true;combat.issued_right=combat.right;combat.issued_selector=resolved.selector;
    combat.issued_combo=(int)Read32(ReadPtr((void *)g_profile->skill_global,0),0x12C);
    combat.issued_selection=combat.selection;combat.issued_source=combat.source;combat.issued_slot=combat.combo_slot;
    combat.issued_epoch=combat.combo_epoch;combat.issued_index=combat.request_index;
    void *target_role=Game_Resolve(combat.target);
    int opcode,a2,a3;
    if (!Target_Rebuilt() && !combat.point_request && Game_Enemy(role,target_role)) {
        opcode=9;
        if (!combat.right && (!resolved.sequence || (resolved.method<10000 &&
            ((This1)g_profile->method_distance)(role, NULL,resolved.method)<=96))) opcode=11;
        a2=(int)combat.target;a3=0;
    } else {
        opcode=10;a2=point.x;a3=point.y;
    }
    submit(opcode,resolved.method,a2,a3);
    Log_Write(ControllerText_Combat_ActionSubmittedLog,
        combat.right ? ControllerText_RightHandLabel:ControllerText_LeftHandLabel,combat.selection,resolved.method,opcode,a2,a3,combat.execution_serial!=before_serial);
    /* 忙碌时的首次请求若未执行、也未进入原版 PendingAction，则只留在有期限的手柄缓冲。
       恢复之前已提交动作的来源，防止它稍后执行时被误认成这次新按键。 */
    unsigned pending=g_profile->pending_offset;
    bool accepted_pending=Read32(role,pending)==(unsigned)opcode &&
        Read32(role,pending+4)==(unsigned)resolved.method && Read32(role,pending+8)==(uint32_t)a2 &&
        Read32(role,pending+12)==(uint32_t)a3;
    if (accepted_pending) Target_RecordPending(opcode,resolved.method,a2,a3);
    if (active && combat.execution_serial==before_serial && !accepted_pending) {
        combat.issued=saved_issued;combat.issued_right=saved_right;
        combat.issued_selection=saved_selection;combat.issued_selector=saved_selector;combat.issued_combo=saved_combo;
        combat.issued_source=saved_source;combat.issued_slot=saved_slot;
        combat.issued_epoch=saved_epoch;combat.issued_index=saved_index;
        combat.pending=combat.retries!=0;
    }
}
