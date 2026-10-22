/* 复用生产输入、解析、战斗与闪避接口，不启动游戏；真实资源窗口用宿主编号映射。 */
#define selected fixture_selected
#define main baseline_game_main
#include "test_game.c"
#undef main
#undef selected
#include "../../src/Modules/Controller/Combat.c"

static unsigned start_attempts;
static int __fastcall session_submit(void *self,void *edx,int op,int method,int b,int c)
{
    if(op==9 || op==10 || op==11) {
        if(method==1001)++start_attempts;
        /* 原分发器允许起手计时续段结束旧实例；普通首段仍遵守忙碌资格。 */
        if(method==1002 && ReadPtr(roles[0],g_profile->active_offset)) {
            ptr(roles[0],g_profile->active_offset,NULL);Combat_End();
        }
    }
    return native_submit(self,edx,op,method,b,c);
}
static void setup_session(Profile *profile,bool expansion)
{
    configure(profile,expansion);test_target_setting=1;record_actions=true;start_attempts=0;
    profile->submit=(uintptr_t)session_submit;
    /* 疾电/奔雷式的共享选择码：首段1001映射起手，1002映射确认；222是另一招。 */
    group_members[0][0]=1001;group_members[0][1]=1002;Write32(groups_data[0],0x26,2);
    group_members[1][0]=1003;
    Write32(methods_data[0],0x29,0);Write32(methods_data[1],0x27,111);
    Write32(methods_data[1],0x29,1015);Write32(methods_data[1],0x2D,1050);
    Write32(methods_data[2],0x27,222);Write32(methods_data[2],0x29,0);
    Write32(hud_data,0x12C,111);test_offset_setting=1;test_offset_grace=250;
}
static void press_skill(bool quick)
{
    g_intent.layer=quick ? LAYER_SKILL:LAYER_GAME;
    g_intent.held=g_intent.pressed=quick ? KEY(PAD_A):KEY(PAD_Y);
    if(quick)Combat_Request(111,ACTION_SKILL,false);
    Combat_Update(roles[0]);
}
static void confirmation(bool expansion)
{
    for(unsigned route=0;route<4;++route) {
        Profile profile;setup_session(&profile,expansion);press_skill((route&1)!=0);
        CHECK(releases==1 && combat.history.count==1);
        engine_tick+=16;press_skill((route&2)!=0);
        CHECK(releases==2 && arg1==1002 && combat.history.count==2);
        Combat_Reset();Game_Release();
    }
    Profile profile;setup_session(&profile,expansion);press_skill(true);
    engine_tick+=14;press_skill(true);
    CHECK(start_attempts==1); /* 尚未开放不能误回首段，即使原生随后会拒绝也不能提交。 */
    g_intent.held=g_intent.pressed=0;++engine_tick;Combat_Update(roles[0]);CHECK(releases==1);
    ++engine_tick;Combat_Update(roles[0]);CHECK(releases==2 && arg1==1002);
    Combat_Reset();Game_Release();
}
static void frozen_phase(bool expansion)
{
    Profile profile;setup_session(&profile,expansion);press_skill(true);engine_tick+=8;
    /* 长期Y是普通技能而非预设，Offset也必须保护已建立的前置。 */
    ptr(roles[0],profile.active_offset,NULL);Combat_End();
    Write32(roles[0],0x73,0x17);g_intent.layer=LAYER_GUARD;g_intent.held=g_intent.pressed=0;
    Combat_DodgeStarted(roles[0]);engine_tick+=200;g_input.now+=200;Combat_Update(roles[0]);
    CHECK(engine_tick-combat.history.start_tick==8);
    Write32(roles[0],0x73,1);Combat_Update(roles[0]);press_skill(false);
    CHECK(releases==1); /* 闪避耗时不能被算成蓄力，仍须等待原下界。 */
    engine_tick+=8;g_input.now+=16;press_skill(true);
    CHECK(releases==2 && arg1==1002);
    Combat_Reset();Game_Release();
}
static void snapshot_and_deadline(bool expansion)
{
    Profile profile;setup_session(&profile,expansion);press_skill(true);engine_tick+=45;
    Combat_PrepareDodge(roles[0]);
    /* 原闪避入口同步结束旧Runtime，保存的是逻辑阶段，不恢复旧实例指针。 */
    ptr(roles[0],profile.active_offset,NULL);Combat_End();Write32(roles[0],0x73,0x17);
    g_intent.layer=LAYER_GUARD;g_intent.held=g_intent.pressed=0;Combat_DodgeStarted(roles[0]);Combat_FinishDodgeAttempt();
    engine_tick+=100;g_input.now+=100;Combat_Update(roles[0]);CHECK(engine_tick-combat.history.start_tick==45);
    Write32(roles[0],0x73,1);Combat_Update(roles[0]);
    CHECK(!ReadPtr(roles[0],profile.active_offset) && combat.history.last_method==1001);
    /* 宽限期间可以越过原上界，但下界照常增长；到期不能继续借用旧前置。 */
    engine_tick+=10;g_input.now+=100;g_intent.layer=LAYER_SKILL;Combat_Update(roles[0]);
    ResolvedSkill next;WorldPoint aim={6464,6400};
    CHECK(Skill_Query(roles[0],111,&aim,&combat.history,&next)==SKILL_READY && next.method==1002);
    g_input.now+=150;Combat_Update(roles[0]);
    CHECK(!combat.history.grace && Skill_Query(roles[0],111,&aim,&combat.history,&next)==SKILL_EXPIRED);
    press_skill(true);CHECK(arg1==1001 && combat.history.count==1); /* 正常新起手，不把历史追加成两次旧前置 */
    Combat_Reset();Game_Release();

    setup_session(&profile,expansion);press_skill(true);engine_tick+=8;
    Combat_PrepareDodge(roles[0]);Combat_FinishDodgeAttempt(); /* 未成功闪避 */
    engine_tick+=100;g_intent.layer=LAYER_SKILL;g_intent.held=g_intent.pressed=0;Combat_Update(roles[0]);
    CHECK(!combat.offset_active && engine_tick-combat.history.start_tick==108);
    Combat_Reset();Game_Release();

    setup_session(&profile,expansion);press_skill(true);engine_tick+=20;
    ptr(roles[0],profile.active_offset,NULL);Combat_End();
    g_intent.layer=LAYER_GAME;g_intent.held=KEY(PAD_Y);g_intent.pressed=0;Combat_Update(roles[0]);
    CHECK(releases==1 && !combat.pending); /* 长按不能自动确认 */
    Combat_Interrupted();CHECK(!combat.history.count && !combat.pending && !combat.offset_active);
    Combat_Reset();Game_Release();
}
static void generic_phases(bool expansion)
{
    Profile profile;setup_session(&profile,expansion);
    /* 霹雳式较短下界、收招后续击、老英雄式自动阶段，均按记录判断而不特判名称。 */
    Write32(methods_data[1],0x29,1005);press_skill(false);engine_tick+=6;press_skill(true);
    CHECK(releases==2 && arg1==1002);Combat_Reset();Game_Release();
    setup_session(&profile,expansion);Write32(methods_data[1],0x29,12);press_skill(true);engine_tick+=2;
    press_skill(false);CHECK(releases==1 && start_attempts==1);
    ptr(roles[0],profile.active_offset,NULL);Combat_End();++engine_tick;g_intent.pressed=0;Combat_Update(roles[0]);
    CHECK(releases==2 && arg1==1002);Combat_Reset();Game_Release();
    setup_session(&profile,expansion);Write32(methods_data[1],0x29,0);press_skill(true);engine_tick+=20;
    press_skill(true);CHECK(releases==1 && start_attempts==1);Combat_Reset();Game_Release();
    /* 不同技能成功后建立自己的起手历史，失败尝试不能提前删除旧技能会话。 */
    setup_session(&profile,expansion);Write32(methods_data[2],0x29,12);press_skill(true);Combat_Request(222,ACTION_SKILL,false);Combat_Update(roles[0]);
    CHECK(combat.history.count==1 && combat.history.last_method==1001);
    ptr(roles[0],profile.active_offset,NULL);Combat_End();g_intent.held=g_intent.pressed=0;Combat_Update(roles[0]);
    CHECK(arg1==1003 && combat.history.count==1 && combat.history.last_method==1003);
    Combat_Reset();Game_Release();
}
static void grace_boundaries(bool expansion)
{
    for(unsigned mode=0;mode<3;++mode) {
        Profile profile;setup_session(&profile,expansion);
        test_offset_grace=mode==0 ? 0:mode==1 ? 250:500;
        press_skill(true);engine_tick+=45;ptr(roles[0],profile.active_offset,NULL);Combat_End();
        Write32(roles[0],0x73,0x17);g_intent.layer=LAYER_GUARD;g_intent.held=g_intent.pressed=0;
        Combat_DodgeStarted(roles[0]);engine_tick+=200;g_input.now=0xFFFFFFF0u;Combat_Update(roles[0]);
        Write32(roles[0],0x73,1);Combat_Update(roles[0]);engine_tick+=10;
        ResolvedSkill next;WorldPoint point={6464,6400};
        if(test_offset_grace) {
            g_input.now+=(unsigned)test_offset_grace-1;Combat_Update(roles[0]);
            CHECK(Skill_Query(roles[0],111,&point,&combat.history,&next)==SKILL_READY);
            ++g_input.now;Combat_Update(roles[0]);
        } else Combat_Update(roles[0]);
        CHECK(!combat.history.grace && Skill_Query(roles[0],111,&point,&combat.history,&next)==SKILL_EXPIRED);
        Combat_Reset();Game_Release();
    }
    Profile profile;setup_session(&profile,expansion);press_skill(true);engine_tick+=20;
    Write32(methods_data[1],0x31,2);WorldPoint forward={6464,6400},back={6336,6400};ResolvedSkill next;
    CHECK(Skill_Query(roles[0],111,&forward,&combat.history,&next)==SKILL_DIRECTION);
    CHECK(Skill_Query(roles[0],111,&back,&combat.history,&next)==SKILL_READY);
    Combat_Reset();Game_Release();

    setup_session(&profile,expansion);press_skill(true);engine_tick+=20;
    ptr(roles[0],profile.active_offset,NULL);Combat_End();Write32(roles[0],0x73,0x17);
    Combat_DodgeStarted(roles[0]);engine_tick+=100;g_input.now+=100;Combat_Update(roles[0]);
    Write32(roles[0],0x73,1);Combat_Update(roles[0]);CHECK(combat.history.grace);
    Combat_SelectCombo(0);CHECK(!combat.history.grace && !combat.history.count && !combat.offset_active);
    Combat_Reset();Game_Release();
}
int main(void)
{
    confirmation(false);confirmation(true);frozen_phase(false);frozen_phase(true);
    snapshot_and_deadline(false);snapshot_and_deadline(true);generic_phases(false);generic_phases(true);
    grace_boundaries(false);grace_boundaries(true);
    printf("技能同义确认、早按缓冲及闪避阶段冻结回归通过：%u项\n",checks);return 0;
}
