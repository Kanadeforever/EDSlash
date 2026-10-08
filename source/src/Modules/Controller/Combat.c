#include "Combat.h"
#include "Feedback.h"
#include <math.h>
#include <string.h>

static struct {
    uint32_t actor,target;
    void *world;
    ActionHistory history;
    bool had_stick,owned,right,pending,preferred_right;
    float direction_x,direction_y;
    int selection;
    unsigned retries;
    ActionSource source,issued_source;
    bool request_fresh,combo_ready,point_request;
    WorldPoint requested_point;
    unsigned combo_slot,combo_epoch,issued_epoch,issued_slot;
    int combo_cursor,request_index,issued_index;
    /* 输入请求和已经提交给原版的动作分开保存：新按 Y 不能把尚未执行的旧 X 标成右手。 */
    bool issued,issued_right;
    int issued_selector,issued_combo,issued_selection;
    uint32_t execution_serial;
    int history_extra;
} combat;

static void *world(void) { return ReadPtr((void *)g_profile->world_global,0); }
static void *manager(void) { return ReadPtr(world(),0x30); }
static uint32_t tick(void) { return Read32((void *)g_profile->game_tick,0); }
static void submit(int opcode,int a,int b,int c) { ((This4)g_profile->submit)(manager(),opcode,a,b,c); }

void Combat_Reset(void) { memset(&combat,0,sizeof combat);combat.combo_cursor=-1; }
void Combat_Suspend(void) { combat.pending=false;combat.request_fresh=false; }
uint32_t Combat_Target(void) { return combat.target; }
void Combat_Request(int selection,ActionSource source,bool left_style)
{
    if (selection<0) return;
    void *role=Game_Player();
    if (!role) return;
    if (combat.actor!=Read32(role,0x14) || combat.world!=world()) {
        Combat_Reset();combat.actor=Read32(role,0x14);combat.world=world();combat.target=Read32(role,0x143);
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
    *record=(void *)(uintptr_t)((This1)g_profile->role_method)(role,resolved->method);
    return *record && ((This1)g_profile->method_usable)(role,(int)(uintptr_t)*record);
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
    void *sequence=Memory_Readable(hud,0x130) ? (void *)(uintptr_t)((This1)g_profile->combo_get)(hud,(int)index):NULL;
    unsigned count=Read32(sequence,0);
    return Memory_Readable(sequence,12) && count && count<=64 && Memory_Readable(ReadPtr(sequence,4),12);
}
void Combat_SelectCombo(unsigned index)
{
    /* 空组不改变已装备动作，也不提前改插件连招游标。 */
    if (!Combat_ComboAvailable(index)) return;
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    combat.combo_slot=index;combat.combo_cursor=-1;combat.combo_ready=true;++combat.combo_epoch;
    if (combat.pending && combat.source==ACTION_COMBO) combat.pending=false;
    if (Memory_Readable(hud,0x130)) ((This1)g_profile->right_set)(hud,-1-(int)index);
    Log_Write("[连招] 切换独立套组=%u，下一次 Y 从首项开始。",index+1);
}

static int combo_selection(void *hud)
{
    int selected=(int)Read32(hud,0x12C);
    if (!combat.combo_ready || (selected<=-1 && selected>=-4 && combat.combo_slot!=(unsigned)(-1-selected))) {
        combat.combo_slot=selected<=-1 && selected>=-4 ? (unsigned)(-1-selected):0;
        combat.combo_cursor=selected<=-1 && selected>=-4 ? (int)Read32(hud,0x120):-1;
        combat.combo_ready=true;
    }
    if (!g_profile->combo_get) return -1;
    void *list=(void *)(uintptr_t)((This1)g_profile->combo_get)(hud,(int)combat.combo_slot);
    unsigned count=Read32(list,0);
    if (!Memory_Readable(list,12) || !count || count>64) return -1;
    unsigned index=combat.combo_cursor<0 ? 0:((unsigned)combat.combo_cursor+1)%count;
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
        Log_Write("[战斗通知] 未匹配到本次手柄提交，组=%d；不推进手柄历史或连招游标。",selector);
        return;
    }
    combat.issued=false;++combat.execution_serial;
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
    if (combat.issued_source==ACTION_COMBO && combat.issued_slot==combat.combo_slot &&
        combat.issued_epoch==combat.combo_epoch) {
        combat.combo_cursor=combat.issued_index;
    }
    if (combat.issued_source==ACTION_COMBO && combat.issued_epoch==combat.combo_epoch &&
        selected==-1-(int)combat.issued_slot && Memory_Readable(hud,0xC20)) {
        /* 以保存的左右手来源推进原生自定义连招游标，不读取任何鼠标按钮全局标志。 */
        Write32(hud,0x120,(uint32_t)combat.issued_index);
    }
    Log_Write("[战斗执行] 原生动作已建立，组=%d 方向=%d 来源=%s 历史=%u。",
        selector,direction,combat.issued_right ? "右手":"左手",combat.history.count);
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
    ((This0)g_profile->history_clear)(mouse);
    for (unsigned i=0;i<combat.history.count;++i)
        ((This3)g_profile->history_record)(mouse,combat.history.selectors[i],combat.history.direction,
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
    Log_Write("[输入交接] 手柄到物理鼠标，成功历史=%u；保留套组进度。",combat.history.count);
}

void Combat_ImportHistory(void)
{
    void *role=Game_Player(), *mouse=ReadPtr((void *)g_profile->mouse_global,0);
    Combat_Reset();
    /* 键盘采样在原鼠标解析之前，切图首帧可能还是旧玩家缓存，不能导入旧角色历史。 */
    if (!role || !Memory_Readable(mouse,0x68) || ReadPtr(mouse,0x38)!=role) return;
    combat.actor=Read32(role,0x14);combat.world=world();combat.target=Read32(role,0x143);
    uint32_t count=Read32(mouse,0x5C);
    void *node=ReadPtr(mouse,0x60);
    /* 原链表节点保存 next/previous/selector；限制数量并逐节点检查，拒绝损坏或循环链。 */
    if (count>64) {Log_Write("[输入交接] 鼠标历史超出上限，按新序列接管。");return;}
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
    Log_Write("[输入交接] 物理鼠标到手柄，成功历史=%u；保留当前目标。",combat.history.count);
}

static WorldPoint aim_point(void *role)
{
    if (combat.point_request) return combat.requested_point;
    void *target=Game_Resolve(combat.target);
    if (Game_Enemy(role,target)) {
        WorldPoint point={(int)Read32(target,0x2C),(int)Read32(target,0x30)};
        return point;
    }
    WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)},point=origin;
    float dx=combat.direction_x,dy=combat.direction_y;
    if (!combat.had_stick) {
        /* 无新方向、无目标时读取角色真实 Facing。用原版方向换算做反查，
           不猜不同角色的八向/十六向枚举顺序，也不使用最后一次鼠标位置。 */
        unsigned directions=Read32(role,0x2BF)==16 ? 16:8;
        unsigned facing=Read32(role,0x14B);
        dx=0;dy=1;
        for (unsigned i=0;i<directions;++i) {
            float angle=(float)i*6.28318530718f/directions;
            WorldPoint probe={origin.x+(int)lroundf(cosf(angle)*256),origin.y+(int)lroundf(sinf(angle)*256)};
            int value=((This2)g_profile->facing_direction)(role,(int)(uintptr_t)&probe,(int)(uintptr_t)&origin);
            if ((unsigned)value==facing) { dx=cosf(angle);dy=sinf(angle);break; }
        }
    }
    point.x+=(int)lroundf(dx*128);point.y+=(int)lroundf(dy*128);
    return point;
}

void Combat_Update(void *role,uint32_t candidate)
{
    uint32_t actor=Read32(role,0x14),now=tick();
    if (actor!=combat.actor || combat.world!=world()) {
        Combat_Reset();combat.actor=actor;combat.world=world();
        /* 第一次进入时认领角色已经有效的战斗目标；无效句柄会在下面正式清除。 */
        combat.target=Read32(role,0x143);
    }
    bool active=Read32(role,g_profile->active_offset)!=0;
    if (combat.history.count && !active && !combat.history.ended) {
        combat.history.ended=true;combat.history.end_tick=now;
    }
    uint32_t timeout=Read32((void *)g_profile->combo_timeout,0);
    if (combat.history.count && combat.history.ended && now-combat.history.end_tick>timeout)
        memset(&combat.history,0,sizeof combat.history);

    bool stick=g_intent.lx!=0 || g_intent.ly!=0;
    float dx=0,dy=0;
    if (stick) Control_WorldDirection(g_intent.lx,g_intent.ly,&dx,&dy);
    bool new_direction=stick && (!combat.had_stick || dx*combat.direction_x+dy*combat.direction_y<0.995f);
    if (new_direction) { combat.direction_x=dx;combat.direction_y=dy; }
    combat.had_stick=stick;
    bool held=g_intent.layer==LAYER_GAME && (g_intent.held&(KEY(PAD_X)|KEY(PAD_Y)));
    bool keep=Game_Enemy(role,Game_Resolve(combat.target));
    uint32_t target=combat.target;
    /* 切 RT/LB/RB 层不等于结束战斗。活动动作和重试期间，只有明确的新方向或失效才重选。 */
    if (!keep || new_direction || (!held && !active && !combat.pending && g_intent.layer==LAYER_GAME)) target=candidate;
    if (target && !Game_Enemy(role,Game_Resolve(target))) target=0;
    if (target!=combat.target) {
        combat.target=target;
        submit(18,(int)target,0,0);
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
            void *group=right_slot>=0 ? (void *)(uintptr_t)((This1)g_profile->lookup)((void *)g_profile->skill_groups,right_slot):NULL;
            combat.selection=Memory_Readable(group,0x36) && Read32(group,0x32)<2 ? right_slot:-1;
            combat.source=ACTION_RIGHT;
        } else {combat.selection=((This0)g_profile->left_get)(hud);combat.source=ACTION_LEFT;}
        combat.right=right;combat.pending=combat.selection>=0;combat.retries=5;fresh=true;
        if (edge) Log_Write("[战斗输入] 来源=%s 选择=%d 新按=1 忙碌=%d 历史=%u 已结束=%d。",
            right ? "右手":"左手",combat.selection,active,combat.history.count,combat.history.ended);
    }
    if (!combat.pending) return;
    if (!fresh) {
        if (!combat.retries) {combat.pending=false;return;}
        --combat.retries;
        /* 原版新按下可直接解析时间窗口，只有缓冲重试受忙碌门控制。
           不手工取消活动 Runtime，能否衔接或中断仍由原版动作协议决定。 */
        if (active) {
            if (!combat.retries) {combat.pending=false;Log_Write("[战斗丢弃] 缓冲期间持续忙碌，选择=%d。",combat.selection);}
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
            Log_Write("[战斗恢复] 旧序列不匹配选择=%d，清理已结束历史 %u 项后重试。",combat.selection,combat.history.count);
            memset(&combat.history,0,sizeof combat.history);
        }
        if (!combat.retries) {combat.pending=false;Log_Write("[战斗丢弃] 招式仍不可解析，选择=%d。",combat.selection);}
        return;
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
        ((This2)g_profile->facing_point)(role,(int)(uintptr_t)&point,(int)(uintptr_t)&origin);
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
    if (!combat.point_request && Game_Enemy(role,target_role)) {
        opcode=9;
        if (!combat.right && (!resolved.sequence || (resolved.method<10000 &&
            ((This1)g_profile->method_distance)(role,resolved.method)<=96))) opcode=11;
        a2=(int)combat.target;a3=0;
    } else {
        opcode=10;a2=point.x;a3=point.y;
    }
    submit(opcode,resolved.method,a2,a3);
    Log_Write("[战斗提交] 来源=%s 选择=%d 招式=%d 事件=%d 参数=%d,%d 通知=%d。",
        combat.right ? "右手":"左手",combat.selection,resolved.method,opcode,a2,a3,combat.execution_serial!=before_serial);
    /* 忙碌时的首次请求若未执行、也未进入原版 PendingAction，则只留在有期限的手柄缓冲。
       恢复之前已提交动作的来源，防止它稍后执行时被误认成这次新按键。 */
    unsigned pending=g_profile->pending_offset;
    bool accepted_pending=Read32(role,pending)==(unsigned)opcode &&
        Read32(role,pending+4)==(unsigned)resolved.method && Read32(role,pending+8)==(uint32_t)a2 &&
        Read32(role,pending+12)==(uint32_t)a3;
    if (active && combat.execution_serial==before_serial && !accepted_pending) {
        combat.issued=saved_issued;combat.issued_right=saved_right;
        combat.issued_selection=saved_selection;combat.issued_selector=saved_selector;combat.issued_combo=saved_combo;
        combat.issued_source=saved_source;combat.issued_slot=saved_slot;
        combat.issued_epoch=saved_epoch;combat.issued_index=saved_index;
        combat.pending=combat.retries!=0;
    }
}
