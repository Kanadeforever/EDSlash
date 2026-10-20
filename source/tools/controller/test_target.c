/* 复用真实战斗宿主，新增命中锁规则通过生产Target/Combat/Guard接口验证。 */
#define main baseline_game_main
#include "test_game.c"
#undef main

static void set_health(void *role,float value)
{ memcpy((BYTE *)role+g_profile->health_offset,&value,4); }
static void hit_enemy(void *runtime,float before,float after)
{
    set_health(roles[1],before);
    TargetHit hit=Target_BeginHit(roles[1],roles[0],runtime,true);
    set_health(roles[1],after);Target_EndHit(&hit,before);
}
static uintptr_t probe_gate;
static unsigned ring_draws;
static BYTE ring_surface[8],ring_sprite[8],ring_clip[16];
static int __fastcall fixture_ring_get(void *role,void *unused)
{ (void)unused;CHECK(role==roles[1]);return (int)(uintptr_t)ring_sprite; }
static void *__cdecl fixture_ring_clip(void) {return ring_clip;}
static int __fastcall fixture_ring_draw(void *sprite,void *unused,void *surface,int x,int y,int style,int shade,void *clip)
{
    (void)unused;CHECK(sprite==ring_sprite && surface==ring_surface && x==321 && y==234);
    CHECK(style==2 && shade==-1 && clip==ring_clip);++ring_draws;return 1;
}
/* 真正执行生产裸桥，模拟原绘制点的栈布局；返回后检查角色、画布寄存器没有改变。 */
static int __declspec(naked) probe_ring(void *role,void *surface,int x,int y)
{
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        sub esp,48h
        mov esi,[ebp+8]
        mov edi,esi
        mov ebx,[ebp+12]
        mov eax,[ebp+16]
        mov [esp+3ch],eax
        mov eax,[ebp+20]
        mov [esp+40h],eax
        stc
        pushfd
        pop eax
        mov [esp+38h],eax
        call dword ptr [probe_gate]
        pushfd
        pop edx
        cmp eax,34
        jne native_flags
        cmp edx,[esp+38h]
        jne corrupt
    native_flags:
        cmp esi,[ebp+8]
        jne corrupt
        cmp edi,[ebp+8]
        jne corrupt
        cmp ebx,[ebp+12]
        jne corrupt
        jmp done
    corrupt:
        mov eax,-1
    done:
        add esp,48h
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static void ring_visual_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);test_target_setting=1;record_actions=true;ring_draws=0;
    BYTE *code=VirtualAlloc(NULL,128,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);CHECK(code!=NULL);
    void *network=NULL;memcpy(code,"\x56\x8B\xF1\x8B\x0D",5);
    uint32_t global=(uint32_t)(uintptr_t)&network;memcpy(code+5,&global,4);
    memcpy(code+9,"\x8B\xCE\x5E\xE9",4);
    int32_t jump=(int32_t)((uintptr_t)native_target_set-(uintptr_t)code-17);memcpy(code+13,&jump,4);
    profile.target_set=(uintptr_t)code;profile.target_ring_gate=probe_gate=(uintptr_t)(code+64);
    profile.target_ring_resume=(uintptr_t)(code+96);
    profile.target_ring_get=(uintptr_t)fixture_ring_get;profile.target_ring_draw=(uintptr_t)fixture_ring_draw;
    profile.target_ring_clip=(uintptr_t)fixture_ring_clip;
    BYTE setter_bytes[17];memcpy(setter_bytes,code,17);
    CHECK(!Target_Initialize() && !memcmp(code,setter_bytes,17)); /* 未知绘制签名不能半安装 */
    memcpy(code+64,expansion ? "\x8B\x17\x68\x80\0\0\0":"\x8B\x16\x68\x80\0\0\0",7);
    /* 原分支替身消费重放的0x80参数后返回17，重编收尾替身直接返回34。 */
    memcpy(code+71,"\x83\xC4\x04\xB8\x11\0\0\0\xC3",9);
    memcpy(code+96,"\xB8\x22\0\0\0\xC3",6);
    patch_failure=code+64;CHECK(!Target_Initialize() && !memcmp(code,setter_bytes,17) && code[64]==0x8B);
    CHECK(Target_Initialize());
    CHECK(probe_ring(roles[1],ring_surface,321,234)==34 && !ring_draws);
    g_intent.held=g_intent.pressed=KEY(PAD_X);set_health(roles[1],100);Game_Update();
    hit_enemy(runtime_data,100,90);CHECK(Combat_Target()==2);
    CHECK(probe_ring(roles[1],ring_surface,321,234)==34 && ring_draws==1);
    CHECK(probe_ring(roles[0],ring_surface,321,234)==34 && ring_draws==1);
    CHECK(!Read32(roles[0],0x143) && last_opcode==10 && !move_requests);
    set_health(roles[1],0);CHECK(probe_ring(roles[1],ring_surface,321,234)==34 && ring_draws==1);
    g_intent.held=0;Target_SampleInput();CHECK(probe_ring(roles[1],ring_surface,321,234)==34 && ring_draws==1);
    test_target_setting=0;CHECK(probe_ring(roles[1],ring_surface,321,234)==17);
    test_target_setting=1;g_intent.layer=LAYER_MOUSE;CHECK(probe_ring(roles[1],ring_surface,321,234)==17);
    Target_Shutdown();CHECK(!memcmp(code,setter_bytes,17) && code[64]==0x8B && code[65]==(expansion ? 0x17:0x16));
    CHECK(Target_Initialize());code[64]=0x90;Target_Shutdown();CHECK(code[64]==0x90 && !memcmp(code,setter_bytes,17));
    CHECK(VirtualFree(code,0,MEM_RELEASE));Combat_Reset();Game_Release();
}
static void rebuilt_policy(bool expansion)
{
    Profile profile;configure(&profile,expansion);test_target_setting=1;record_actions=true;
    set_health(roles[1],100);Write32(roles[0],0x143,2);
    g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();
    CHECK(releases==1 && last_opcode==10 && !Combat_Target() && !Read32(roles[0],0x143));
    CHECK(last_aim.x==6528 && move_requests==0);
    hit_enemy(runtime_data,100,100);CHECK(!Combat_Target());
    hit_enemy(runtime_data,100,90);CHECK(Combat_Target()==2);
    /* 近战越过敌人后朝向敌人，但定向瞄准仍保留原来朝右的意图。 */
    Write32(roles[0],0x2C,6500);Target_Update(roles[0]);CHECK(last_aim.x==6464);
    WorldPoint aim=Target_Point(roles[0],true);CHECK(aim.x==6628 && aim.y==6400);
    CHECK(move_requests==0 && last_opcode==10);

    /* 用真实Method子记录识别飞行效果；不依赖硬编码气斩名称或招式编号。 */
    Write32(methods_data[1],0x8A,1);ptr(methods_data[1],0x8C,target_command);
    Write32(target_command,8,2);Write32(target_command,12,74);Write32(effects_data[0],0x22,201);
    CHECK(Target_IsDirectional(1002) && !Target_IsDirectional(1001));
    ptr(roles[0],profile.active_offset,NULL);Combat_End();Write32(hud_data,0x12C,222);
    g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();
    CHECK(last_opcode==10 && arg1==1002 && arg2==6628 && arg3==6400);
    last_aim=(WorldPoint){-1,-1};Target_Update(roles[0]);CHECK(last_aim.x==-1);

    /* 松手立即脱锁；在原受击回调中途发生松手，也拒绝回调后的迟到锁定。 */
    BYTE old_runtime[sizeof runtime_data];memcpy(old_runtime,runtime_data,sizeof old_runtime);
    TargetHit old_hit=Target_BeginHit(roles[1],roles[0],runtime_data,true);
    g_intent.held=g_intent.pressed=0;Target_SampleInput();CHECK(!Combat_Target());
    Game_Update();CHECK(!Combat_Target());
    set_health(roles[1],80);Target_EndHit(&old_hit,90);CHECK(!Combat_Target());
    ptr(roles[0],profile.active_offset,NULL);Combat_End();
    g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();
    hit_enemy(old_runtime,80,70);CHECK(!Combat_Target());
    hit_enemy(runtime_data,70,60);CHECK(Combat_Target()==2);
    Target_Interrupted();CHECK(!Combat_Target());
    hit_enemy(runtime_data,60,50);CHECK(!Combat_Target());

    /* 新攻击可重新取得目标；明确转向后旧运行时不能重新锁回去。 */
    ptr(roles[0],profile.active_offset,NULL);Combat_End();Game_Update();
    hit_enemy(runtime_data,50,40);CHECK(Combat_Target()==2);
    g_intent.lx=-1;g_intent.ly=0;Target_Update(roles[0]);CHECK(!Combat_Target());
    aim=Target_Point(roles[0],true);CHECK(aim.x<6500 && aim.y>6400);
    hit_enemy(runtime_data,40,30);CHECK(!Combat_Target());
    Target_Suspend();Combat_Reset();Game_Release();
}

static void native_isolation(bool expansion)
{
    Profile profile;configure(&profile,expansion);test_target_setting=1;
    BYTE *code=VirtualAlloc(NULL,128,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);CHECK(code!=NULL);
    void *network=NULL;memcpy(code,"\x56\x8B\xF1\x8B\x0D",5);
    uint32_t global=(uint32_t)(uintptr_t)&network;memcpy(code+5,&global,4);
    /* 重放真实9字节函数头，再还原this/ESI并进入独立的原生Setter替身。 */
    memcpy(code+9,"\x8B\xCE\x5E\xE9",4);
    int32_t jump=(int32_t)((uintptr_t)native_target_set-(uintptr_t)code-17);memcpy(code+13,&jump,4);
    profile.target_ring_gate=(uintptr_t)(code+64);profile.target_ring_resume=(uintptr_t)(code+96);
    memcpy(code+64,expansion ? "\x8B\x17\x68\x80\0\0\0":"\x8B\x16\x68\x80\0\0\0",7);
    BYTE original_ring[7];memcpy(original_ring,code+64,7);
    profile.target_set=(uintptr_t)code;BYTE original[17];memcpy(original,code,sizeof original);
    CHECK(Target_Initialize());
    typedef void (__fastcall *Set)(void *,void *,void *,int);
    ((Set)code)(roles[0],NULL,roles[1],0);CHECK(!Read32(roles[0],0x143));
    ((Set)code)(roles[1],NULL,roles[0],0);CHECK(Read32(roles[1],0x143)==1);
    test_target_setting=0;((Set)code)(roles[0],NULL,roles[1],0);CHECK(Read32(roles[0],0x143)==2);
    test_target_setting=1;((Set)code)(roles[0],NULL,NULL,0);CHECK(!Read32(roles[0],0x143));
    g_intent.layer=LAYER_NATIVE;((Set)code)(roles[0],NULL,roles[1],0);CHECK(Read32(roles[0],0x143)==2);
    g_intent.layer=LAYER_GAME;Target_Update(roles[0]);CHECK(!Read32(roles[0],0x143));
    Target_Shutdown();CHECK(!memcmp(code,original,sizeof original) && !memcmp(code+64,original_ring,7));CHECK(VirtualFree(code,0,MEM_RELEASE));
    Combat_Reset();Game_Release();
}
static void pending_and_lifecycle(bool expansion)
{
    for(unsigned reason=0;reason<6;++reason) {
        Profile profile;configure(&profile,expansion);test_target_setting=1;record_actions=true;
        set_health(roles[1],100);g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();
        hit_enemy(runtime_data,100,90);CHECK(Combat_Target()==2);
        if(reason==0)g_intent.layer=LAYER_MENU;
        else if(reason==1)g_input.focused=false;
        else if(reason==2)world_ptr=NULL;
        else if(reason==3)Write32(roles[0],0x14,99);
        else if(reason==4)set_health(roles[1],0);
        else test_target_setting=0;
        Target_SampleInput();CHECK(!Combat_Target());
        world_ptr=world_data;Combat_Reset();Game_Release();
    }
    Profile profile;configure(&profile,expansion);test_target_setting=1;
    g_intent.held=KEY(PAD_X);Target_Update(roles[0]);
    for(unsigned i=0;i<4;++i)Write32(roles[0],profile.pending_offset+i*4,(uint32_t[]){11,1001,2,0}[i]);
    Target_RecordPending(11,1001,2,0);g_intent.held=0;Target_SampleInput();
    CHECK(!Read32(roles[0],profile.pending_offset));
    g_intent.held=KEY(PAD_X);Target_Update(roles[0]);
    for(unsigned i=0;i<4;++i)Write32(roles[0],profile.pending_offset+i*4,(uint32_t[]){11,1001,2,0}[i]);
    Target_RecordPending(11,1001,2,0);Write32(roles[0],profile.pending_offset+4,1002);
    g_intent.held=0;Target_SampleInput();CHECK(Read32(roles[0],profile.pending_offset+4)==1002);
    /* 正式清理必须包含伴随对象，不能只清玩家可见的单个字段。 */
    BYTE companion[0x40]={0};ptr(roles[0],expansion ? 0x3DE:0x3D2,companion);
    Write32(roles[0],0x143,2);Write32(companion,0x28,2);Target_Update(roles[0]);
    CHECK(!Read32(roles[0],0x143) && !Read32(companion,0x28));
    ptr(roles[0],expansion ? 0x3DE:0x3D2,NULL);Combat_Reset();Game_Release();
}

int main(void)
{
    ring_visual_regression(false);ring_visual_regression(true);
    pending_and_lifecycle(false);pending_and_lifecycle(true);
    rebuilt_policy(false);rebuilt_policy(true);native_isolation(false);native_isolation(true);
    printf("命中建锁、松手中断/迟到弹道、近战转身、定向技能与原生玩家隔离通过：%u项\n",checks);
    return 0;
}
