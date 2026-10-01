#include "Guard.h"
#include "Combat.h"
#include <math.h>
#include <string.h>

typedef void (__attribute__((thiscall)) *Adjust)(void *,float);
typedef int (__attribute__((thiscall)) *RoleAction)(void *,int,int,int,int,int,int);
static bool installed,dodge_latched,dodge_request;
static bool charge_guard_on_hit=true;
static bool free_run=true;
static int guard_hit_cost=-1,attack_hit_recovery=-1;
static void *dodge_actor;
static unsigned dodge_retries;
/* 这些标量只在同步调用原版闪避启动函数期间有效，不伪造 CombatTarget 对象。 */
static int dodge_x __attribute__((used)),dodge_y __attribute__((used)),dodge_direction __attribute__((used));
static uintptr_t dodge_install __attribute__((used)),dodge_legacy __attribute__((used));
static uintptr_t dodge_resume __attribute__((used)),dodge_failure __attribute__((used));
typedef int (__attribute__((thiscall)) *Receiver)(void *,void *);
static Receiver original_receiver;
static void *receiver_gateway;
static BYTE saved[6][6];

static bool controlled(void *role)
{
    return role && role==Game_Player();
}

void Guard_Periodic(void *role,float amount)
{
    /* 原周期函数的其它分支还负责奔跑扣费和恢复，只替换防御分支中的这一次支出。
     * 原版的钳制及之后的体力阈值检查仍完整执行，不用每帧补回体力。 */
    if (charge_guard_on_hit && controlled(role) && Memory_Readable(role,0x21A) &&
        *((BYTE *)role+0x219)) amount=0.0f;
    ((Adjust)g_profile->stamina_adjust)(role,amount);
}

void Guard_RunCost(void *role,float amount)
{
    /* 只替换原奔跑支出调用；不再猜测战斗状态，原资格与其它恢复/消耗继续执行。 */
    if (free_run && controlled(role)) amount=0.0f;
    ((Adjust)g_profile->stamina_adjust)(role,amount);
}

static float stamina_per_hit(void *role,bool recovery)
{
    /* -1 表示沿用角色原防御一个周期的值；恢复的 -1 跟随实际配置后的扣减值。
     * 0 可关闭该次增减，正数为明确的体力点数，两个数值共用一次计算规则。 */
    int value=recovery ? attack_hit_recovery:guard_hit_cost;
    if (value>=0) return (float)value;
    if (recovery && guard_hit_cost>=0) return (float)guard_hit_cost;
    uintptr_t table=Read32(role,0);
    int unit=((This0)(uintptr_t)Read32((void *)table,0xA0))(role);
    return 2.0f*(float)unit;
}

int Guard_Hit(void *role,int index)
{
    int state=((This1)g_profile->guard_get)(role,index);
    if (!charge_guard_on_hit || !state || index!=0x6A || !controlled(role)) return state;
    /* 此调用点已经经过原版命中/来源资格，进入实际受击的防御处理。
     * 一个原生受击事件只执行一次：扣除原防御一个周期的 2×角色消耗值，
     * 不按伤害值比例另算、不按攻击动画或碰撞扫描次数重复扣。 */
    uintptr_t table=Read32(role,0);
    float cost=stamina_per_hit(role,false);
    ((Adjust)g_profile->stamina_adjust)(role,-cost);
    float stamina,threshold;
    uint32_t bits=Read32(role,g_profile->stamina_offset);memcpy(&stamina,&bits,4);
    bits=Read32((void *)g_profile->guard_threshold,0);memcpy(&threshold,&bits,4);
    if (stamina<threshold) {
        /* 与原周期扣费后的相同阈值、相同六参数 OFF 事件保持一致。
         * 后面的防御判定读取清理后的状态，归零后的受击由原版继续决定。 */
        ((RoleAction)(uintptr_t)Read32((void *)table,0x5C))(role,16,0,0,0,0,0);
    }
    Log_Write("[防御受击] 扣减体力=%.2f，体力=%.2f；原版防御状态=%u。",
              (double)cost,(double)stamina,(unsigned)*((BYTE *)role+0x219));
    return ((This1)g_profile->guard_get)(role,index);
}

void Guard_RecoverHit(void *victim,void *attacker,float health_before,bool was_enemy)
{
    if (!was_enemy || !controlled(attacker) || !Memory_Readable(victim,g_profile->health_offset+4) ||
        Game_Resolve(Read32(victim,0x14))!=victim) return;
    uint32_t bits=Read32(victim,g_profile->health_offset);float health_after;
    memcpy(&health_after,&bits,4);
    /* 原回调返回“已处理”也可能是 miss，必须核对实际生命减少。
     * 致命一击同样可恢复；回血、格挡、无敌或攻击失败都不满足此条件。 */
    if (!(health_after<health_before)) return;
    float amount=stamina_per_hit(attacker,true);
    if (amount<=0) return;
    ((Adjust)g_profile->stamina_adjust)(attacker,amount);
    Log_Write("[攻击命中] 敌人生命 %.2f→%.2f，恢复体力=%.2f。",
              (double)health_before,(double)health_after,(double)amount);
}

static int __attribute__((fastcall)) receiver_hook(void *victim,void *unused,void *runtime)
{
    (void)unused;
    void *attacker=NULL,*scope=ReadPtr((void *)g_profile->world_global,0);
    bool was_enemy=false;float health_before=0;
    if (Memory_Readable(runtime,0x93) && Memory_Readable(victim,g_profile->health_offset+4)) {
        /* 读取本次真实技能运行时的攻击者，不借用“最后打过此敌人”的残留字段。
         * 持续毒伤/环境扣血若不经过该攻击回调，不会给玩家错误回血。 */
        attacker=(void *)(uintptr_t)((This0)g_profile->runtime_owner)(runtime);
        void *method=ReadPtr(runtime,0x8F);
        was_enemy=controlled(attacker) && Game_Enemy(attacker,victim) &&
            Memory_Readable(method,0x24) && (Read32(method,0x22)&0xFFFFu)==0;
        uint32_t bits=Read32(victim,g_profile->health_offset);memcpy(&health_before,&bits,4);
    }
    int result=original_receiver(victim,runtime);
    /* 原回调可能死亡/换图；不能把旧场景的命中奖励加给新角色。 */
    if (scope==ReadPtr((void *)g_profile->world_global,0))
        Guard_RecoverHit(victim,attacker,health_before,was_enemy);
    return result;
}

static void __attribute__((fastcall)) periodic_hook(void *role,void *unused,float amount)
{ (void)unused;Guard_Periodic(role,amount); }
static void __attribute__((fastcall)) run_hook(void *role,void *unused,float amount)
{ (void)unused;Guard_RunCost(role,amount); }
static int __attribute__((fastcall)) hit_hook(void *role,void *unused,int index)
{ (void)unused;return Guard_Hit(role,index); }
static int __attribute__((fastcall)) input_release_hook(void *role,void *unused,int index)
{
    (void)unused;
    /* 手柄自己发 ON/OFF；阻止原版“真实 Alt 没按→OFF”覆盖手柄状态。
     * 只隔离输入生产点，业务状态 Getter 和物理鼠标/键盘模式保持真实值。 */
    if (g_input.connected && g_input.focused && g_intent.layer!=LAYER_MOUSE &&
        g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_NONE && controlled(role)) return 0;
    return ((This1)g_profile->guard_get)(role,index);
}

static int __attribute__((used,noinline)) claim_dodge(void *role)
{
    return dodge_request && role==dodge_actor && controlled(role);
}

static void __attribute__((naked,used)) dodge_hook(void)
{
    /* 拦截点前原版已检查冷却、硬直、活动技能和动作资格。
     * 保存所有寄存器和标志后询问来源；非手柄路径重放原来的空目标分支及压栈。
     * 手柄路径只安装三个方向标量，然后回原版成功/失败、碰撞和位移状态机。 */
    __asm__ volatile(
        "pushfl\n\tpushal\n\tpushl %esi\n\tcall _claim_dodge\n\taddl $4,%esp\n\t"
        "testl %eax,%eax\n\tjz 1f\n\tpopal\n\tpopfl\n\t"
        "pushl %ebx\n\tpushl _dodge_direction\n\tpushl _dodge_y\n\tpushl _dodge_x\n\t"
        "pushl $0x17\n\tmovl %esi,%ecx\n\tcall *_dodge_install\n\tjmp *_dodge_resume\n\t"
        "1:\n\tpopal\n\tpopfl\n\ttestl %edi,%edi\n\tjz 2f\n\t"
        "pushl %edi\n\tpushl %esi\n\tjmp *_dodge_legacy\n\t2:\n\tjmp *_dodge_failure\n\t");
}

void Guard_Reset(void)
{
    dodge_latched=false;dodge_request=false;dodge_actor=NULL;dodge_retries=0;
}

void Guard_Update(void *role)
{
    if (!installed || !controlled(role) || g_intent.layer!=LAYER_GUARD) {Guard_Reset();return;}
    if (!*((BYTE *)role+0x219) && ((This0)g_profile->guard_check)(role)) {
        /* 读取权威状态，资格和延迟仍交给原生事件，不直接写防御字节。 */
        void *root=ReadPtr((void *)g_profile->world_global,0);
        ((This4)g_profile->submit)(ReadPtr(root,0x30),16,1,0,0);
    }
    bool stick=g_intent.lx!=0 || g_intent.ly!=0;
    if (!stick) {dodge_latched=false;dodge_retries=0;return;}
    if (!dodge_latched) {dodge_latched=true;dodge_retries=5;}
    if (!dodge_retries) return;
    --dodge_retries;
    float dx,dy;Control_WorldDirection(g_intent.lx,g_intent.ly,&dx,&dy);
    WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
    WorldPoint point={origin.x+(int)lroundf(dx*256.0f),origin.y+(int)lroundf(dy*256.0f)};
    typedef int (__cdecl *Direction)(const WorldPoint *,const WorldPoint *);
    dodge_x=point.x;dodge_y=point.y;dodge_direction=((Direction)g_profile->direction8)(&point,&origin);
    dodge_actor=role;dodge_request=true;
    int result=((This0)g_profile->dodge_start)(role);
    dodge_request=false;dodge_actor=NULL;
    if (result) {
        dodge_retries=0;
        Log_Write("[方向闪避] 原版启动成功，参考点=%d,%d，方向=%d；摇杆回中后重新触发。",
                  point.x,point.y,dodge_direction);
    }
}

bool Guard_Initialize(void)
{
    if (installed) return true;
    charge_guard_on_hit=Config_Number(L"Combat",L"ChargeGuardOnHit",1,0,1)!=0;
    free_run=Config_Number(L"Combat",L"FreeRun",1,0,1)!=0;
    guard_hit_cost=Config_Number(L"Combat",L"GuardHitCost",-1,-1,10000);
    attack_hit_recovery=Config_Number(L"Combat",L"AttackHitRecovery",-1,-1,10000);
    Log_Write("[战斗配置] 防御受击扣费=%u；0 时恢复原版周期空耗。",charge_guard_on_hit ? 1u:0u);
    Log_Write("[战斗配置] 奔跑免扣费=%u；不再按战斗状态区分。",free_run ? 1u:0u);
    uintptr_t targets[6]={g_profile->guard_periodic_call,g_profile->guard_hit_call,
                         g_profile->guard_input_release_call,g_profile->guard_run_call,g_profile->dodge_gate,g_profile->hit_receiver};
    uintptr_t replacements[6]={(uintptr_t)periodic_hook,(uintptr_t)hit_hook,
                              (uintptr_t)input_release_hook,(uintptr_t)run_hook,(uintptr_t)dodge_hook,(uintptr_t)receiver_hook};
    uintptr_t callees[4]={g_profile->stamina_adjust,g_profile->guard_get,g_profile->guard_get,g_profile->stamina_adjust};
    for (unsigned i=0;i<6;++i) {
        unsigned size=i>=4 ? 6:5;
        if (!Memory_Readable((void *)targets[i],size)) return false;
        memcpy(saved[i],(void *)targets[i],size);
        if (i<4) {
            int32_t displacement;memcpy(&displacement,saved[i]+1,4);
            if (saved[i][0]!=0xE8 || targets[i]+5+displacement!=callees[i]) return false;
        } else if (i==4) {
            if (memcmp(saved[i],"\x85\xFF\x74\x6A\x57\x56",6)) return false;
        } else if (memcmp(saved[i],"\x64\xA1\x00\x00\x00\x00",6)) return false;
    }
    /* 受击函数头第一条完整指令没有相对引用，复制到网关后跳回 +6 即可。
     * 与上面的闪避短跳转不同，不能把两种覆盖方式混用。 */
    receiver_gateway=VirtualAlloc(NULL,11,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if (!receiver_gateway) return false;
    BYTE gateway_bytes[11];memcpy(gateway_bytes,saved[5],6);gateway_bytes[6]=0xE9;
    int32_t back=(int32_t)(g_profile->hit_receiver+6-(uintptr_t)receiver_gateway-11);
    memcpy(gateway_bytes+7,&back,4);
    if (!Memory_Patch(receiver_gateway,gateway_bytes,11)) {
        VirtualFree(receiver_gateway,0,MEM_RELEASE);receiver_gateway=NULL;return false;
    }
    original_receiver=(Receiver)receiver_gateway;
    dodge_install=g_profile->install_state;dodge_legacy=g_profile->dodge_legacy;
    dodge_resume=g_profile->dodge_resume;dodge_failure=g_profile->dodge_failure;
    for (unsigned i=0;i<6;++i) {
        BYTE bytes[6]={i>=4 ? 0xE9:0xE8,0,0,0,0,0x90};
        int32_t displacement=(int32_t)(replacements[i]-targets[i]-5);
        memcpy(bytes+1,&displacement,4);
        if (!Memory_Patch((void *)targets[i],bytes,i>=4 ? 6:5)) {
            while (i) {--i;Memory_Patch((void *)targets[i],saved[i],i>=4 ? 6:5);}
            VirtualFree(receiver_gateway,0,MEM_RELEASE);receiver_gateway=NULL;original_receiver=NULL;
            Log_Write("[防御][停止] 入口安装失败，撤回本模块修改。");return false;
        }
    }
    installed=true;
    Log_Write("[防御] 原生 ON/OFF、可配置防御/奔跑扣费及方向闪避入口已安装；原版耗尽规则保留。");
    return true;
}

void Guard_Shutdown(void)
{
    if (!installed) return;
    uintptr_t targets[6]={g_profile->guard_periodic_call,g_profile->guard_hit_call,
                         g_profile->guard_input_release_call,g_profile->guard_run_call,g_profile->dodge_gate,g_profile->hit_receiver};
    uintptr_t replacements[6]={(uintptr_t)periodic_hook,(uintptr_t)hit_hook,
                              (uintptr_t)input_release_hook,(uintptr_t)run_hook,(uintptr_t)dodge_hook,(uintptr_t)receiver_hook};
    bool receiver_removed=false;
    for (unsigned i=0;i<6;++i) {
        BYTE expected[6]={i>=4 ? 0xE9:0xE8,0,0,0,0,0x90};
        int32_t displacement=(int32_t)(replacements[i]-targets[i]-5);
        memcpy(expected+1,&displacement,4);
        unsigned size=i>=4 ? 6:5;
        if (Memory_Readable((void *)targets[i],size) && !memcmp((void *)targets[i],expected,size)) {
            bool removed=Memory_Patch((void *)targets[i],saved[i],size);
            if(i==5) receiver_removed=removed;
        }
    }
    if (receiver_removed) {VirtualFree(receiver_gateway,0,MEM_RELEASE);receiver_gateway=NULL;original_receiver=NULL;}
    installed=false;Guard_Reset();
}
