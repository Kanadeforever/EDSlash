#include "Combat.h"
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
    /* 输入请求和已经提交给原版的动作分开保存：新按 Y 不能把尚未执行的旧 X 标成右手。 */
    bool issued,issued_right;
    int issued_selector,issued_combo;
    uint32_t execution_serial;
} combat;

static void *world(void) { return ReadPtr((void *)g_profile->world_global,0); }
static void *manager(void) { return ReadPtr(world(),0x30); }
static uint32_t tick(void) { return Read32((void *)g_profile->game_tick,0); }
static void submit(int opcode,int a,int b,int c) { ((This4)g_profile->submit)(manager(),opcode,a,b,c); }

void Combat_Reset(void) { memset(&combat,0,sizeof combat); }
void Combat_Suspend(void) { combat.pending=false; }
uint32_t Combat_Target(void) { return combat.target; }
bool Combat_AllowsMouseRetry(void) { return !g_input.connected || g_intent.layer==LAYER_MOUSE; }

bool Combat_OwnsHistory(void)
{
    /* 原版调用点已经限定当前受控玩家。此处再验证来源和场景，鼠标模式仍执行旧业务。 */
    return g_profile && combat.owned && combat.actor && combat.world==world() &&
        combat.actor==Read32(manager(),0x0C) && g_input.connected && g_input.focused &&
        g_intent.layer!=LAYER_MOUSE && g_intent.layer!=LAYER_NONE;
}

void Combat_Record(int selector,int direction)
{
    if (!Combat_OwnsHistory()) return;
    if (!combat.issued || (combat.issued_selector>=0 && combat.issued_selector!=selector)) {
        Log_Write("[战斗通知] 未匹配到本次手柄提交，组=%d；不推进手柄历史或连招游标。",selector);
        return;
    }
    combat.issued=false;++combat.execution_serial;
    /* 只由原生 Runtime 成功建立通知追加。原版自动续段关闭历史门时不会进入此调用点。 */
    if (combat.history.count==64) {
        memmove(combat.history.selectors,combat.history.selectors+1,63*sizeof(int));
        --combat.history.count;
    }
    combat.history.selectors[combat.history.count++]=selector;
    combat.history.start_tick=tick();combat.history.ended=false;combat.history.direction=direction;
    void *hud=ReadPtr((void *)g_profile->skill_global,0);
    int selected=(int)Read32(hud,0x12C);
    if (combat.issued_right && selected==combat.issued_combo && selected>=-4 && selected<=-1 && Memory_Readable(hud,0xC20)) {
        /* 以保存的左右手来源推进原生自定义连招游标，不读取任何鼠标按钮全局标志。 */
        Write32(hud,0x120,Read32(hud,0x120)+1);
    }
    Log_Write("[战斗执行] 原生动作已建立，组=%d 方向=%d 来源=%s 历史=%u。",
        selector,direction,combat.issued_right ? "右手":"左手",combat.history.count);
}

void Combat_End(void)
{
    if (!Combat_OwnsHistory() || !combat.history.count) return;
    /* 原生结束回调可能和下一段创建发生在同一更新周期，不能只靠下一帧看空指针判断结束。 */
    combat.history.ended=true;combat.history.end_tick=tick();
}

static WorldPoint aim_point(void *role)
{
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

    /* 快捷层本身不发攻击，也不把短暂切层当成清除目标；尚未提交的请求可取消。 */
    if (g_intent.layer!=LAYER_GAME) { combat.pending=false;return; }
    bool edge=(g_intent.pressed&(KEY(PAD_X)|KEY(PAD_Y)))!=0;
    bool fresh=false;
    bool both=(g_intent.held&(KEY(PAD_X)|KEY(PAD_Y)))==(KEY(PAD_X)|KEY(PAD_Y));
    bool right=(g_intent.pressed&KEY(PAD_Y)) ? true:
        (g_intent.pressed&KEY(PAD_X)) ? false:both ? combat.preferred_right:(g_intent.held&KEY(PAD_Y))!=0;
    /* 两键仍都按着时延续最后一次明确边沿的来源，不在下一帧又固定跳回 Y。 */
    if (held) combat.preferred_right=right;
    if (edge || (held && !combat.pending && !active)) {
        void *hud=ReadPtr((void *)g_profile->skill_global,0);
        if (!Memory_Readable(hud,0xC20)) return;
        combat.selection=((This0)(right ? g_profile->right_get:g_profile->left_get))(hud);
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
    if (!Skill_Resolve(role,combat.selection,&point,&combat.history,&resolved)) {
        if (combat.history.count && combat.history.ended) {
            /* 原版 SkillRelease 失败且历史已有结束标记时会清历史，再让缓冲下一次按首招解析。
               dev4 漏掉了这条分支，导致跨招式一直失败直到整段历史超时。 */
            Log_Write("[战斗恢复] 旧序列不匹配选择=%d，清理已结束历史 %u 项后重试。",combat.selection,combat.history.count);
            memset(&combat.history,0,sizeof combat.history);
        }
        if (!combat.retries) {combat.pending=false;Log_Write("[战斗丢弃] 招式仍不可解析，选择=%d。",combat.selection);}
        return;
    }
    combat.pending=false;
    /* 首动作和原版一致保留执行资格预检，后续 Runtime 仍会执行自己的完整校验。 */
    if (!resolved.sequence && resolved.method<10000) {
        void *record=(void *)(uintptr_t)((This1)g_profile->role_method)(role,resolved.method);
        if (!record || !((This1)g_profile->method_usable)(role,(int)(uintptr_t)record)) {
            /* 原生缓冲的重试在资格暂时失败时仍保留剩余次数；不能在动作刚结束的
               恢复窗口里，把已经排队的输入提前丢掉。不存在的记录则不继续等待。 */
            combat.pending=record && combat.retries!=0;
            return;
        }
    }
    WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
    if (!active && Read32(role,0x73)==1)
        ((This2)g_profile->facing_point)(role,(int)(uintptr_t)&point,(int)(uintptr_t)&origin);
    combat.owned=true;
    bool saved_issued=combat.issued,saved_right=combat.issued_right;
    int saved_selector=combat.issued_selector,saved_combo=combat.issued_combo;
    uint32_t before_serial=combat.execution_serial;
    combat.issued=true;combat.issued_right=combat.right;combat.issued_selector=resolved.selector;
    combat.issued_combo=(int)Read32(ReadPtr((void *)g_profile->skill_global,0),0x12C);
    void *target_role=Game_Resolve(combat.target);
    int opcode,a2,a3;
    if (Game_Enemy(role,target_role)) {
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
        combat.issued_selector=saved_selector;combat.issued_combo=saved_combo;
        combat.pending=combat.retries!=0;
    }
}
