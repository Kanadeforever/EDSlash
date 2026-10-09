#define EDSLASH_REAL_CONFIG
/* 复用已验证的32位游戏替身，配置服务改链接真实Config/Toml/FileIO，不复制其保存或应用算法。 */
#define main baseline_game_main
#include "test_game.c"
#undef main
#include <wchar.h>

static void start_settings_dash(void)
{
    Guard_Reset();g_intent.layer=LAYER_GUARD;g_intent.lx=1;g_intent.ly=0.5f;
    *((BYTE *)roles[0]+0x219)=1;
    Guard_Update(roles[0]);CHECK(Guard_IsDodging(roles[0]));
}
static void finish_settings_dash(void)
{
    for(unsigned i=0;i<8 && Guard_IsDodging(roles[0]);++i)
        ((This0)patched_callee(g_profile->dodge_motion_call))(roles[0], NULL);
    CHECK(!Guard_IsDodging(roles[0]));
}
static void settings_regression(bool expansion)
{
    wchar_t path[MAX_PATH];CHECK(GetModuleFileNameW(NULL,path,MAX_PATH)>0);
    wchar_t *slash=wcsrchr(path,L'\\');CHECK(slash!=NULL);
    swprintf(slash+1,MAX_PATH-(size_t)(slash+1-path),L"guard_settings_%lu_%u.toml",GetCurrentProcessId(),expansion);
    /* 测试文件只在本测试程序旁的.build内，绝不读取或覆盖玩家TOML。 */
    DeleteFileW(path);CHECK(RuntimeConfig_OpenPath(path));
    Profile profile;configure(&profile,expansion);
    profile.stamina_offset=expansion?0x3B6:0x3AA;
    profile.health_offset=expansion?0x3AE:0x3A2;
    profile.guard_threshold=(uintptr_t)&guard_threshold;profile.guard_get=(uintptr_t)native_guard_state;
    profile.stamina_adjust=(uintptr_t)native_stamina;profile.guard_check=(uintptr_t)native_guard_eligible;
    memset(guard_vtable,0,sizeof guard_vtable);guard_vtable[0xA0/4]=(uintptr_t)native_guard_unit;
    guard_vtable[0x80/4]=(uintptr_t)native_maximum;
    guard_vtable[0x5C/4]=(uintptr_t)native_guard_off;ptr(roles[0],0,guard_vtable);
    *((BYTE*)roles[0]+0x219)=1;write_float(roles[0],profile.stamina_offset,50);
    BYTE *code=VirtualAlloc(NULL,128,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);CHECK(code!=NULL);
    memset(code,0x90,128);
    memcpy(code,"\x56\x57\x89\xCE\xBF\x01\x00\x00\x00\x85\xFF\x74\x6A\x57\x56",15);
    memcpy(code+15,"\x83\xC4\x08\x5F\x5E\x31\xC0\xC3",8);
    memcpy(code+32,"\x5B\x5F\x5E\xB8\x01\x00\x00\x00\xC3",9);
    memcpy(code+119,"\x5F\x5E\x31\xC0\xC3",5);
    make_call(code+64,(uintptr_t)native_stamina);make_call(code+72,(uintptr_t)native_guard_state);
    make_call(code+80,(uintptr_t)native_guard_state);make_call(code+88,(uintptr_t)native_stamina);
    profile.guard_periodic_call=(uintptr_t)(code+64);profile.guard_hit_call=(uintptr_t)(code+72);
    profile.guard_input_release_call=(uintptr_t)(code+80);profile.guard_run_call=(uintptr_t)(code+88);
    profile.dodge_start=(uintptr_t)code;
    profile.dodge_gate=(uintptr_t)(code+9);profile.dodge_legacy=(uintptr_t)(code+15);
    profile.dodge_resume=(uintptr_t)(code+32);profile.dodge_failure=(uintptr_t)(code+119);
    profile.install_state=(uintptr_t)native_dodge_install;
    make_call(code+48,(uintptr_t)native_dash_init);make_call(code+56,(uintptr_t)native_dash_motion);
    profile.dodge_init_call=(uintptr_t)(code+48);profile.dodge_init=(uintptr_t)native_dash_init;
    profile.dodge_motion_call=(uintptr_t)(code+56);profile.dodge_motion=(uintptr_t)native_dash_motion;
    profile.map_bounds=(uintptr_t)native_dash_bounds;profile.cell_passable=(uintptr_t)native_dash_cell;
    profile.grid_commit=(uintptr_t)native_dash_commit;profile.world_to_grid=(uintptr_t)native_dash_grid;
    profile.role_effect=(uintptr_t)native_dash_effect;profile.dodge_counter_offset=expansion ? 0x2EB:0x2DF;
    guard_vtable[8/4]=(uintptr_t)native_dash_position;
    memset(dash_cells,0xFF,sizeof dash_cells);Write32(dash_map,8,128);ptr(dash_map,0x14,dash_cells);
    ptr(roles[0],0x6F,dash_map);dash_wall=false;legacy_motion_calls=legacy_init_calls=0;
    memcpy(code+96,"\x64\xA1\x00\x00\x00\x00",6);
    code[102]=0xE9;int32_t hit_jump=(int32_t)((uintptr_t)native_damage_receiver-(uintptr_t)code-107);
    memcpy(code+103,&hit_jump,4);profile.hit_receiver=(uintptr_t)(code+96);
    profile.runtime_owner=(uintptr_t)native_damage_owner;
    /* 自有小程序复现cmp角度与jle：函数保存ESI，分支分别返回能否格挡。 */
    BYTE *angle_code=VirtualAlloc(NULL,48,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);CHECK(angle_code!=NULL);
    memcpy(angle_code,"\x8B\x44\x24\x04\x56\x89\xCE\x83\xF8\x02\x0F\x8E\x0A\x00\x00\x00",16);
    memcpy(angle_code+16,"\xB8\x01\x00\x00\x00\x5E\xC2\x04\x00",9);
    memcpy(angle_code+26,"\x31\xC0\x5E\xC2\x04\x00",6);
    profile.guard_angle_gate=(uintptr_t)(angle_code+10);
    CHECK(Guard_Initialize());
    unsigned applied=0;PadInput released={0};
    Guard_SyncSettings(&released,&applied);CHECK(applied==RuntimeConfig_Current()->generation);

    /* 真保存128→256，松LT但角色仍闪避：快照和本次位移都继续用128。 */
    start_settings_dash();
    for(unsigned i=0;i<3;++i) ((This0)patched_callee(profile.dodge_motion_call))(roles[0], NULL);
    CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,256));
    Guard_SyncSettings(&released,&applied);
    CHECK(RuntimeConfig_HasPending() && RuntimeConfig_GetInt(CONFIG_DODGE_DISTANCE)==128);
    finish_settings_dash();CHECK(Read32(roles[0],0x2C)==6528);
    Guard_SyncSettings(&released,&applied);
    CHECK(!RuntimeConfig_HasPending() && RuntimeConfig_GetInt(CONFIG_DODGE_DISTANCE)==256);
    CHECK(applied==RuntimeConfig_Current()->generation);
    start_settings_dash();finish_settings_dash();CHECK(Read32(roles[0],0x2C)==6784);

    /* 防御式确认：模拟其它调用者提前提交快照，Guard拒收期间不能吞掉代数。 */
    start_settings_dash();CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,64));
    CHECK(RuntimeConfig_ApplyFrame(1));unsigned previous=applied;
    Guard_SyncSettings(&released,&applied);CHECK(applied==previous && applied!=RuntimeConfig_Current()->generation);
    CHECK(!Guard_ApplySettings());finish_settings_dash();CHECK(Read32(roles[0],0x2C)==7040);
    Guard_SyncSettings(&released,&applied);CHECK(applied==RuntimeConfig_Current()->generation);
    start_settings_dash();finish_settings_dash();CHECK(Read32(roles[0],0x2C)==7104);

    /* 开关也等动作结束；原版闪避状态17同样会阻止中途应用。 */
    start_settings_dash();CHECK(RuntimeConfig_SetInt(CONFIG_DIRECTIONAL_DODGE,0));
    Guard_SyncSettings(&released,&applied);CHECK(RuntimeConfig_GetInt(CONFIG_DIRECTIONAL_DODGE)==1);
    finish_settings_dash();CHECK(Read32(roles[0],0x2C)==7168);
    Guard_SyncSettings(&released,&applied);CHECK(RuntimeConfig_GetInt(CONFIG_DIRECTIONAL_DODGE)==0);
    unsigned legacy_before=legacy_init_calls;
    ((This0)patched_callee(profile.dodge_init_call))(roles[0], NULL);CHECK(legacy_init_calls==legacy_before+1);
    Write32(roles[0],0x73,0x17);CHECK(RuntimeConfig_SetInt(CONFIG_DIRECTIONAL_DODGE,1));
    Guard_SyncSettings(&released,&applied);CHECK(RuntimeConfig_HasPending());
    Write32(roles[0],0x73,1);Guard_SyncSettings(&released,&applied);CHECK(!RuntimeConfig_HasPending());

    /* 中断留下旧dash.active也不应阻塞：真实角色已退出17，再次闪避必须使用96。 */
    start_settings_dash();CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,96));
    Write32(roles[0],0x73,1);Guard_SyncSettings(&released,&applied);
    CHECK(!RuntimeConfig_HasPending() && Guard_ApplySettings());
    unsigned x=Read32(roles[0],0x2C);start_settings_dash();finish_settings_dash();CHECK(Read32(roles[0],0x2C)==x+96);

    /* 新场景/新角色/无玩家都不被旧位移记录永久锁住；不向旧对象补写状态。 */
    start_settings_dash();CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,160));
    BYTE alternate_world[0x100]={0};ptr(alternate_world,0x30,manager_data);world_ptr=alternate_world;
    Write32(roles[0],0x73,1);Guard_SyncSettings(&released,&applied);CHECK(!RuntimeConfig_HasPending());
    x=Read32(roles[0],0x2C);start_settings_dash();finish_settings_dash();CHECK(Read32(roles[0],0x2C)==x+160);
    start_settings_dash();CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,80));
    Write32(manager_data,0x0C,2);Write32(roles[1],0x73,1);
    Guard_SyncSettings(&released,&applied);CHECK(!RuntimeConfig_HasPending() && Guard_ApplySettings());
    Write32(manager_data,0x0C,0);CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,112));
    Guard_SyncSettings(&released,&applied);CHECK(!RuntimeConfig_HasPending() && Guard_ApplySettings());
    Write32(manager_data,0x0C,1);Write32(roles[0],0x73,1);world_ptr=world_data;
    x=Read32(roles[0],0x2C);start_settings_dash();finish_settings_dash();CHECK(Read32(roles[0],0x2C)==x+112);

    /* 常态快照未变化时仍快速返回，确认序号不重复推进。 */
    previous=applied;Guard_SyncSettings(&released,&applied);CHECK(applied==previous);
    Guard_Shutdown();CHECK(VirtualFree(angle_code,0,MEM_RELEASE));CHECK(VirtualFree(code,0,MEM_RELEASE));
    Game_Release();
    /* 真实保存、应用和RT输入，Actor同偏移故意不是存档角色编号。 */
    configure(&profile,expansion);record_actions=true;g_intent.layer=LAYER_SKILL;
    Write32(configuration_player,0x348,4);Write32(roles[0],0x348,30);
    ConfigBindingEdit edits[2]={{expansion ? 2u:1u,4,1,{1,111,0}},{expansion ? 2u:1u,4,3,{1,222,0}}};
    CHECK(RuntimeConfig_SaveBatch(NULL,0,edits,2));RuntimeConfig_ApplyFrame(0);
    g_intent.held=g_intent.pressed=KEY(PAD_A);Game_Update();CHECK(releases==1 && arg1==1001 && last_right_style);
    ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();Combat_Reset();
    g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();CHECK(releases==2 && arg1==1002 && last_right_style);
    ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();Combat_Reset();
    edits[0].value=(ConfigBinding){0,0,0};edits[1].value=(ConfigBinding){0,0,0};
    CHECK(RuntimeConfig_SaveBatch(NULL,0,edits,2));RuntimeConfig_ApplyFrame(0);
    g_intent.held=g_intent.pressed=KEY(PAD_A);Game_Update();CHECK(releases==2);
    g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();CHECK(releases==2);
    Combat_Reset();Game_Release();record_actions=false;CHECK(DeleteFileW(path));
    printf("%s真实TOML保存、动作安全点、Guard确认及中断/切换集成回归通过\n",expansion?"外传":"本体");
}
int main(void)
{
    settings_regression(false);settings_regression(true);
    printf("手柄设置应用集成检查通过：%u项\n",checks);return 0;
}
