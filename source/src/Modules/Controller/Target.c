#include "Target.h"
#include <math.h>
#include <string.h>

/* 目标只由有效命中取得。瞄准方向独立保存，敌人绕后不会改写定向技能的意图。 */
static struct {
    void *world;
    uint32_t actor,target,generation;
    bool held,aim_ready,cleaned,pending_owned,rebuilt;
    float aim_x,aim_y;
    int pending[4];
    uint32_t roots[64];
    unsigned root_count,root_next;
    bool class_cached,class_directional;
    int class_method;
    uintptr_t class_methods,class_effects;
} target;
static BYTE saved_setter[9];
static void *setter_gateway;
static bool installed;
static BYTE saved_ring[7];
static uintptr_t ring_native,ring_done;
static bool ring_installed;
typedef void (__fastcall *Setter)(void *,void *,void *,int);

static void *scope(void) { return ReadPtr((void *)g_profile->world_global,0); }
bool Target_Rebuilt(void) { return RuntimeConfig_GetInt(CONFIG_TARGET_MODE)==1; }
static bool owns_input(void)
{
    return g_profile && g_input.connected && g_input.focused &&
        RuntimeConfig_GetInt(CONFIG_CONTROLLER_ENABLED) && g_intent.layer!=LAYER_MOUSE &&
        g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_NONE;
}
/* 返回1表示本绘制分支由重编处理，包括“没有目标所以不画”；返回0沿原版资格继续。
 * 原圈资源按敌人体型选择，坐标、绘制层、颜色和裁剪均复用原角色绘制的参数。 */
static int __cdecl draw_ring(void *role,void *surface,int x,int y)
{
    if(!Target_Rebuilt() || !owns_input())return 0;
    uint32_t handle=Target_Handle();
    if(handle && Game_Resolve(handle)==role) {
        void *sprite=(void *)(uintptr_t)((This0)g_profile->target_ring_get)(role,NULL);
        if(sprite) {
            typedef void *(__cdecl *Clip)(void);
            typedef int (__fastcall *Draw)(void *,void *,void *,int,int,int,int,void *);
            void *clip=((Clip)g_profile->target_ring_clip)();
            ((Draw)g_profile->target_ring_draw)(sprite,NULL,surface,x,y,2,-1,clip);
        }
    }
    return 1;
}
/* 分支入口的ESP+40/+44是已经投影好的脚下坐标。保存标志和全部通用寄存器后，
 * 对应位置移到ESP+64/+68；两个版本分别用ESI/EDI保存当前被绘制的角色。 */
static void __declspec(naked) ring_base_hook(void)
{
    __asm {
        pushfd
        pushad
        mov eax,[esp+64h]
        mov edx,[esp+68h]
        push edx
        push eax
        push ebx
        push esi
        call draw_ring
        add esp,16
        test eax,eax
        jz native_branch
        popad
        popfd
        jmp dword ptr [ring_done]
    native_branch:
        popad
        popfd
        mov edx,[esi]
        push 80h
        jmp dword ptr [ring_native]
    }
}
static void __declspec(naked) ring_expansion_hook(void)
{
    __asm {
        pushfd
        pushad
        mov eax,[esp+64h]
        mov edx,[esp+68h]
        push edx
        push eax
        push ebx
        push edi
        call draw_ring
        add esp,16
        test eax,eax
        jz native_branch
        popad
        popfd
        jmp dword ptr [ring_done]
    native_branch:
        popad
        popfd
        mov edx,[edi]
        push 80h
        jmp dword ptr [ring_native]
    }
}
static void ring_patch(BYTE *out)
{
    memset(out,0x90,7);out[0]=0xE9;
    uintptr_t hook=g_profile->game_id==GAME_ID_DAOJIAN ? (uintptr_t)ring_base_hook:(uintptr_t)ring_expansion_hook;
    int32_t relative=(int32_t)(hook-g_profile->target_ring_gate-5);memcpy(out+1,&relative,4);
}
static bool attack_held(void)
{
    if (!owns_input()) return false;
    if (g_intent.layer==LAYER_GAME) return (g_intent.held&(KEY(PAD_X)|KEY(PAD_Y)))!=0;
    if (g_intent.layer==LAYER_SKILL || g_intent.layer==LAYER_ITEM || g_intent.layer==LAYER_DUAL)
        return (g_intent.held&(KEY(PAD_A)|KEY(PAD_B)|KEY(PAD_X)|KEY(PAD_Y)))!=0;
    return false;
}
static void cancel_pending(void)
{
    if (!target.pending_owned || !g_profile) {target.pending_owned=false;return;}
    void *role=Game_Player();
    if (target.pending_owned && role && target.world==scope() && target.actor==Read32(role,0x14)) {
        bool same=true;
        for (unsigned i=0;i<4;++i)
            if (Read32(role,g_profile->pending_offset+i*4)!=(uint32_t)target.pending[i]) same=false;
        /* 仅撤销本模块实际提交且尚未被替换的请求，不清理脚本或其它来源的动作。 */
        if (same && g_profile->pending_clear) ((This0)g_profile->pending_clear)(role,NULL);
    }
    target.pending_owned=false;
}
void Target_Interrupted(void)
{
    cancel_pending();target.target=0;target.root_count=target.root_next=0;++target.generation;
}
void Target_Suspend(void) { Target_Interrupted();target.held=false; }
void Target_SampleInput(void)
{
    if (Target_Rebuilt() && target.held && !attack_held()) Target_Suspend();
}
void Target_Reset(void)
{
    uint32_t next=target.generation+1;
    cancel_pending();memset(&target,0,sizeof target);target.generation=next;
}

static void __fastcall setter_hook(void *role,void *unused,void *candidate,int policy)
{
    (void)unused;
    /* 原生ExecuteMethod和受击反应都能重建持续目标。只隔离当前手柄玩家的非空写入；
     * 空目标清理、NPC、键鼠与原版档继续执行完整Setter及其资格检查。 */
    if (candidate && Target_Rebuilt() && owns_input() && role==Game_Player()) return;
    ((Setter)setter_gateway)(role,NULL,candidate,policy);
}

bool Target_Initialize(void)
{
    if (installed) return true;
    BYTE expected_ring[7]={0x8B,0x16,0x68,0x80,0,0,0};
    if(g_profile->game_id!=GAME_ID_DAOJIAN)expected_ring[1]=0x17;
    if(!Memory_Readable((void *)g_profile->target_ring_gate,7) ||
        memcmp((void *)g_profile->target_ring_gate,expected_ring,7))return false;
    memcpy(saved_ring,expected_ring,7);
    BYTE *entry=(BYTE *)g_profile->target_set;
    /* 三条完整指令共9字节，不含相对跳转；最后一条读取当前版本的原生全局。 */
    if (!Memory_Readable(entry,9) || memcmp(entry,"\x56\x8B\xF1\x8B\x0D",5)) return false;
    memcpy(saved_setter,entry,9);
    setter_gateway=VirtualAlloc(NULL,14,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if (!setter_gateway) return false;
    BYTE gateway[14];memcpy(gateway,saved_setter,9);gateway[9]=0xE9;
    int32_t relative=(int32_t)((uintptr_t)entry+9-(uintptr_t)setter_gateway-14);
    memcpy(gateway+10,&relative,4);
    if (!Memory_Patch(setter_gateway,gateway,sizeof gateway)) goto failed;
    BYTE replacement[9]={0xE9,0,0,0,0,0x90,0x90,0x90,0x90};
    relative=(int32_t)((uintptr_t)setter_hook-(uintptr_t)entry-5);memcpy(replacement+1,&relative,4);
    if (!Memory_Patch(entry,replacement,sizeof replacement)) goto failed;
    ring_native=g_profile->target_ring_gate+7;ring_done=g_profile->target_ring_resume;
    BYTE ring_bytes[7];ring_patch(ring_bytes);
    if(!Memory_Patch((void *)g_profile->target_ring_gate,ring_bytes,7)) {
        /* 若恢复入口也失败，保留仍被入口引用的网关，交卸载路径重试，避免悬空跳转。 */
        if(!Memory_Patch(entry,saved_setter,9)) {installed=true;return false;}
        goto failed;
    }
    ring_installed=true;
    installed=true;return true;
failed:
    VirtualFree(setter_gateway,0,MEM_RELEASE);setter_gateway=NULL;return false;
}
void Target_Shutdown(void)
{
    Target_Suspend();
    if (!installed) return;
    if(ring_installed) {
        BYTE expected_ring[7];ring_patch(expected_ring);
        /* 后来被其它插件替换的分支不能强行覆盖。此桥没有动态网关需要释放。 */
        if(Memory_Readable((void *)g_profile->target_ring_gate,7) &&
            !memcmp((void *)g_profile->target_ring_gate,expected_ring,7))
            Memory_Patch((void *)g_profile->target_ring_gate,saved_ring,7);
        ring_installed=false;
    }
    BYTE expected[9]={0xE9,0,0,0,0,0x90,0x90,0x90,0x90};
    int32_t relative=(int32_t)((uintptr_t)setter_hook-g_profile->target_set-5);memcpy(expected+1,&relative,4);
    if (Memory_Readable((void *)g_profile->target_set,9) &&
        !memcmp((void *)g_profile->target_set,expected,9) &&
        Memory_Patch((void *)g_profile->target_set,saved_setter,9)) {
        VirtualFree(setter_gateway,0,MEM_RELEASE);setter_gateway=NULL;installed=false;
    }
}

/* 只遍历本招实际引用的效果链。飞行效果201使用独立方向；未知或过深的链也不强制追敌。 */
static bool scan_directional(int method)
{
    if (method>=10000) return true;
    int ids[64];bool effects[64];unsigned used=1;ids[0]=method;effects[0]=false;
    for (unsigned at=0;at<used;++at) {
        uintptr_t table=effects[at] ? g_profile->effects:g_profile->methods;
        if (!table) return true;
        void *record=(void *)(uintptr_t)((This1)g_profile->lookup)((void *)table,NULL,ids[at]);
        unsigned count_offset=effects[at] ? 0x40:0x8A,ptr_offset=effects[at] ? 0x42:0x8C;
        if (!Memory_Readable(record,ptr_offset+4)) return true;
        if (effects[at] && (Read32(record,0x22)&0xFFFFu)==201) return true;
        /* 可变落点动作保留调用者指定的方向/距离；这是一条瞄准策略，不把该字段当作全技能分类。 */
        if (!effects[at] && *((BYTE *)record+0x57)==1) return true;
        unsigned count=Read32(record,count_offset)&0xFFFFu;void *commands=ReadPtr(record,ptr_offset);
        if (count>128 || (count && !Memory_Readable(commands,count*32))) return true;
        for (unsigned i=0;i<count;++i) {
            unsigned code=Read32(commands,i*32+8);
            if (code!=2 && code!=9) continue;
            int next=(int)Read32(commands,i*32+12);bool effect=code==2,seen=false;
            for (unsigned j=0;j<used;++j) if (ids[j]==next && effects[j]==effect) seen=true;
            if (seen) continue;
            if (used==64) return true;
            ids[used]=next;effects[used++]=effect;
        }
    }
    return false;
}

/* 同一招的分类只在首次使用时解析；正常战斗帧不反复查询整条效果链。 */
bool Target_IsDirectional(int method)
{
    if (target.class_cached && target.class_method==method && target.class_methods==g_profile->methods &&
        target.class_effects==g_profile->effects) return target.class_directional;
    bool directional=scan_directional(method);
    target.class_cached=true;target.class_method=method;target.class_methods=g_profile->methods;
    target.class_effects=g_profile->effects;target.class_directional=directional;
    return directional;
}

static WorldPoint facing_point(void *role)
{
    WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
    unsigned count=Read32(role,0x2BF)==16 ? 16:8,facing=Read32(role,0x14B);
    target.aim_x=0;target.aim_y=1;
    for (unsigned i=0;i<count;++i) {
        float angle=(float)i*6.28318530718f/count;
        WorldPoint probe={origin.x+(int)lroundf(cosf(angle)*256),origin.y+(int)lroundf(sinf(angle)*256)};
        if ((unsigned)((This2)g_profile->facing_direction)(role,NULL,(int)(uintptr_t)&probe,(int)(uintptr_t)&origin)==facing) {
            target.aim_x=cosf(angle);target.aim_y=sinf(angle);break;
        }
    }
    target.aim_ready=true;return origin;
}
WorldPoint Target_Point(void *role,bool directional)
{
    WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
    void *enemy=Game_Resolve(Target_Handle());
    if (!directional && Game_Enemy(role,enemy))
        return (WorldPoint){(int)Read32(enemy,0x2C),(int)Read32(enemy,0x30)};
    if (!target.aim_ready) (void)facing_point(role);
    return (WorldPoint){origin.x+(int)lroundf(target.aim_x*128),origin.y+(int)lroundf(target.aim_y*128)};
}
void Target_Update(void *role)
{
    if (!role) {Target_Suspend();return;}
    if (target.actor!=Read32(role,0x14) || target.world!=scope()) {
        Target_Reset();target.actor=Read32(role,0x14);target.world=scope();
    }
    if (!Target_Rebuilt()) {if(target.rebuilt)Target_Suspend();target.rebuilt=false;target.cleaned=false;return;}
    if (!owns_input()) {Target_Suspend();target.cleaned=false;return;}
    if (!target.rebuilt) {Target_Interrupted();target.rebuilt=true;}
    if (!target.cleaned || Read32(role,0x143)) {
        /* 正式Clear同时维护companion镜像，不能只把Role+143写零。 */
        if (g_profile->target_clear) ((This0)g_profile->target_clear)(role,NULL);
        target.cleaned=true;
    }
    bool held=attack_held(),stick=g_intent.lx!=0 || g_intent.ly!=0;
    if (!held && target.held) Target_Interrupted();
    target.held=held;
    if (!held) target.target=0;
    if (!target.aim_ready) (void)facing_point(role);
    if (stick) {
        float x,y;Control_WorldDirection(g_intent.lx,g_intent.ly,&x,&y);
        if (x*target.aim_x+y*target.aim_y<0.995f) {
            /* 明确的新方向结束旧命中会话，旧飞行物不能把刚放弃的敌人重新锁回。 */
            Target_Interrupted();target.aim_x=x;target.aim_y=y;
        }
    }
    if (target.target && !Game_Enemy(role,Game_Resolve(target.target))) target.target=0;
    void *active=ReadPtr(role,g_profile->active_offset);
    if (held && target.target && (!active || !Target_IsDirectional((int)Read32(active,0x97)))) {
        WorldPoint point=Target_Point(role,false),origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
        /* 近战越过目标时使用原转向接口面对敌人；不提交走路、追近或取消动作。 */
        ((This2)g_profile->facing_point)(role,NULL,(int)(uintptr_t)&point,(int)(uintptr_t)&origin);
    }
}
uint32_t Target_Handle(void)
{
    if (!Target_Rebuilt() || !attack_held() || !target.actor || !target.target) return 0;
    void *role=Game_Player(),*enemy=Game_Resolve(target.target);
    if (target.world!=scope() || target.actor!=Read32(role,0x14) || !Game_Enemy(role,enemy)) {
        target.target=0;return 0;
    }
    uint32_t bits=Read32(enemy,g_profile->health_offset);float health;memcpy(&health,&bits,4);
    if (!isfinite(health) || health<=0) target.target=0;
    return target.target;
}
void Target_RecordRuntime(void *runtime)
{
    if (!Target_Rebuilt() || !attack_held() || !target.held || !Memory_Readable(runtime,0x18)) return;
    uint32_t handle=Read32(runtime,0x14);if (!handle) return;
    target.roots[target.root_next]=handle;target.root_next=(target.root_next+1)%64;
    if (target.root_count<64) ++target.root_count;
}
void Target_RecordPending(int opcode,int method,int a2,int a3)
{
    target.pending[0]=opcode;target.pending[1]=method;target.pending[2]=a2;target.pending[3]=a3;
    target.pending_owned=true;
}
TargetHit Target_BeginHit(void *victim,void *attacker,void *runtime,bool was_enemy)
{
    TargetHit hit={0};
    if (!was_enemy || !Target_Rebuilt() || !attack_held() || !target.held ||
        attacker!=Game_Player() || target.actor!=Read32(attacker,0x14) || target.world!=scope()) return hit;
    /* 子效果继承父运行时Handle。只有本攻击会话建立的根或其可验证后代能够建锁。 */
    for (unsigned depth=0;depth<8 && Memory_Readable(runtime,0x44B);++depth) {
        uint32_t own=Read32(runtime,0x14),parent=Read32(runtime,0x447);
        for (unsigned i=0;i<target.root_count;++i) if (target.roots[i]==own || target.roots[i]==parent) hit.eligible=true;
        if (hit.eligible || !parent || parent==own) break;
        runtime=Game_Resolve(parent);
    }
    hit.world=scope();hit.actor=target.actor;hit.generation=target.generation;
    hit.victim=victim;hit.victim_handle=Read32(victim,0x14);return hit;
}
void Target_EndHit(const TargetHit *hit,float health_before)
{
    if (!hit || !hit->eligible || !Target_Rebuilt() || !attack_held() ||
        hit->generation!=target.generation || hit->world!=scope() || hit->actor!=target.actor ||
        Game_Resolve(hit->victim_handle)!=hit->victim || !Game_Enemy(Game_Player(),hit->victim)) return;
    uint32_t bits=Read32(hit->victim,g_profile->health_offset);float after;memcpy(&after,&bits,4);
    /* 原受击返回1也可能是miss；首版以真实伤害确认命中，格挡/零伤害不误建锁。 */
    if (!isfinite(health_before) || !isfinite(after) || health_before<=0 || after>=health_before || after<=0) return;
    if (!target.target) target.target=hit->victim_handle;
}
