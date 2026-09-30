#include "Plugin.h"
#include "Combat.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* 在真正的 32 位进程中，让游戏适配层调用同约定的替身函数。
   这验证 this、参数值、功能路由及两个 Profile 偏移；不冒充真实游戏测试。 */
const Profile *g_profile;
Intent g_intent;
PadInput g_input;
WCHAR g_directory[MAX_PATH];
HWND g_window;
static BYTE world_data[0x100], manager_data[0x300], mouse_data[0x100], ui_data[0x100];
static BYTE hud_data[0xD00], scene_data[0x100], table_data[6*16];
static BYTE roles[3][0x500], record_data[0x40], binding_data[0x20], page_data[0xC0];
static void *world_ptr=world_data,*mouse_ptr=mouse_data,*scene_ptr=scene_data,*table_ptr=table_data,*hud_ptr=hud_data;
static int last_opcode, arg1, arg2, arg3, releases, last_policy, selected, quick_slot;
static void *last_target;
static bool page_visible, physical_mouse, busy_gate;
static unsigned checks;
static unsigned move_requests;
static bool reject_move;
static BYTE choices_data[32],groups_data[2][0x30],methods_data[3][0x90];
static uint32_t group_members[2][2],engine_tick,history_timeout;
static int group_table,method_table;
static bool unavailable_skill,record_actions;
static bool reject_busy;
static bool emulate_combo;
static bool reject_qualification;
static WorldPoint last_aim;
static BYTE runtime_data[0x100];
#define CHECK(e) do { ++checks; if (!(e)) { fprintf(stderr,"适配检查失败，行 %d：%s\n",__LINE__,#e); exit(1); } } while(0)

void Log_Write(const char *format, ...) { (void)format; }
bool Input_PhysicalDown(int key) { return key==VK_LBUTTON && physical_mouse; }
int Config_Number(const WCHAR *section,const WCHAR *key,int fallback,int minimum,int maximum)
{ (void)section;(void)key;(void)minimum;(void)maximum;return fallback; }
bool Memory_Readable(const void *p,size_t bytes)
{
    MEMORY_BASIC_INFORMATION info;
    if (!p || !VirtualQuery(p,&info,sizeof info) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
    return (uintptr_t)p+bytes <= (uintptr_t)info.BaseAddress+info.RegionSize;
}
uint32_t Read32(const void *p,unsigned offset)
{ uint32_t v=0;if(p && Memory_Readable((BYTE *)p+offset,4))memcpy(&v,(BYTE *)p+offset,4);return v; }
void *ReadPtr(const void *p,unsigned offset) { return (void *)(uintptr_t)Read32(p,offset); }
void Write32(void *p,unsigned offset,uint32_t value) { memcpy((BYTE *)p+offset,&value,4); }
static void ptr(void *base,unsigned offset,void *value) { Write32(base,offset,(uint32_t)(uintptr_t)value); }

static int __attribute__((thiscall)) native_submit(void *self,int opcode,int a,int b,int c)
{
    CHECK(self==manager_data);
    last_opcode=opcode; arg1=a;arg2=b;arg3=c;
    if (opcode==18) Write32(roles[0],0x143,(uint32_t)a);
    if (opcode==9 || opcode==10 || opcode==11) {
        CHECK(a==1001 || a==1002 || a==1003);
        if (reject_busy && ReadPtr(roles[0],g_profile->active_offset)) return 0;
        if (opcode==11 && !record_actions) {
            unsigned offset=g_profile->pending_offset;
            Write32(roles[0],offset,(uint32_t)opcode);Write32(roles[0],offset+4,(uint32_t)a);
            Write32(roles[0],offset+8,(uint32_t)b);Write32(roles[0],offset+12,(uint32_t)c);
        }
        ++releases;last_policy=opcode==11 ? 0:1;
        last_target=opcode==10 ? NULL:Game_Resolve((uint32_t)b);
        if (opcode==10) {last_aim.x=b;last_aim.y=c;}
        if (record_actions) {
            Combat_Record((int)(Read32(methods_data[a-1001],0x27)&0xFFFF),0);
            ptr(roles[0],g_profile->active_offset,runtime_data);
        }
    }
    if (opcode==1 || opcode==2) {
        ++move_requests;
        if (!reject_move) Write32(roles[0],0x73,0x0B);
        Write32(mouse_data,0x90,a);Write32(mouse_data,0x94,b);
    }
    return 1;
}
static int __attribute__((thiscall)) native_stop(void *self,int state,int a,int b,int c)
{ CHECK(self==roles[0] && state==1 && a==0 && b==0 && c==0);Write32(self,0x73,1);return 1; }
static int __attribute__((thiscall)) native_release(void *self,int method,int policy,void *target)
{ (void)self;(void)method;(void)policy;(void)target;CHECK(0 && "禁止回到鼠标技能入口");return 0; }
static int __attribute__((thiscall)) native_left(void *self) { CHECK(self==hud_data);return 111; }
static int __attribute__((thiscall)) native_right(void *self)
{
    CHECK(self==hud_data);
    if (emulate_combo) {
        static const int slots[]={111,222,111};
        int index=(int)Read32(hud_data,0x120)+1;
        if (index>=3) {Write32(hud_data,0x120,(uint32_t)-1);index=0;}
        CHECK(index>=0 && index<3);return slots[index];
    }
    return 222;
}
static int __attribute__((thiscall)) native_select(void *self,int value) { CHECK(self==hud_data);selected=value;return 1; }
static int __attribute__((thiscall)) native_quick(void *self,int value) { CHECK(self==hud_data);quick_slot=value;return 1; }
static int __attribute__((thiscall)) native_inspect_gate(void *self,int handle) { CHECK(self==manager_data && handle==3);return busy_gate; }
static int __attribute__((thiscall)) native_hover(void *self,int handle) { CHECK(self==manager_data && handle==3);return 1; }
static int __cdecl native_relation(void *a,void *b) { CHECK(a==roles[0] && b==roles[1]);return 1; }
static int __attribute__((thiscall)) native_template(void *self,int a,int b) { CHECK(self==record_data && a==0x1F && b==1);return 0xFFFF; }
static int __attribute__((thiscall)) native_jm(void *self,int id)
{ CHECK(self==ui_data);return page_visible && id==0x94 ? (int)(uintptr_t)page_data:0; }
static int __attribute__((thiscall)) native_property(void *self,int index)
{
    if (self==choices_data) { CHECK(index>=1 && index<=3);return index==1 ? 2:index==2 ? 111:222; }
    CHECK(self==record_data && index==0x0D);return page_visible ? 1:0;
}
static int __attribute__((thiscall)) native_lookup(void *table,int id)
{
    if (table==&group_table && (id==111 || id==222)) return (int)(uintptr_t)groups_data[id==111 ? 0:1];
    if (table==&method_table && id>=1001 && id<=1003) return (int)(uintptr_t)methods_data[id-1001];
    return 0;
}
static int __attribute__((thiscall)) native_eligibility(void *self,int id)
{ CHECK(self==roles[0] && (id==111 || id==222));return unavailable_skill ? -1:0; }
static int __attribute__((thiscall)) native_role_method(void *self,int method)
{ CHECK(self==roles[0]);return native_lookup(&method_table,method); }
static int __attribute__((thiscall)) native_usable(void *self,int record)
{ CHECK(self==roles[0] && record!=0);return !reject_qualification; }
static int __attribute__((thiscall)) native_distance(void *self,int method)
{ CHECK(self==roles[0] && method>=1001 && method<=1003);return 64; }
static int __attribute__((thiscall)) native_aim(void *self,int point,int origin)
{ CHECK(self==roles[0] && origin!=0);last_aim=*(WorldPoint *)(uintptr_t)point;return 1; }
static int __cdecl native_dir8(const WorldPoint *point,const WorldPoint *origin)
{
    float angle=atan2f((float)(point->y-origin->y),(float)(point->x-origin->x));
    int result=(int)lroundf(angle*4/3.14159265358979323846f);
    return (result+8)%8;
}
static int __attribute__((thiscall)) native_facing(void *self,int point,int origin)
{ CHECK(self==roles[0]);return native_dir8((WorldPoint *)(uintptr_t)point,(WorldPoint *)(uintptr_t)origin); }

static void configure(Profile *profile, bool expansion)
{
    Combat_Reset();
    memset(profile,0,sizeof *profile);
    profile->world_global=(uintptr_t)&world_ptr;profile->mouse_global=(uintptr_t)&mouse_ptr;
    profile->entities_global=(uintptr_t)&scene_ptr;profile->handles_global=(uintptr_t)&table_ptr;
    profile->skill_global=(uintptr_t)&hud_ptr;profile->ui=(uintptr_t)ui_data;
    profile->submit=(uintptr_t)native_submit;profile->install_state=(uintptr_t)native_stop;
    profile->skill_release=(uintptr_t)native_release;profile->left_get=(uintptr_t)native_left;
    profile->right_get=(uintptr_t)native_right;profile->left_set=profile->right_set=(uintptr_t)native_select;
    profile->quick_use=(uintptr_t)native_quick;profile->inspect_gate=(uintptr_t)native_inspect_gate;
    profile->hover_set=(uintptr_t)native_hover;
    profile->relation=(uintptr_t)native_relation;profile->template_value=(uintptr_t)native_template;
    profile->get_jm=(uintptr_t)native_jm;
    profile->ui_property=(uintptr_t)native_property;
    profile->skill_groups=(uintptr_t)&group_table;profile->methods=(uintptr_t)&method_table;
    profile->game_tick=(uintptr_t)&engine_tick;profile->combo_timeout=(uintptr_t)&history_timeout;
    profile->lookup=(uintptr_t)native_lookup;profile->skill_eligibility=(uintptr_t)native_eligibility;
    profile->role_method=(uintptr_t)native_role_method;profile->method_usable=(uintptr_t)native_usable;
    profile->method_distance=(uintptr_t)native_distance;profile->facing_point=(uintptr_t)native_aim;
    profile->facing_direction=(uintptr_t)native_facing;profile->direction8=(uintptr_t)native_dir8;
    profile->invalid_offset=expansion?0x446:0x43A;profile->interact_offset=expansion?0x32D:0x321;
    profile->active_offset=expansion?0x359:0x34D;
    profile->pending_offset=expansion?0x456:0x446;
    g_profile=profile;
    memset(roles,0,sizeof roles);memset(table_data,0,sizeof table_data);
    memset(ui_data,0,sizeof ui_data);memset(mouse_data,0,sizeof mouse_data);
    ptr(world_data,0x30,manager_data);Write32(world_data,0x58,1);
    Write32(manager_data,0x0C,1);
    ptr(ui_data,0x18,page_data);ptr(ui_data,0x1C,page_data);ptr(page_data,0x50,record_data);
    ptr(mouse_data,0x38,roles[0]);ptr(scene_data,0x1C,roles[0]);
    for(unsigned i=0;i<3;++i) {
        Write32(roles[i],0x14,i+1);Write32(roles[i],0x67,0x28);
        Write32(roles[i],0x2C,6400+i*64);Write32(roles[i],0x30,6400);
        Write32(roles[i],0x1A7,i);ptr(roles[i],0x18B,record_data);
        ptr(table_data,(i+1)*6+2,roles[i]);
        ptr(roles[i],8,i<2 ? roles[i+1]:NULL);
    }
    Write32(roles[2],profile->interact_offset,1);
    /* 受控角色按原版 typed resolver 的范围验证，不能把所有角色都强制等同普通 Actor 0x28。 */
    Write32(roles[0],0x67,0x3C);
    Write32(roles[0],0x73,1);ptr(roles[0],0x193,choices_data);
    memset(groups_data,0,sizeof groups_data);memset(methods_data,0,sizeof methods_data);
    for (int i=0;i<2;++i) {
        group_members[i][0]=1001+i;Write32(groups_data[i],0x26,1);ptr(groups_data[i],0x2A,group_members[i]);
        Write32(methods_data[i],0x24,1001+i);Write32(methods_data[i],0x27,i==0 ? 111:222);
        Write32(methods_data[i],0x29,500);
    }
    Write32(methods_data[2],0x24,1003);Write32(methods_data[2],0x27,111);Write32(methods_data[2],0x29,500);
    engine_tick=1000;history_timeout=500;unavailable_skill=record_actions=emulate_combo=false;reject_busy=true;
    reject_qualification=false;
    Write32(page_data,0x64,1);
    memset(&g_input,0,sizeof g_input);g_input.connected=g_input.focused=true;g_input.now=1000;
    memset(&g_intent,0,sizeof g_intent);g_intent.layer=LAYER_GAME;
    page_visible=physical_mouse=busy_gate=false;releases=0;last_opcode=0;
    move_requests=0;reject_move=false;
}

static void exercise(bool expansion)
{
    Profile profile;configure(&profile,expansion);
    CHECK(!Game_Menu());
    /* 顶层路由可能保留旧鼠标命中；不应把过时的 +0x40 当作全局菜单门。 */
    ptr(ui_data,0x40,page_data);CHECK(!Game_Menu());ptr(ui_data,0x40,NULL);
    /* 玩家鼠标缓存为空时，权威管理器句柄仍能驱动 A 调查。 */
    ptr(mouse_data,0x38,NULL);
    g_intent.pressed=KEY(PAD_A);Game_Update();
    CHECK(last_opcode==19 && arg1==3 && !arg2 && !arg3 && releases==0);
    CHECK(!Game_Menu());
    BYTE keys[256]={0};g_intent.layer=LAYER_MENU;g_intent.pressed=KEY(PAD_A);Game_Keyboard(keys);
    CHECK(keys[VK_RETURN]==0);g_intent.layer=LAYER_GAME;
    /* 加载过渡时没有玩家，应阻止原生动作，但不能伪装成菜单并把 A 变成 Enter。 */
    Write32(manager_data,0x0C,0);last_opcode=0;CHECK(!Game_Menu());Game_Update();
    CHECK(last_opcode==0 && releases==0);Write32(manager_data,0x0C,1);
    busy_gate=true;last_opcode=0;Game_Update();CHECK(last_opcode==0 && releases==0);busy_gate=false;
    g_intent.pressed=0;g_intent.held=KEY(PAD_X);Game_Update();
    CHECK(releases==1 && last_policy==0 && last_target==roles[1]);
    CHECK(ReadPtr(mouse_data,0x38)==NULL);
    CHECK(ReadPtr(mouse_data,0x3C)==NULL && Read32(mouse_data,0x2C)==0);
    g_intent.held=KEY(PAD_Y);g_input.now+=200;Game_Update();
    CHECK(releases==2 && last_policy==1);
    g_intent.held=KEY(PAD_B);g_input.now+=200;Game_Update();CHECK(releases==2);
    g_intent.held=KEY(PAD_X);page_visible=true;Game_Update();CHECK(releases==2);page_visible=false;
    ptr(ui_data,0x3C,page_data);Game_Update();CHECK(releases==2);ptr(ui_data,0x3C,NULL);
    g_intent.held=0;g_intent.lx=1;Game_Update();
    CHECK(Read32(mouse_data,0x90)>100 && Read32(mouse_data,0x94)<100);
    CHECK(last_opcode==3 && arg1==0);
    unsigned sent=move_requests;Game_Update();CHECK(move_requests==sent);
    int old_goal_x=(int)Read32(mouse_data,0x90),old_goal_y=(int)Read32(mouse_data,0x94);
    g_intent.run=true;Game_Update();CHECK(last_opcode==3 && arg1==1 && move_requests==sent+1);
    CHECK((int)Read32(mouse_data,0x90)==old_goal_x && (int)Read32(mouse_data,0x94)==old_goal_y);
    g_intent.lx=-1;Game_Update();CHECK(move_requests==sent+2 && Read32(mouse_data,0x90)<100);
    g_intent.lx=0;Game_Update();CHECK(Read32(roles[0],0x73)==1);
    /* 原版拒绝移动时，同一目标只限频重试；松杆再推必须立即产生新请求。 */
    reject_move=true;g_intent.lx=1;Game_Update();sent=move_requests;
    g_input.now+=16;Game_Update();CHECK(move_requests==sent);
    g_input.now+=120;Game_Update();CHECK(move_requests==sent+1);
    g_intent.lx=0;Game_Update();g_intent.lx=1;Game_Update();CHECK(move_requests==sent+2);
    g_intent.lx=0;Game_Update();reject_move=false;
    g_intent.layer=LAYER_MEDICINE;g_intent.pressed=KEY(PAD_X);g_intent.held=KEY(PAD_X);Game_Update();
    CHECK(quick_slot==2 && releases==2);
    g_intent.layer=LAYER_ITEM;Game_Update();CHECK(quick_slot==8 && releases==2);
    g_intent.layer=LAYER_SKILL;g_intent.pressed=0;g_intent.rx=1;Game_Update();CHECK(selected==-2);
    ptr(hud_data,0xC18,binding_data);Write32(binding_data,0x14,456);Write32(binding_data,0x18,'Q');
    g_intent.rx=0;g_intent.pressed=KEY(PAD_A);Game_Update();CHECK(selected==456 && releases==2);
    g_intent.layer=LAYER_GUARD;g_intent.pressed=0;Game_Update();
    CHECK(Game_Async(VK_MENU,0)&0x8000);CHECK(!Game_Async(VK_SHIFT,0));
    g_input.focused=false;CHECK(!Game_Async(VK_MENU,0));Game_Update();
    CHECK(last_opcode==16 && arg1==0);
    Game_Release();
    printf("%s适配回放通过\n",expansion?"外传":"本体");
}

static void combat_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);
    /* 无敌人时，真实鼠标无论放在哪里、悬停何物，都不能改变角色 Facing 后备方向。 */
    Write32(roles[1],profile.interact_offset,1);
    for (int sample=0;sample<4;++sample) {
        Combat_Reset();g_intent.held=KEY(PAD_X);g_intent.pressed=KEY(PAD_X);g_input.now+=200;
        for (int offset=0x14;offset<=0x30;offset+=4) Write32(mouse_data,offset,(uint32_t)(sample*1000-5000+offset));
        ptr(mouse_data,0x3C,roles[2]);ptr(mouse_data,0x40,roles[2]);
        BYTE before[sizeof mouse_data];memcpy(before,mouse_data,sizeof before);
        Game_Update();
        CHECK(last_opcode==10 && arg1==1001 && last_aim.x==6528 && last_aim.y==6400);
        CHECK(!memcmp(before,mouse_data,sizeof before));
    }
    /* 最严格的隔离：插件连鼠标对象指针都拿不到时，原生坐标动作仍能建立请求。 */
    mouse_ptr=NULL;Combat_Reset();g_input.now+=200;Game_Update();
    CHECK(last_opcode==10 && last_aim.x==6528 && last_aim.y==6400);mouse_ptr=mouse_data;
    Combat_Reset();g_intent.lx=-1;g_input.now+=200;Game_Update();
    CHECK(last_opcode==10 && last_aim.x<6400 && last_aim.y>6400);
    CHECK(Game_Async(VK_LBUTTON,(SHORT)0x8000)==0 && Game_Async(VK_RBUTTON,(SHORT)0x8000)==0);
    CHECK(!Combat_AllowsMouseRetry());
    g_intent.layer=LAYER_MENU;CHECK(!Combat_AllowsMouseRetry());
    CHECK(Game_Async(VK_LBUTTON,(SHORT)0x8000)==0);
    g_intent.layer=LAYER_MOUSE;CHECK(Game_Async(VK_RBUTTON,(SHORT)0x8000)==(SHORT)0x8000);
    CHECK(Combat_AllowsMouseRetry());

    configure(&profile,expansion);
    g_intent.held=KEY(PAD_X);g_intent.pressed=KEY(PAD_X);record_actions=true;Game_Update();
    CHECK(Combat_Target()==2 && last_target==roles[1]);
    Game_Release();CHECK(Combat_Target()==2);
    g_intent.layer=LAYER_SKILL;g_intent.held=g_intent.pressed=0;
    Combat_Update(roles[0],0);CHECK(Combat_Target()==2);
    g_intent.layer=LAYER_GAME;g_intent.held=KEY(PAD_Y);g_intent.pressed=KEY(PAD_Y);
    ptr(roles[0],profile.active_offset,NULL);g_input.now+=200;engine_tick+=10;Game_Update();
    CHECK(last_target==roles[1] && last_opcode==9);

    /* 解析重试自持上下文，不使用鼠标 +0x6C/+0x70/+0x74，也不在松键后无限重试。 */
    configure(&profile,expansion);unavailable_skill=true;
    g_intent.held=KEY(PAD_X);g_intent.pressed=KEY(PAD_X);Game_Update();CHECK(releases==0);
    g_intent.held=g_intent.pressed=0;ptr(mouse_data,0x3C,roles[2]);Write32(mouse_data,0x74,99);
    Game_Update();CHECK(releases==0);
    unavailable_skill=false;Game_Update();CHECK(releases==1 && last_target==roles[1]);
    CHECK(Read32(mouse_data,0x74)==99);
    configure(&profile,expansion);unavailable_skill=true;
    g_intent.held=KEY(PAD_X);g_intent.pressed=KEY(PAD_X);Game_Update();
    g_intent.held=g_intent.pressed=0;
    for (int i=0;i<5;++i) Game_Update();
    unavailable_skill=false;Game_Update();CHECK(releases==0);
    configure(&profile,expansion);ptr(roles[0],profile.active_offset,runtime_data);
    g_intent.held=KEY(PAD_Y);g_intent.pressed=KEY(PAD_Y);Game_Update();CHECK(releases==0);
    g_intent.held=g_intent.pressed=0;ptr(roles[0],profile.active_offset,NULL);Game_Update();
    CHECK(releases==1 && last_target==roles[1]);
    configure(&profile,expansion);ptr(roles[0],profile.active_offset,runtime_data);
    g_intent.held=KEY(PAD_Y);g_intent.pressed=KEY(PAD_Y);Game_Update();
    g_intent.held=g_intent.pressed=0;
    for (int i=0;i<6;++i) Game_Update();
    ptr(roles[0],profile.active_offset,NULL);Game_Update();CHECK(releases==0);

    /* 控制器自己的历史参与 Method 解析，覆盖续接成员、方向条件及两个时间边界。 */
    configure(&profile,expansion);
    Write32(groups_data[0],0x26,2);group_members[0][1]=1003;
    ActionHistory history={0};history.count=1;history.selectors[0]=111;
    history.start_tick=990;history.end_tick=950;history.ended=true;history.direction=0;
    WorldPoint forward={6528,6400},backward={6272,6400};ResolvedSkill resolved;
    CHECK(Skill_Resolve(roles[0],111,&forward,&history,&resolved) && resolved.method==1003 && resolved.sequence);
    methods_data[2][0x31]=2;
    CHECK(!Skill_Resolve(roles[0],111,&forward,&history,&resolved));
    CHECK(Skill_Resolve(roles[0],111,&backward,&history,&resolved) && resolved.method==1003);
    methods_data[2][0x31]=0;Write32(methods_data[2],0x29,1005);Write32(methods_data[2],0x2D,1015);
    CHECK(Skill_Resolve(roles[0],111,&forward,&history,&resolved));
    history.start_tick=995;CHECK(!Skill_Resolve(roles[0],111,&forward,&history,&resolved));
    history.start_tick=985;CHECK(!Skill_Resolve(roles[0],111,&forward,&history,&resolved));
    CHECK(Skill_Resolve(roles[0],10001,&forward,&history,&resolved) && resolved.method==10001);
    ptr(roles[0],0x193,NULL);CHECK(!Skill_Resolve(roles[0],10001,&forward,&history,&resolved));
    ptr(roles[0],0x193,choices_data);

    configure(&profile,expansion);Write32(hud_data,0x12C,(uint32_t)-1);Write32(hud_data,0x120,0);
    record_actions=true;g_intent.held=KEY(PAD_Y);g_intent.pressed=KEY(PAD_Y);Game_Update();
    CHECK(Read32(hud_data,0x120)==1);
    Combat_Reset();Combat_Record(222,0);CHECK(Read32(hud_data,0x120)==1);
    ptr(roles[0],profile.active_offset,NULL);g_intent.held=KEY(PAD_X);g_intent.pressed=KEY(PAD_X);
    g_input.now+=200;Game_Update();CHECK(Read32(hud_data,0x120)==1);
    Combat_Reset();record_actions=false;Game_Release();
    printf("%s鼠标无关性、重试、序列条件与动作历史回归通过\n",expansion?"外传":"本体");
}

static void chain_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);
    record_actions=true;
    /* 模拟真实首招：时间条件为 0，只能在空历史下起手。这正是 dev4 测试未覆盖的差异。 */
    Write32(methods_data[0],0x29,0);Write32(methods_data[1],0x29,0);
    g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();CHECK(releases==1 && arg1==1001);
    g_intent.held=KEY(PAD_X)|KEY(PAD_Y);g_intent.pressed=KEY(PAD_Y);Game_Update();CHECK(releases==1);
    ptr(roles[0],profile.active_offset,NULL);engine_tick+=1;Combat_End();
    g_intent.held=KEY(PAD_Y);g_intent.pressed=0;Game_Update();CHECK(releases==1);
    Game_Update();CHECK(releases==2 && arg1==1002 && last_policy==1);

    /* 旧 X 请求等待时新 Y 优先，反向切换也按新边沿决定来源，而不是永远让“Y 仍按住”获胜。 */
    configure(&profile,expansion);ptr(roles[0],profile.active_offset,runtime_data);
    g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();
    g_intent.held=KEY(PAD_X)|KEY(PAD_Y);g_intent.pressed=KEY(PAD_Y);Game_Update();
    g_intent.held=KEY(PAD_Y);g_intent.pressed=0;ptr(roles[0],profile.active_offset,NULL);Game_Update();
    CHECK(releases==1 && arg1==1002);
    configure(&profile,expansion);reject_qualification=true;
    g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();CHECK(releases==0);
    g_intent.held=g_intent.pressed=0;reject_qualification=false;Game_Update();CHECK(releases==1 && arg1==1002);
    configure(&profile,expansion);
    Combat_Reset();ptr(roles[0],profile.active_offset,runtime_data);
    g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();
    g_intent.held=KEY(PAD_X)|KEY(PAD_Y);g_intent.pressed=KEY(PAD_X);Game_Update();
    g_intent.pressed=0;ptr(roles[0],profile.active_offset,NULL);Game_Update();CHECK(arg1==1001 && last_policy==0);
    Game_Update();CHECK(arg1==1001 && last_policy==0);
    g_intent.held=KEY(PAD_Y);Game_Update();CHECK(arg1==1002 && last_policy==1);

    /* 一组混合左右普通动作的右手预设连续推进；允许原版历史重置后的一个重试帧，不允许等超时。 */
    configure(&profile,expansion);record_actions=emulate_combo=true;
    Write32(methods_data[0],0x29,0);Write32(methods_data[1],0x29,0);
    Write32(hud_data,0x12C,(uint32_t)-1);Write32(hud_data,0x120,(uint32_t)-1);
    for (int stage=0;stage<6;++stage) {
        int before=releases;
        g_intent.held=g_intent.pressed=KEY(PAD_Y);g_input.now+=16;Game_Update();
        if (releases==before) {g_intent.pressed=0;Game_Update();}
        CHECK(releases==before+1 && arg1==(stage%3==1 ? 1002:1001));
        CHECK((int)Read32(hud_data,0x120)==stage%3);
        ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();
        g_intent.held=g_intent.pressed=0;Game_Update();
    }

    /* 原生同帧结束后立即续段：即使下一次轮询仍忙，结束通知也已打开正确的后续选择窗口。 */
    configure(&profile,expansion);record_actions=true;
    g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();
    ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();
    ptr(roles[0],profile.active_offset,runtime_data);reject_busy=false;
    g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();CHECK(releases==2 && arg1==1002);

    /* 后来的 Y 缓冲不能改写已经提交、稍后才执行的 X 的来源，也不能借它推进右手预设。 */
    configure(&profile,expansion);Write32(hud_data,0x12C,(uint32_t)-1);Write32(hud_data,0x120,(uint32_t)-1);
    g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();
    ptr(roles[0],profile.active_offset,runtime_data);
    g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();
    Combat_Record(111,0);CHECK((int)Read32(hud_data,0x120)==-1);
    Combat_Record(222,0);CHECK((int)Read32(hud_data,0x120)==-1);
    Combat_Reset();Game_Release();
    printf("%s交替按键、混合预设、结束窗口和来源隔离回归通过\n",expansion?"外传":"本体");
}

int main(void)
{
    exercise(false);exercise(true);
    combat_regression(false);combat_regression(true);
    chain_regression(false);chain_regression(true);
    printf("原生路由、双版本偏移与调用约定检查通过：%u 项\n",checks);
    return 0;
}
