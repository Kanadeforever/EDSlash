#include "../../src/Runtime/Perf.h"
#include "Plugin.h"
#include "Combat.h"
#include "Guard.h"
#include "Feedback.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* 在真正的 32 位进程中，让游戏适配层调用同约定的替身函数。
   这验证 this、参数值、功能路由及两个 Profile 偏移；不冒充真实游戏测试。 */
int __attribute__((noinline,noclone)) SettingsWindow_Active(void){return 0;}
int SettingsWindow_ShowPointer(void){return 0;}
const Profile *g_profile;
Intent g_intent;
PadInput g_input;
WCHAR g_directory[MAX_PATH];
HWND g_window;
static BYTE world_data[0x100], manager_data[0x300], mouse_data[0x100], ui_data[0x100];
static BYTE hud_data[0xD00], scene_data[0x100], table_data[6*16];
static BYTE class_data[0x300];static void *class_ptr=class_data;
static int custom_binding,custom_right;
static bool last_right_style;
static BYTE roles[3][0x500], record_data[0x40], binding_data[0x20], page_data[0xC0];
static void *world_ptr=world_data,*mouse_ptr=mouse_data,*scene_ptr=scene_data,*table_ptr=table_data,*hud_ptr=hud_data;
static int last_opcode, arg1, arg2, arg3, releases, last_policy, selected, quick_slot;
static void *last_target;
static bool page_visible, physical_mouse, busy_gate;
static int test_omni_setting=1;
#ifndef EDSLASH_REAL_CONFIG
static int test_inspect_distance=160;
static int test_single_trigger=1,test_combo_switch=0;
#endif
static int test_dodge_setting=1,test_dodge_distance=128,test_aim_ms=1000;
static int test_guard_setting=1,test_run_setting=1,test_cost=-1,test_recovery=-1;
static unsigned checks,readiness_logs;
static unsigned move_requests;
static bool reject_move;
static BYTE choices_data[32],groups_data[2][0x40],methods_data[3][0x90];
static uint32_t group_members[2][2],engine_tick,history_timeout;
static int group_table,method_table;
static bool unavailable_skill,record_actions,jump_zero_range,jump_builtin;
static BYTE throw_items[6][0x24];static bool test_throwing;
static bool reject_busy;
static bool emulate_combo;
static bool reject_qualification;
static WorldPoint last_aim;
static BYTE runtime_data[0x100];
#define CHECK(e) do { ++checks; if (!(e)) { fprintf(stderr,"适配检查失败，行 %d：%s\n",__LINE__,#e); exit(1); } } while(0)

/* 自有宿主记录共享UI绘制订阅，菜单回归可主动发出同一事件。 */
static RuntimeEventCallback cursor_draw_callback,cursor_begin_callback;
int Runtime_Subscribe(RuntimeEventId event,RuntimeEventCallback callback,void *user)
{
    if ((event!=RUNTIME_EVENT_UI_DRAW_END && event!=RUNTIME_EVENT_UI_DRAW_BEGIN) || !callback || user) return 0;
    if(event==RUNTIME_EVENT_UI_DRAW_BEGIN){cursor_begin_callback=callback;return 1;}
    cursor_draw_callback=callback;return 1;
}
void Log_Write(const char *format, ...)
{
    if (!strncmp(format,"[控制就绪]",strlen("[控制就绪]"))) ++readiness_logs;
    if (!strncmp(format,"[战斗提交]",strlen("[战斗提交]"))) {
        va_list args;va_start(args,format);const char *style=va_arg(args,const char *);
        last_right_style=!strcmp(style,"右手");va_end(args);
    }
}
bool Input_PhysicalDown(int key) { return key==VK_LBUTTON && physical_mouse; }
#ifndef EDSLASH_REAL_CONFIG
static ConfigSnapshot test_snapshot={.generation=1};
int RuntimeConfig_HasPending(void) {return 0;}
int RuntimeConfig_ApplyFrame(int idle) {(void)idle;return 0;}
const ConfigSnapshot *RuntimeConfig_Current(void) {return &test_snapshot;}
int RuntimeConfig_GetInt(ConfigId id)
{
    switch (id) {
    case CONFIG_INSPECT_DISTANCE:return test_inspect_distance;
    case CONFIG_LEGACY_ULTIMATE:return test_single_trigger;
    case CONFIG_COMBO_SWITCH:return test_combo_switch;
    case CONFIG_OMNI_GUARD:return test_omni_setting;
    case CONFIG_DODGE_DISTANCE:return test_dodge_distance;
    case CONFIG_DIRECTIONAL_DODGE:return test_dodge_setting;
    case CONFIG_CHARGE_GUARD:return test_guard_setting;
    case CONFIG_FREE_RUN:return test_run_setting;
    case CONFIG_GUARD_MODE:return test_cost>=0;
    case CONFIG_GUARD_PERCENT:return test_cost>=0 ? test_cost:0;
    case CONFIG_RECOVERY_MODE:return test_recovery>=0;
    case CONFIG_RECOVERY_PERCENT:return test_recovery>=0 ? test_recovery:0;
    case CONFIG_CONTROLLER_ENABLED:return 1;
    case CONFIG_MOVE_LEAD:return 12;
    case CONFIG_AIM_EXPAND_MS:return test_aim_ms;
    default:return 0;
    }
}
#endif
static GameProfile runtime_profile={.game_id=GAME_ID_DAOJIAN};
static BYTE configuration_player[0x400];
static int __attribute__((thiscall)) configuration_player_get(void *self)
{CHECK(self==configuration_player);return (int)(uintptr_t)configuration_player;}
static const RuntimeContext runtime_context={.profile=&runtime_profile};
const RuntimeContext *Runtime_GetContext(void) {return &runtime_context;}
#ifndef EDSLASH_REAL_CONFIG
ConfigBinding RuntimeConfig_GetBinding(unsigned game,unsigned role,unsigned slot)
{
    CHECK(game==runtime_profile.game_id);
    ConfigBinding binding={(custom_binding==2 ? slot==1:custom_binding && slot==1 && role==4),custom_binding==2 ? 222:111,custom_right};return binding;
}
#endif
bool Memory_Readable(const void *p,size_t bytes)
{
    MEMORY_BASIC_INFORMATION info;
    if (!p || !VirtualQuery(p,&info,sizeof info) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
    return (uintptr_t)p+bytes <= (uintptr_t)info.BaseAddress+info.RegionSize;
}
/* 保持宿主读内存替身为独立函数，避免GCC16在被多个fixture包含时丢失常量传播克隆符号。 */
uint32_t __attribute__((noinline,noclone)) Read32(const void *p,unsigned offset)
{ uint32_t v=0;if(p && Memory_Readable((BYTE *)p+offset,4))memcpy(&v,(BYTE *)p+offset,4);return v; }
void *ReadPtr(const void *p,unsigned offset) { return (void *)(uintptr_t)Read32(p,offset); }
void Write32(void *p,unsigned offset,uint32_t value) { memcpy((BYTE *)p+offset,&value,4); }
bool Memory_Patch(void *p,const void *bytes,size_t count)
{ CHECK(Memory_Readable(p,count));memcpy(p,bytes,count);return true; }
int RuntimeWin32_IsReadable(unsigned long address,unsigned long bytes)
{return Memory_Readable((void *)(uintptr_t)address,bytes);}
int RuntimeWin32_Read(unsigned long address,void *out,unsigned long bytes)
{
    if(!Memory_Readable((void *)(uintptr_t)address,bytes))return 0;
    memcpy(out,(void *)(uintptr_t)address,bytes);return 1;
}
void RuntimeLog_Write(const char *format,...) {(void)format;}
static void ptr(void *base,unsigned offset,void *value) { Write32(base,offset,(uint32_t)(uintptr_t)value); }

static int __attribute__((thiscall)) native_submit(void *self,int opcode,int a,int b,int c)
{
    CHECK(self==manager_data);
    last_opcode=opcode; arg1=a;arg2=b;arg3=c;
    if (opcode==16) { *((BYTE *)roles[0]+0x219)=a!=0; if(a) Write32(roles[0],0x14B,7); }
    if (opcode==18) Write32(roles[0],0x143,(uint32_t)a);
    if (opcode==9 || opcode==10 || opcode==11) {
        CHECK(a==1001 || a==1002 || a==1003 || (jump_builtin && a==10000) || (test_throwing && a>=10042 && a<10048));
        if (reject_busy && ReadPtr(roles[0],g_profile->active_offset)) return 0;
        if (opcode==11 && !record_actions) {
            unsigned offset=g_profile->pending_offset;
            Write32(roles[0],offset,(uint32_t)opcode);Write32(roles[0],offset+4,(uint32_t)a);
            Write32(roles[0],offset+8,(uint32_t)b);Write32(roles[0],offset+12,(uint32_t)c);
        }
        if(test_throwing && a>=10042) {
            unsigned index=(unsigned)(a-10042);unsigned quantity=Read32(throw_items[index],0x1C);
            if(!quantity)return 0;
            Write32(throw_items[index],0x1C,quantity-1);
        }
        ++releases;last_policy=opcode==11 ? 0:1;
        last_target=opcode==10 ? NULL:Game_Resolve((uint32_t)b);
        if (opcode==10) {last_aim.x=b;last_aim.y=c;}
        if (record_actions) {
            Combat_Record(jump_builtin && a==10000 ? (int)(Read32(methods_data[2],0x27)&0xFFFF):a>=10000 ? 222:(int)(Read32(methods_data[a-1001],0x27)&0xFFFF),0);
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
static int __attribute__((thiscall)) native_hover(void *self,int handle) { CHECK(self==manager_data && handle>=0 && handle<=3);Write32(self,4,(uint32_t)handle);return 1; }
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
    if (table==&method_table && jump_builtin && id==10000)return (int)(uintptr_t)methods_data[2];
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
{ CHECK(self==roles[0] && method>=1001 && method<=1003);return jump_zero_range ? 0:64; }
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

static int empty_preset=-1;
static BYTE preset_heads[4][16],preset_nodes[4][3][12];
static int __attribute__((thiscall)) native_combo(void *self,int index)
{
    CHECK(self==hud_data && index>=0 && index<4);
    unsigned count=index==empty_preset ? 0:emulate_combo ? 3:1;
    Write32(preset_heads[index],0,count);ptr(preset_heads[index],4,preset_nodes[index][0]);
    for(unsigned i=0;i<count;++i) {
        ptr(preset_nodes[index][i],0,i+1<count ? preset_nodes[index][i+1]:NULL);
        Write32(preset_nodes[index][i],8,emulate_combo && i!=1 ? 111:222);
    }
    return (int)(uintptr_t)preset_heads[index];
}

static int __attribute__((thiscall)) native_prepared(void *self,int selection)
{ CHECK(self==hud_data);Write32(self,0xBF4,(uint32_t)selection);Write32(self,0xBF8,0);return 1; }
static int __attribute__((thiscall)) native_icon(void *self,int selection)
{ CHECK(self==hud_data);return selection+4000; }
static int __attribute__((thiscall)) native_throw_group(void *role)
{ CHECK(role==roles[0]);return (int)(uintptr_t)groups_data[1]; }
static BYTE animation_data[0x20],other_animation[0x20];
static void *animation_pointer=animation_data;static bool animation_done;
static int __attribute__((thiscall)) native_animation(void *role)
{ CHECK(role==roles[0]);return (int)(uintptr_t)animation_pointer; }
static int __attribute__((thiscall)) native_animation_finished(void *animation)
{ CHECK(animation==animation_pointer);return animation_done; }
static WorldPoint *__cdecl native_dash_grid(WorldPoint *out,const WorldPoint *in);
static void configure(Profile *profile, bool expansion)
{
    Feedback_End();Combat_Reset();animation_pointer=animation_data;animation_done=false;test_throwing=false;test_guard_setting=test_run_setting=1;test_cost=test_recovery=-1;
    memset(profile,0,sizeof *profile);
    custom_binding=custom_right=0;runtime_profile.game_id=expansion ? GAME_ID_WAIZHUAN:GAME_ID_DAOJIAN;
    profile->player_class_global=(uintptr_t)&class_ptr;Write32(class_data,0x2E0,1);
    profile->world_global=(uintptr_t)&world_ptr;profile->mouse_global=(uintptr_t)&mouse_ptr;
    profile->entities_global=(uintptr_t)&scene_ptr;profile->handles_global=(uintptr_t)&table_ptr;
    profile->skill_global=(uintptr_t)&hud_ptr;profile->ui=(uintptr_t)ui_data;
    profile->submit=(uintptr_t)native_submit;profile->install_state=(uintptr_t)native_stop;
    profile->skill_release=(uintptr_t)native_release;profile->left_get=(uintptr_t)native_left;
    profile->right_get=(uintptr_t)native_right;profile->left_set=profile->right_set=(uintptr_t)native_select;
    profile->quick_use=(uintptr_t)native_quick;profile->inspect_gate=(uintptr_t)native_inspect_gate;
    profile->hover_set=(uintptr_t)native_hover;profile->world_to_grid=(uintptr_t)native_dash_grid;
    profile->relation=(uintptr_t)native_relation;profile->template_value=(uintptr_t)native_template;
    profile->get_jm=(uintptr_t)native_jm;
    profile->prepared_set=(uintptr_t)native_prepared;profile->icon_resolve=(uintptr_t)native_icon;
    profile->animation_get=(uintptr_t)native_animation;profile->animation_finished=(uintptr_t)native_animation_finished;
    profile->throw_group=(uintptr_t)native_throw_group;
    profile->ui_property=(uintptr_t)native_property;profile->combo_get=(uintptr_t)native_combo;
    profile->skill_groups=(uintptr_t)&group_table;profile->methods=(uintptr_t)&method_table;
    profile->game_tick=(uintptr_t)&engine_tick;profile->combo_timeout=(uintptr_t)&history_timeout;
    profile->lookup=(uintptr_t)native_lookup;profile->skill_eligibility=(uintptr_t)native_eligibility;
    profile->role_method=(uintptr_t)native_role_method;profile->method_usable=(uintptr_t)native_usable;
    profile->method_distance=(uintptr_t)native_distance;profile->facing_point=(uintptr_t)native_aim;
    profile->facing_direction=(uintptr_t)native_facing;profile->direction8=(uintptr_t)native_dir8;
    profile->invalid_offset=expansion?0x446:0x43A;profile->interact_offset=expansion?0x32D:0x321;
    profile->active_offset=expansion?0x359:0x34D;
    profile->pending_offset=expansion?0x456:0x446;
    profile->inspect_ready_offset=expansion?0x412:0x406;
    g_profile=profile;
    memset(roles,0,sizeof roles);memset(table_data,0,sizeof table_data);
    /* 场景Actor和存档Player故意不同，配置角色编号只允许读取后者。 */
    memset(configuration_player,0,sizeof configuration_player);Write32(configuration_player,0x348,4);
    Write32(roles[0],0x348,30);
    profile->inventory_root=(uintptr_t)configuration_player;profile->inventory_get=(uintptr_t)configuration_player_get;
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
    Write32(roles[2],profile->interact_offset,1);Write32(roles[2],profile->inspect_ready_offset,1);
    /* 受控角色按原版 typed resolver 的范围验证，不能把所有角色都强制等同普通 Actor 0x28。 */
    Write32(roles[0],0x67,0x3C);
    Write32(roles[0],0x73,1);ptr(roles[0],0x193,choices_data);
    Write32(hud_data,0xBF4,(uint32_t)-1);
    Write32(hud_data,0x12C,(uint32_t)-1);
    memset(groups_data,0,sizeof groups_data);memset(methods_data,0,sizeof methods_data);
    for (int i=0;i<2;++i) {
        Write32(groups_data[i],0x24,i==0 ? 111:222);
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
    g_intent.layer=LAYER_ITEM;g_intent.pressed=0;Game_Update();CHECK(quick_slot==2 && releases==2);
    int before_selected=selected;g_intent.layer=LAYER_SKILL;g_intent.pressed=0;g_intent.rx=1;Game_Update();CHECK(selected==before_selected);
    ptr(hud_data,0xC18,binding_data);Write32(binding_data,0x14,111);Write32(binding_data,0x18,'Q');
    g_intent.rx=0;g_intent.pressed=KEY(PAD_A);Game_Update();CHECK(selected==before_selected && releases==2);
    g_intent.layer=LAYER_GUARD;g_intent.pressed=0;Game_Update();
    CHECK(Game_Async(VK_MENU,0)==0);CHECK(!Game_Async(VK_SHIFT,0));
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
    CHECK(Read32(hud_data,0x120)==0);
    Combat_Reset();Combat_Record(222,0);CHECK(Read32(hud_data,0x120)==0);
    ptr(roles[0],profile.active_offset,NULL);g_intent.held=KEY(PAD_X);g_intent.pressed=KEY(PAD_X);
    g_input.now+=200;Game_Update();CHECK(Read32(hud_data,0x120)==0);
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


/* 用原版容器同样的节点布局回放交接，刻意模拟右键造成游标推进，检验交接不重复推进。 */
static BYTE transfer_nodes[64][12];
static int __attribute__((thiscall)) native_clear_history(void *self)
{
    CHECK(self==mouse_data);
    memset(transfer_nodes,0,sizeof transfer_nodes);
    Write32(self,0x48,0xFFFFFFFFu);Write32(self,0x4C,0xFFFFFFFFu);Write32(self,0x50,0xFFFFFFFFu);
    Write32(self,0x5C,0);Write32(self,0x60,0);Write32(self,0x64,0);return 0;
}
static int __attribute__((thiscall)) native_append_history(void *self,int selector,int direction,void *extra)
{
    CHECK(self==mouse_data);
    unsigned count=Read32(self,0x5C);CHECK(count<64);
    BYTE *node=transfer_nodes[count];void *tail=ReadPtr(self,0x64);
    ptr(node,0,NULL);ptr(node,4,tail);Write32(node,8,(uint32_t)selector);
    if(tail)ptr(tail,0,node);else ptr(self,0x60,node);
    ptr(self,0x64,node);Write32(self,0x5C,count+1);
    Write32(self,0x48,(uint32_t)selector);Write32(self,0x4C,engine_tick);
    Write32(self,0x50,0xFFFFFFFFu);Write32(self,0x54,(uint32_t)direction);
    Write32(self,0x58,(uint32_t)(uintptr_t)extra);
    Write32(hud_data,0x120,Read32(hud_data,0x120)+1);return 0;
}
static int __attribute__((thiscall)) native_inventory(void *self)
{ (void)self;return (int)(uintptr_t)throw_items; }
static int __attribute__((thiscall)) native_item_at(void *self,int slot)
{ CHECK(self==throw_items);return slot>=56 && slot<62 ? (int)(uintptr_t)throw_items[slot-56]:0; }

/* GCC16对这段长回放的跳转清理发生内部错误；仅宿主时间线用O1，生产业务仍是O2。 */
static void __attribute__((noinline,noclone,optimize("O1"))) transfer_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);
    profile.history_clear=(uintptr_t)native_clear_history;profile.history_record=(uintptr_t)native_append_history;
    record_actions=true;g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();
    CHECK(releases==1);Write32(hud_data,0x120,7);Write32(mouse_data,0x74,5);
    Combat_RecordExtra(999,4,9);
    engine_tick=1234;Combat_ExportHistory();
    CHECK(Read32(mouse_data,0x5C)==1 && Read32(transfer_nodes[0],8)==222);
    CHECK(Read32(mouse_data,0x4C)==1000 && Read32(mouse_data,0x50)==0xFFFFFFFFu);
    CHECK(Read32(hud_data,0x120)==7 && Read32(mouse_data,0x74)==0);
    CHECK(Read32(mouse_data,0x58)==0);
    g_intent.layer=LAYER_NATIVE;CHECK(!Combat_OwnsHistory() && Combat_AllowsMouseRetry());
    CHECK(Game_Async(VK_RBUTTON,(SHORT)0x8000)==(SHORT)0x8000);
    /* 真实鼠标继续完成下一动作，再接回手柄必须含两次成功历史。 */
    native_append_history(mouse_data,111,3,(void*)2);Write32(mouse_data,0x50,1240);
    Write32(hud_data,0x120,8);Combat_ImportHistory();
    g_intent.layer=LAYER_GAME;CHECK(Combat_OwnsHistory() && !Combat_AllowsMouseRetry());
    Combat_ExportHistory();
    CHECK(Read32(mouse_data,0x5C)==2 && Read32(transfer_nodes[1],8)==111);
    CHECK(Read32(mouse_data,0x4C)==1234 && Read32(mouse_data,0x50)==1240);
    CHECK(Read32(mouse_data,0x54)==3 && Read32(mouse_data,0x58)==2 && Read32(hud_data,0x120)==8);
    /* 切图不能把上一角色的历史交给新世界。 */
    void *saved_world=world_ptr;BYTE another_world[0x100]={0};world_ptr=another_world;
    Combat_ExportHistory();CHECK(Read32(mouse_data,0x5C)==2);world_ptr=saved_world;
    ptr(mouse_data,0x38,roles[1]);Combat_ImportHistory();CHECK(!Combat_OwnsHistory());
    ptr(mouse_data,0x38,roles[0]);Combat_ImportHistory();CHECK(Combat_OwnsHistory());
    Combat_Reset();printf("%s物理鼠标和手柄双向历史交接回归通过\n",expansion?"外传":"本体");
}

static float guard_threshold=5.0f;
static int test_maximum=100;
static uintptr_t guard_vtable[48];
static unsigned dodge_installs;
static float read_float(void *role,unsigned offset)
{ uint32_t bits=Read32(role,offset);float value;memcpy(&value,&bits,4);return value; }
static void write_float(void *role,unsigned offset,float value)
{ uint32_t bits;memcpy(&bits,&value,4);Write32(role,offset,bits); }
static void __attribute__((thiscall)) native_stamina(void *role,float amount)
{
    float value=read_float(role,g_profile->stamina_offset)+amount;
    if(value<0)value=0;
    if(value>test_maximum)value=(float)test_maximum;
    write_float(role,g_profile->stamina_offset,value);
}
static int __attribute__((thiscall)) native_guard_state(void *role,int index)
{ CHECK(index==0x6A);return *((BYTE*)role+0x219); }
/* 等级或装备变化可能改变最大值；每次事件都应重新读取这个原版接口。 */
static int __attribute__((thiscall)) native_maximum(void *role)
{ CHECK(role==roles[0]);return test_maximum; }
static int __attribute__((thiscall)) native_guard_unit(void *role)
{ CHECK(role==roles[0]);return 3; }
static int __attribute__((thiscall)) native_guard_off(void *role,int opcode,int a,int b,int c,int d,int e)
{
    CHECK(role==roles[0] && opcode==16 && !a && !b && !c && !d && !e);
    *((BYTE*)role+0x219)=0;return 1;
}
static int __attribute__((thiscall)) native_guard_eligible(void *role)
{ CHECK(role==roles[0]);return 1; }
static BYTE dash_map[0x80],dash_cells[128*128*19];
static bool dash_wall;static unsigned legacy_motion_calls,legacy_init_calls;
static uintptr_t patched_callee(uintptr_t call)
{ int32_t displacement;memcpy(&displacement,(void *)(call+1),4);return call+5+displacement; }
static int __attribute__((thiscall)) native_dash_init(void *role)
{ CHECK(role==roles[0]);++legacy_init_calls;return 1; }
static int __attribute__((thiscall)) native_dash_motion(void *role)
{ CHECK(role==roles[0]);++legacy_motion_calls;return 1; }
static int __attribute__((thiscall)) native_dash_bounds(void *map,int x,int y)
{ CHECK(map==dash_map);return x>=0 && x<128 && y>=0 && y<128; }
static int __attribute__((thiscall)) native_dash_cell(void *cell,int kind)
{ (void)kind;return !dash_wall || (BYTE *)cell<dash_cells+(100*128+101)*19; }
static int __attribute__((thiscall)) native_dash_commit(void *role,int x,int y,void *mode)
{ CHECK(role==roles[0] && !mode && x>=0 && x<128 && y>=0 && y<128);return 1; }
static int __attribute__((thiscall)) native_dash_position(void *role,int x,int y)
{ CHECK(role==roles[0]);Write32(role,0x2C,(uint32_t)x);Write32(role,0x30,(uint32_t)y);return 1; }
static WorldPoint *__cdecl native_dash_grid(WorldPoint *out,const WorldPoint *in)
{ out->x=in->x/64;out->y=in->y/64;return out; }
static int __attribute__((thiscall)) native_dash_effect(void *role,int effect,int a,int b,int c)
{ CHECK(role==roles[0] && effect==0x66 && a==b && c==0);return 1; }
static int __attribute__((thiscall)) native_dodge_install(void *role,int state,int x,int y,int direction)
{
    if (state==1) { CHECK(role==roles[0]);Write32(role,0x73,1);return 1; }
    CHECK(role==roles[0] && state==0x17 && direction>=0 && direction<8);
    CHECK(x!=6400 || y!=6400);Write32(role,0x73,0x17);++dodge_installs;
    return ((This0)patched_callee(g_profile->dodge_init_call))(role);
}
static void make_call(BYTE *where,uintptr_t callee)
{ int32_t displacement=(int32_t)(callee-(uintptr_t)where-5);where[0]=0xE8;memcpy(where+1,&displacement,4); }
static float damage_to_apply;static bool damage_from_npc;
static int __attribute__((thiscall)) native_damage_owner(void *runtime)
{ CHECK(runtime==runtime_data);return (int)(uintptr_t)(damage_from_npc ? roles[1]:roles[0]); }
static int __attribute__((thiscall)) native_damage_receiver(void *victim,void *runtime)
{
    CHECK(runtime==runtime_data);
    float health=read_float(victim,g_profile->health_offset);
    write_float(victim,g_profile->health_offset,health-damage_to_apply);return 1;
}

static void guard_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);
    profile.stamina_offset=expansion?0x3B6:0x3AA;
    profile.health_offset=expansion?0x3AE:0x3A2;
    profile.guard_threshold=(uintptr_t)&guard_threshold;profile.guard_get=(uintptr_t)native_guard_state;
    profile.stamina_adjust=(uintptr_t)native_stamina;profile.guard_check=(uintptr_t)native_guard_eligible;
    memset(guard_vtable,0,sizeof guard_vtable);guard_vtable[0xA0/4]=(uintptr_t)native_guard_unit;
    guard_vtable[0x80/4]=(uintptr_t)native_maximum;
    guard_vtable[0x5C/4]=(uintptr_t)native_guard_off;ptr(roles[0],0,guard_vtable);
    *((BYTE*)roles[0]+0x219)=1;write_float(roles[0],profile.stamina_offset,50);
    Guard_Periodic(roles[0],-6.0f);CHECK(read_float(roles[0],profile.stamina_offset)==50.0f);
    CHECK(Guard_Hit(roles[0],0x6A)==1 && read_float(roles[0],profile.stamina_offset)==44.0f);
    CHECK(Guard_Hit(roles[0],0x6A)==1 && read_float(roles[0],profile.stamina_offset)==38.0f);
    write_float(roles[0],profile.stamina_offset,5);
    CHECK(Guard_Hit(roles[0],0x6A)==0 && read_float(roles[0],profile.stamina_offset)==0.0f);
    Guard_Periodic(roles[0],10.0f);CHECK(read_float(roles[0],profile.stamina_offset)==10.0f);
    *((BYTE*)roles[1]+0x219)=1;write_float(roles[1],profile.stamina_offset,50);
    Guard_Periodic(roles[1],-6.0f);CHECK(read_float(roles[1],profile.stamina_offset)==44.0f);
    CHECK(Guard_Hit(roles[1],0x6A)==1 && read_float(roles[1],profile.stamina_offset)==44.0f);

    /* 在自有进程执行真实裸钩子，检查寄存器/压栈/返回；不注入游戏。
       小程序只复现入口前已保存 ESI/EDI 和目标门的栈形状，原版冷却资格另由双样本字节核对。 */
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
    *((BYTE*)roles[0]+0x219)=1;
    CHECK(((This1)angle_code)(roles[0],0)==1 && ((This1)angle_code)(roles[0],4)==1);
    CHECK(((This1)angle_code)(roles[1],0)==0 && ((This1)angle_code)(roles[1],4)==1);
    *((BYTE*)roles[0]+0x219)=0;CHECK(((This1)angle_code)(roles[0],0)==0);
    *((BYTE*)roles[0]+0x219)=1;
    /* 手柄开启防御仍沿事件前朝向：即使原事件把角色朝向改成旧鼠标方向，也用原转向接口恢复。 */
    *((BYTE*)roles[0]+0x219)=0;Write32(roles[0],0x14B,0);
    g_intent.layer=LAYER_GUARD;g_intent.lx=g_intent.ly=0;Guard_Update(roles[0]);
    CHECK(last_aim.x==(int)Read32(roles[0],0x2C)+256 && last_aim.y==(int)Read32(roles[0],0x30));
    /* 真实攻击入口返回“处理过”但没扣生命时不回；实际扣生命才恢复。 */
    ptr(runtime_data,0x8F,methods_data[0]);write_float(roles[1],profile.health_offset,100);
    write_float(roles[0],profile.stamina_offset,40);damage_to_apply=0;
    CHECK(((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data)==1);
    CHECK(read_float(roles[0],profile.stamina_offset)==40);
    damage_to_apply=5;CHECK(((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data)==1);
    CHECK(read_float(roles[0],profile.stamina_offset)==46);
    damage_to_apply=-5;((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data);
    CHECK(read_float(roles[0],profile.stamina_offset)==46);
    damage_to_apply=5;damage_from_npc=true;((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data);
    CHECK(read_float(roles[0],profile.stamina_offset)==46);damage_from_npc=false;
    write_float(roles[0],profile.stamina_offset,98);((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data);
    CHECK(read_float(roles[0],profile.stamina_offset)==100);
    /* 免费奔跑不再依赖敌人的追击目标，战斗和非战斗都免支出。 */
    write_float(roles[0],profile.stamina_offset,50);Guard_RunCost(roles[0],-3);CHECK(read_float(roles[0],profile.stamina_offset)==50);
    Write32(roles[1],0x143,1);Guard_RunCost(roles[0],-3);CHECK(read_float(roles[0],profile.stamina_offset)==50);
    /* 验证真实入口包装不会把原版 Alt 松开当成手柄防御松开。 */
    int32_t input_relative;memcpy(&input_relative,code+81,4);
    This1 input_hook=(This1)(uintptr_t)((uintptr_t)code+85+input_relative);
    *((BYTE*)roles[0]+0x219)=1;g_intent.layer=LAYER_GUARD;CHECK(input_hook(roles[0],0x6A)==0);
    g_intent.layer=LAYER_NATIVE;CHECK(input_hook(roles[0],0x6A)==1);
    *((BYTE*)roles[0]+0x219)=1;g_intent.layer=LAYER_GUARD;g_intent.lx=1;g_intent.ly=0;
    dodge_installs=0;g_intent.layer=LAYER_ACTION_MENU;Guard_Update(roles[0]);CHECK(dodge_installs==0 && roles[0][0x219]);
    g_intent.layer=LAYER_DUAL;Guard_Update(roles[0]);CHECK(dodge_installs==0 && roles[0][0x219]);
    g_intent.layer=LAYER_GUARD;Guard_Update(roles[0]);CHECK(dodge_installs==1);
    Guard_Update(roles[0]);CHECK(dodge_installs==1);
    g_intent.lx=0;Guard_Update(roles[0]);g_intent.lx=-1;Guard_Update(roles[0]);CHECK(dodge_installs==2);
    CHECK(((This0)profile.dodge_start)(roles[1])==0);CHECK(dodge_installs==2);
    for(unsigned i=0;i<8;++i) ((This0)patched_callee(profile.dodge_motion_call))(roles[0]);
    float wx,wy;Control_WorldDirection(-1,0,&wx,&wy);
    CHECK((int)Read32(roles[0],0x2C)==6400+(int)lroundf(wx*128));
    CHECK((int)Read32(roles[0],0x30)==6400+(int)lroundf(wy*128));
    CHECK(Read32(roles[0],0x73)==1 && legacy_motion_calls==0 && legacy_init_calls==0);
    /* 八个推杆方向均沿直线达到配置距离，不回到原版的参考点圆弧。 */
    const float axes[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    for(unsigned n=0;n<8;++n) {
        Guard_Reset();Write32(roles[0],0x2C,6400);Write32(roles[0],0x30,6400);
        g_intent.lx=axes[n][0];g_intent.ly=axes[n][1];Guard_Update(roles[0]);
        for(unsigned i=0;i<8;++i) ((This0)patched_callee(profile.dodge_motion_call))(roles[0]);
        Control_WorldDirection(g_intent.lx,g_intent.ly,&wx,&wy);
        CHECK((int)Read32(roles[0],0x2C)==6400+(int)lroundf(wx*128));
        CHECK((int)Read32(roles[0],0x30)==6400+(int)lroundf(wy*128));
    }
    /* 中途墙格提前停，测试长距离终点检查遗漏、以及其它角色占用不被近邻回退绕开。 */
    for(unsigned n=0;n<2;++n) {
        Guard_Reset();Write32(roles[0],0x2C,6400);Write32(roles[0],0x30,6400);
        dash_wall=n==0;
        if(n==1) {unsigned short occupant=2;memcpy(dash_cells+(100*128+101)*19+10,&occupant,2);}
        g_intent.lx=1;g_intent.ly=0.5f;Guard_Update(roles[0]);
        for(unsigned i=0;i<8 && Read32(roles[0],0x73)==0x17;++i)
            ((This0)patched_callee(profile.dodge_motion_call))(roles[0]);
        CHECK(Read32(roles[0],0x2C)>6400 && Read32(roles[0],0x2C)<6464);
        CHECK(Read32(roles[0],0x30)==6400 && Read32(roles[0],0x73)==1);
    }
    dash_wall=false;memset(dash_cells,0xFF,sizeof dash_cells);
        /* 用户改距离后读取新配置，不用256参考点推算距离，也不能沿用上次启动的缓存值。 */
    Guard_Shutdown();test_dodge_distance=64;CHECK(Guard_Initialize());Guard_Reset();
    Write32(roles[0],0x2C,6400);Write32(roles[0],0x30,6400);g_intent.lx=1;g_intent.ly=0.5f;
    Guard_Update(roles[0]);
    for(unsigned i=0;i<8;++i) ((This0)patched_callee(profile.dodge_motion_call))(roles[0]);
    CHECK(Read32(roles[0],0x2C)==6464 && Read32(roles[0],0x30)==6400);
    Guard_Shutdown();test_dodge_distance=128;test_dodge_setting=0;CHECK(Guard_Initialize());
    ((This0)patched_callee(profile.dodge_init_call))(roles[0]);
    ((This0)patched_callee(profile.dodge_motion_call))(roles[0]);
    CHECK(legacy_init_calls==1 && legacy_motion_calls==1);
    test_dodge_setting=1;
    Guard_Shutdown();CHECK(!memcmp(code+9,"\x85\xFF\x74\x6A\x57\x56",6));
    /* 两个开关分别恢复原版扣费，不关闭原生防御/闪避接口。 */
    test_guard_setting=test_run_setting=0;CHECK(Guard_Initialize());
    *((BYTE*)roles[0]+0x219)=1;write_float(roles[0],profile.stamina_offset,50);
    Guard_Periodic(roles[0],-6);CHECK(read_float(roles[0],profile.stamina_offset)==44);
    CHECK(Guard_Hit(roles[0],0x6A)==1 && read_float(roles[0],profile.stamina_offset)==44);
    Guard_RunCost(roles[0],-3);CHECK(read_float(roles[0],profile.stamina_offset)==41);
    Guard_Shutdown();test_guard_setting=0;test_run_setting=1;CHECK(Guard_Initialize());
    write_float(roles[0],profile.stamina_offset,50);Guard_Periodic(roles[0],-6);Guard_RunCost(roles[0],-3);
    CHECK(read_float(roles[0],profile.stamina_offset)==44);
    Guard_Shutdown();test_guard_setting=1;test_run_setting=0;CHECK(Guard_Initialize());
    write_float(roles[0],profile.stamina_offset,50);Guard_Periodic(roles[0],-6);Guard_RunCost(roles[0],-3);
    CHECK(read_float(roles[0],profile.stamina_offset)==47);
    Guard_Shutdown();test_guard_setting=test_run_setting=1;CHECK(Guard_Initialize());Guard_Shutdown();
    test_cost=1000;test_recovery=500;CHECK(Guard_Initialize());
    *((BYTE*)roles[0]+0x219)=1;write_float(roles[0],profile.stamina_offset,50);
    Guard_Hit(roles[0],0x6A);CHECK(read_float(roles[0],profile.stamina_offset)==40);
    damage_to_apply=5;((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data);
    CHECK(read_float(roles[0],profile.stamina_offset)==45);
    Guard_Shutdown();test_cost=0;test_recovery=0;CHECK(Guard_Initialize());
    Guard_Hit(roles[0],0x6A);((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data);
    CHECK(read_float(roles[0],profile.stamina_offset)==45);
    /* 最大体力200时12.50%扣25、10.00%回20；更改最大值后立即按新值计算。 */
    Guard_Shutdown();test_maximum=200;test_cost=1250;test_recovery=1000;CHECK(Guard_Initialize());
    *((BYTE*)roles[0]+0x219)=1;write_float(roles[0],profile.stamina_offset,100);
    Guard_Hit(roles[0],0x6A);CHECK(read_float(roles[0],profile.stamina_offset)==75);
    ((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data);
    CHECK(read_float(roles[0],profile.stamina_offset)==95);
    test_maximum=400;Guard_Hit(roles[0],0x6A);
    CHECK(read_float(roles[0],profile.stamina_offset)==45);
    ((This1)profile.hit_receiver)(roles[1],(int)(uintptr_t)runtime_data);
    CHECK(read_float(roles[0],profile.stamina_offset)==85);
    test_maximum=100;
    Guard_Shutdown();test_cost=test_recovery=-1;CHECK(Guard_Initialize());Guard_Shutdown();
    test_omni_setting=0;CHECK(Guard_Initialize());
    *((BYTE*)roles[0]+0x219)=1;
    CHECK(((This1)angle_code)(roles[0],0)==0 && ((This1)angle_code)(roles[0],4)==1);
    Guard_Shutdown();test_omni_setting=1;
    /* 原版最低5点仍保留：有4.7点时会退出防御，这就是“还有体力却不能防”的一个原因。 */
    test_maximum=80;test_cost=100;CHECK(Guard_Initialize());
    *((BYTE*)roles[0]+0x219)=1;write_float(roles[0],profile.stamina_offset,5.5f);
    Guard_Hit(roles[0],0x6A);CHECK(fabsf(read_float(roles[0],profile.stamina_offset)-4.7f)<0.001f);
    CHECK(*((BYTE*)roles[0]+0x219)==0);Guard_Shutdown();test_maximum=100;test_cost=-1;CHECK(Guard_Initialize());Guard_Shutdown();
    CHECK(VirtualFree(angle_code,0,MEM_RELEASE));
    CHECK(VirtualFree(code,0,MEM_RELEASE));Game_Release();
    printf("%s防御扣费、耗尽、非玩家隔离和闪避裸钩子栈回归通过\n",expansion?"外传":"本体");
}

static void quick_cast_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);record_actions=true;emulate_combo=true;
    Write32(hud_data,0x12C,(uint32_t)-1);Write32(hud_data,0x120,(uint32_t)-1);
    ptr(hud_data,0xC18,binding_data);Write32(binding_data,0x14,222);Write32(binding_data,0x18,'Q');
    g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();CHECK(releases==1 && arg1==1001);
    ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();
    /* 显式配置RT+A后直接发技能，不装备、不推进套组。 */
    custom_binding=2;
    g_intent.layer=LAYER_SKILL;g_intent.held=g_intent.pressed=KEY(PAD_A);Game_Update();
    if(releases==1){g_intent.pressed=0;Game_Update();}
    CHECK(releases==2 && arg1==1002 && (int)Read32(hud_data,0x120)==0);
    ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();
    /* 插入快捷技能后 Y 仍是套组第二项，而不是绑定到快捷技能。 */
    g_intent.layer=LAYER_GAME;g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();
    if(releases==2){g_intent.pressed=0;Game_Update();}
    CHECK(releases==3 && arg1==1002 && (int)Read32(hud_data,0x120)==1);
    profile.inventory_root=(uintptr_t)throw_items;profile.inventory_get=(uintptr_t)native_inventory;
    profile.item_at=(uintptr_t)native_item_at;test_throwing=true;
    for(unsigned i=0;i<6;++i){Write32(hud_data,0x208+(i+6)*0xE4,56+i);Write32(throw_items[i],0x1C,2);Write32(throw_items[i],0x20,42+i);}
    for(int n=0;n<2;++n){
        ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();
        g_intent.layer=LAYER_ITEM;g_intent.held=g_intent.pressed=KEY(PAD_A);Game_Update();
        CHECK(releases==4+n && arg1==10042 && (int)Read32(hud_data,0x120)==1);
    }
    CHECK(Read32(throw_items[0],0x1C)==0);
    g_intent.pressed=KEY(PAD_A);Game_Update();CHECK(releases==5);
    CHECK((int)Read32(hud_data,0x12C)==-1);
    /* LT+方向切套组，技能层右摇杆不再改变套组。 */
    g_intent.layer=LAYER_GUARD;g_intent.pressed=KEY(PAD_RIGHT);Game_Update();CHECK(selected==-2);
    printf("%s快捷技能直发、同槽连续投掷、空槽与独立套组回归通过\n",expansion?"外传":"本体");
    Combat_Reset();Game_Release();
}

static int icon_seen,selection_seen,icon_draw_calls,shade_seen,alpha_seen;
static int __attribute__((thiscall)) native_icon_draw(void *self,int context,int icon,int selection,
    int x,int y,int shade,int alpha,int bindings,int side)
{
    CHECK(self==hud_data && context==123 && x==456 && y==789 && bindings==1 && side==0);
    shade_seen=shade;alpha_seen=alpha;icon_seen=icon;selection_seen=selection;++icon_draw_calls;return 17;
}
static void feedback_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);record_actions=true;engine_tick=100;
    Write32(groups_data[0],0x32,1000);Write32(groups_data[1],0x32,1001);
    BYTE *code=VirtualAlloc(NULL,16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);CHECK(code!=NULL);
    make_call(code,(uintptr_t)native_icon_draw);profile.icon_draw=(uintptr_t)native_icon_draw;
    profile.right_icon_call=(uintptr_t)code;CHECK(Feedback_Initialize());
    g_input.connected=g_input.focused=true;g_intent.layer=LAYER_GUARD;g_intent.held=g_intent.pressed=0;
    int selection,icon;releases=0;
    Feedback_Ultimate(0);CHECK(Read32(hud_data,0xBF4)==111 && releases==0);
    CHECK(Feedback_Selection(&selection,&icon) && selection==111 && icon==4111);
    Feedback_Ultimate(1);CHECK(Read32(hud_data,0xBF4)==222 && releases==0);
    /* 模拟游戏HUD自己的准备过期，下一次必须重新准备，不能沿用插件私有确认标志。 */
    native_prepared(hud_data,-1);Feedback_Ultimate(1);CHECK(releases==0 && Read32(hud_data,0xBF4)==222);
    Feedback_Ultimate(1);Combat_Update(roles[0],0);
    CHECK(releases==1 && arg1==1002 && (int)Read32(hud_data,0xBF4)==-1);
    CHECK(Feedback_Selection(&selection,&icon) && selection==222);
    typedef int (__attribute__((thiscall)) *IconCall)(void *,int,int,int,int,int,int,int,int,int);
    CHECK(((IconCall)patched_callee(profile.right_icon_call))(hud_data,123,0,-1,456,789,4,32,1,0)==17);
    CHECK(icon_seen==4222 && selection_seen==222 && shade_seen==0 && alpha_seen==-1);
    /* Runtime结束时动画还在收尾，不能把图标立即恢复；真正末帧完成才恢复。 */
    ptr(roles[0],profile.active_offset,NULL);engine_tick++;Combat_End();
    CHECK(Feedback_Selection(&selection,&icon) && selection==222);
    animation_done=true;CHECK(!Feedback_Selection(&selection,&icon));animation_done=false;
    ptr(roles[0],profile.active_offset,runtime_data);
    Feedback_Start(111,ACTION_SKILL);CHECK(Feedback_Selection(&selection,&icon) && selection==111);
    /* 物理鼠标接管本身不撤掉正在发动的图标，新Runtime取代旧Runtime才恢复原图标。 */
    g_intent.layer=LAYER_NATIVE;CHECK(Feedback_Selection(&selection,&icon));
    /* 同一动作续段换Runtime仍显示。新实际动作会经成功通知重设显示，不能靠地址区别续段。 */
    ptr(roles[0],profile.active_offset,methods_data[0]);CHECK(Feedback_Selection(&selection,&icon));
    ptr(roles[0],profile.active_offset,NULL);engine_tick++;Feedback_RuntimeEnded();
    CHECK(Feedback_Selection(&selection,&icon));
    animation_pointer=other_animation;CHECK(!Feedback_Selection(&selection,&icon));
    animation_pointer=animation_data;
    ptr(roles[0],profile.active_offset,runtime_data);
    Feedback_Start(10042,ACTION_THROW);CHECK(Feedback_Selection(&selection,&icon) && selection==222);
    engine_tick++;ptr(roles[0],profile.active_offset,NULL);CHECK(!Feedback_Selection(&selection,&icon));
    Feedback_Shutdown();CHECK(patched_callee(profile.right_icon_call)==(uintptr_t)native_icon_draw);
    CHECK(VirtualFree(code,0,MEM_RELEASE));Combat_Reset();record_actions=false;
    printf("%s图标包装、结束恢复、来源切换及必杀准备/换选/过期/确认回归通过\n",expansion?"外传":"本体");
}

static void configured_binding_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);record_actions=true;custom_binding=1;
    g_intent.layer=LAYER_SKILL;g_intent.held=g_intent.pressed=KEY(PAD_A);
    Game_Update();
    CHECK(releases==1 && arg1==1001 && last_right_style);
    ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();
    Combat_Reset();
    ptr(hud_data,0xC18,binding_data);Write32(binding_data,0x14,222);Write32(binding_data,0x18,'Q');
    Write32(configuration_player,0x348,30);g_intent.pressed=KEY(PAD_A);Game_Update();
    CHECK(releases==1); /* 另一个角色未设置，不串用绑定也不回退原键盘选择。 */
    ptr(roles[0],profile.active_offset,NULL);engine_tick+=2;Combat_End();Combat_Reset();
    Write32(configuration_player,0x348,4);custom_right=1;g_intent.pressed=KEY(PAD_A);Game_Update();
    CHECK(releases==2 && last_right_style);
    Combat_Reset();Game_Release();custom_binding=0;
}
static int __attribute__((thiscall)) move_terrain(void *cell,int kind)
{ (void)kind;return *((BYTE *)cell)!=0; }
static void move_obstacle_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);
    profile.map_bounds=(uintptr_t)native_dash_bounds;profile.cell_passable=(uintptr_t)move_terrain;
    ptr(roles[0],0x6F,dash_map);Write32(dash_map,8,128);ptr(dash_map,0x14,dash_cells);
    memset(dash_cells,0xFF,sizeof dash_cells);
    g_intent.lx=1;Game_Update();int far_x=(int)Read32(mouse_data,0x90),far_y=(int)Read32(mouse_data,0x94);
    CHECK(far_x>100 && far_y<100);g_intent.lx=0;Game_Update();
    /* 世界右前方两格后的墙只能缩短直线目标，不能改投左侧绕路点。 */
    dash_cells[(98*128+102)*19]=0;g_intent.lx=1;Game_Update();
    CHECK(Read32(mouse_data,0x90)==101 && Read32(mouse_data,0x94)==99);
    g_intent.lx=0;Game_Update();dash_cells[(99*128+101)*19]=0;
    unsigned before=move_requests;g_intent.lx=1;Game_Update();CHECK(move_requests==before);
    memset(dash_cells,0xFF,sizeof dash_cells);g_intent.lx=0;Game_Update();
    g_intent.lx=1;Game_Update();CHECK(Read32(mouse_data,0x90)==(unsigned)far_x && Read32(mouse_data,0x94)==(unsigned)far_y);
    g_intent.lx=0;Game_Update();dash_cells[(97*128+99)*19]=0;
    /* 画面右上经原等距逆投影后也须停在障碍前，不能退向画面左边。 */
    g_intent.lx=1;g_intent.ly=-1;Game_Update();
    CHECK(Read32(mouse_data,0x90)==99 && Read32(mouse_data,0x94)==98);
    Game_Release();
}
#ifndef EDSLASH_REAL_CONFIG
/* 长期左右手选择和组合优先级使用真实生产Game/Combat；原执行器只由宿主替身记录。 */
static int __attribute__((thiscall)) selected_left(void *hud) {CHECK(hud==hud_data);return (int)Read32(hud,0x128);}
static void new_combat_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);Combat_Reset();Game_Release();
    test_single_trigger=0;test_combo_switch=0;
    static const int face_order[]={PAD_Y,PAD_B,PAD_A,PAD_X};
    static const int dpad_order[]={PAD_UP,PAD_RIGHT,PAD_DOWN,PAD_LEFT};
    for (unsigned mode=0;mode<3;++mode) {
        test_single_trigger=mode==2;test_combo_switch=mode==1 ? 1:0;
        for(unsigned slot=0;slot<4;++slot) {
            selected=0;g_intent.layer=LAYER_GUARD;
            g_intent.held=g_intent.pressed=KEY(mode==0 ? face_order[slot]:dpad_order[slot]);Game_Update();
            CHECK(selected==-(int)(slot+1));
        }
        if (mode<2) {
            selected=0;g_intent.pressed=KEY(mode==0 ? PAD_UP:PAD_Y);Game_Update();CHECK(selected==0);
        }
    }
    empty_preset=2;selected=77;Combat_SelectCombo(2);CHECK(selected==77);empty_preset=-1;
    test_single_trigger=0;test_combo_switch=0;
    Write32(groups_data[0],0x32,1000);Write32(groups_data[1],0x32,1001);
    Write32(hud_data,0xBF4,UINT32_MAX);selected=77;releases=0;g_intent.layer=LAYER_DUAL;
    g_intent.held=g_intent.pressed=KEY(PAD_A);Game_Update();
    CHECK(Read32(hud_data,0xBF4)==111 && releases==0 && selected==77);
    g_intent.held=g_intent.pressed=KEY(PAD_B);Game_Update();CHECK(Read32(hud_data,0xBF4)==222 && releases==0);
    g_intent.held=g_intent.pressed=KEY(PAD_B);Game_Update();CHECK(releases==1 && arg1==1002);
    /* 双扳机的十字、肩键、BACK/START不能兼办单LT套组或RT技能。 */
    unsigned before=releases;selected=77;
    g_intent.held=g_intent.pressed=KEY(PAD_UP)|KEY(PAD_RIGHT)|KEY(PAD_DOWN)|KEY(PAD_LEFT)|KEY(PAD_LB)|KEY(PAD_RB)|KEY(PAD_BACK)|KEY(PAD_START);
    Game_Update();CHECK((unsigned)releases==before && selected==77);
    Game_Release();Combat_Reset();configure(&profile,expansion);profile.left_get=(uintptr_t)selected_left;
    Write32(hud_data,0x128,222);Write32(hud_data,0x12C,111);Write32(groups_data[0],0x32,0);
    g_intent.layer=LAYER_GAME;g_intent.held=g_intent.pressed=KEY(PAD_X);Game_Update();CHECK(releases==1 && arg1==1002);
    Game_Release();Combat_Reset();g_intent.held=g_intent.pressed=KEY(PAD_Y);Game_Update();CHECK(releases==2 && arg1==1001);
    CHECK(Read32(hud_data,0x128)==222 && Read32(hud_data,0x12C)==111);
    /* 投掷类选择不进入Y规则；原BF4准备态也不能劫持Y的普通技能。 */
    Game_Release();Combat_Reset();Write32(groups_data[0],0x32,2);Game_Update();CHECK(releases==2);
    Write32(groups_data[0],0x32,0);Write32(hud_data,0xBF4,222);Game_Update();CHECK(releases==3 && arg1==1001);
    Game_Release();Combat_Reset();Write32(roles[2],profile.interact_offset,0);
    g_intent.pressed=g_intent.held=0;g_intent.lx=1;Game_Update();CHECK(Read32(roles[0],0x73)==0x0B);
    g_intent.pressed=KEY(PAD_A);Game_Update();g_intent.pressed=0;g_intent.lx=0;Game_Update();
    CHECK(Read32(roles[0],0x73)==1); /* 无目标空按A不能遗失松杆停止的拥有权 */
    Game_Release();Combat_Reset();test_single_trigger=1;test_combo_switch=0;
}
#endif
/* 菜单路由3和世界等待3不能混同；原诊断误把物理菜单停留计为加载等待。 */
static void readiness_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);readiness_logs=0;g_intent.layer=LAYER_NATIVE;
    page_visible=true;ptr(ui_data,0x40,page_data);CHECK(Game_Menu());Game_Diagnose();
    page_visible=false;ptr(ui_data,0x40,NULL);CHECK(!Game_Menu());g_input.now+=1000;Game_Diagnose();
    CHECK(readiness_logs==0);
    Write32(world_data,0x58,0);Game_Diagnose();g_input.now+=200;
    Write32(world_data,0x58,1);Game_Diagnose();CHECK(readiness_logs==1);
}
/* 原技能快捷与B落点使用同一绑定；不从场景Actor取玩家档案字段。 */
static int jump_fixture_selection;
static BYTE jump_camera[0x14];
static int __attribute__((thiscall)) jump_projection(void *self,WorldPoint *out,int x,int y)
{CHECK(self==jump_camera);*out=(WorldPoint){6400+x+2*y,6400-x+2*y};return 1;}
static int __attribute__((thiscall)) jump_range(void *self,int method)
{CHECK(self==roles[0] && (method==1001 || method==1002 || (jump_builtin && method==10000)));return 1;}
static void jump_regression(bool expansion)
{
    Profile profile;configure(&profile,expansion);Game_Release();
    profile.projection=(uintptr_t)jump_projection;
    profile.method_range=(uintptr_t)jump_range;profile.projection_global=(uintptr_t)jump_camera;
    /* Actor的init字段故意为空，B必须沿可用快捷绑定而不是假定Actor有档案资源。 */
    ptr(roles[0],0x308,NULL);
    record_actions=true;reject_qualification=false;reject_busy=false;
    g_input.connected=g_input.focused=true;g_intent.layer=LAYER_GAME;
    for(unsigned kind=0;kind<(expansion ? 4u:3u);++kind) {
        /* 外传第四个数据映射可代表瞬移；业务由原Method，控制器不改类型。 */
        test_aim_ms=kind==1 ? 2000:kind==2 ? 200:kind==3 ? 10000:1000;int duration=test_aim_ms;
        jump_builtin=kind==2;jump_fixture_selection=jump_builtin ? 10000:kind==3 ? 222:111;
        /* RT未设置不发动作；B仍独立解析原跳跃选择，并核对实际Method。 */
        memset(binding_data,0,sizeof binding_data);Write32(binding_data,0x18,'Q');
        Write32(binding_data,0x14,(uint32_t)jump_fixture_selection);Write32(binding_data,0x1C,kind%2);
        ptr(hud_data,0xC18,binding_data);ptr(roles[0],profile.active_offset,NULL);Combat_Reset();
        g_intent.layer=LAYER_SKILL;g_intent.pressed=KEY(PAD_A);g_intent.held=0;g_intent.lx=g_intent.ly=0;
        int rt_before=releases;Game_Update();CHECK(releases==rt_before); /* 未设置RT+A无动作，固定B仍独立使用原跳跃技能。 */int rt_method=jump_builtin ? 10000:jump_fixture_selection==222 ? 1002:1001;
        ptr(roles[0],profile.active_offset,NULL);Combat_Reset();Feedback_End();g_intent.layer=LAYER_GAME;
        ptr(roles[0],profile.active_offset,NULL);Write32(roles[0],0x73,1);Combat_Reset();
        Write32(roles[0],0x73,0x0B);g_input.now=100;g_intent.lx=1;g_intent.ly=0;g_intent.pressed=g_intent.held=KEY(PAD_B);
        int before=releases;unsigned old_moves=move_requests;Game_Update();POINT first,last;
        CHECK(Game_JumpAnchor(&first) && releases==before && move_requests==old_moves && Read32(roles[0],0x73)==1);
        int selection,icon;CHECK(Feedback_Selection(&selection,&icon) && selection==jump_fixture_selection);
        g_intent.pressed=0;g_input.now=100+(unsigned)duration/2;test_aim_ms=duration==10000 ? 100:10000;
        Game_Update();CHECK(Game_JumpAnchor(&last) && last.x==34 && last.y==0); /* 当前预览固定原耗时 */
        g_input.now=100+(unsigned)duration;Game_Update();CHECK(Game_JumpAnchor(&last));POINT maximum=last;
        g_input.now=200+(unsigned)duration*2;Game_Update();CHECK(Game_JumpAnchor(&last) && last.x==maximum.x && last.y==maximum.y);
        /* 松键当帧故意改杆方向和时钟，提交必须仍是最后显示的落点。 */
        WorldPoint shown;jump_projection(jump_camera,&shown,maximum.x,maximum.y);
        g_intent.held=0;g_intent.lx=-1;g_input.now=9000;Game_Update();CHECK(!Game_JumpAnchor(&last) && releases==before+1);
        CHECK(last_opcode==10 && arg1==rt_method && last_target==NULL);
        CHECK(arg2==shown.x && arg3==shown.y);
        CHECK(Feedback_Selection(&selection,&icon) && selection==jump_fixture_selection);
        ptr(roles[0],profile.active_offset,NULL);Feedback_RuntimeEnded();animation_done=true;++engine_tick;
        CHECK(!Feedback_Selection(&selection,&icon));animation_done=false;
    }
    jump_builtin=false;test_aim_ms=1000;
    /* 预览时切组合层、失焦或世界失效，取消而非自动补跳；B菜单用途不受影响。 */
    ptr(roles[0],profile.active_offset,NULL);g_intent.pressed=g_intent.held=KEY(PAD_B);g_input.now=5000;
    Game_Update();int before=releases;g_intent.layer=LAYER_DUAL;g_intent.pressed=0;Game_Update();
    POINT point;CHECK(!Game_JumpAnchor(&point) && releases==before);
    Game_Release();g_intent.layer=LAYER_GAME;jump_fixture_selection=0xFFFF;Write32(binding_data,0x14,0xFFFF);g_intent.pressed=KEY(PAD_B);Game_Update();CHECK(!Game_JumpAnchor(&point));
    jump_fixture_selection=111;Write32(binding_data,0x14,111);g_intent.pressed=0;g_intent.held=0;Game_Update();
    record_actions=false;g_intent.lx=g_intent.ly=0;Combat_Reset();Game_Release();
}
int main(int argc,char **argv)
{
    if(argc>1 && (!strcmp(argv[1],"--jump") || !strcmp(argv[1],"--jump-zero"))){jump_zero_range=!strcmp(argv[1],"--jump-zero");jump_regression(false);jump_regression(true);printf("两作基础跳跃映射、预览/松开/取消与既有图标回放通过：%u项\n",checks);return 0;}
#ifndef EDSLASH_REAL_CONFIG
    new_combat_regression(false);new_combat_regression(true);
#endif
    readiness_regression(false);readiness_regression(true);
    exercise(false);exercise(true);
    move_obstacle_regression(false);move_obstacle_regression(true);
    combat_regression(false);combat_regression(true);
    chain_regression(false);chain_regression(true);
    transfer_regression(false);transfer_regression(true);
    guard_regression(false);guard_regression(true);
    quick_cast_regression(false);quick_cast_regression(true);
    feedback_regression(false);feedback_regression(true);
    configured_binding_regression(false);configured_binding_regression(true);
    printf("原生路由、双版本偏移与调用约定检查通过：%u 项\n",checks);
    return 0;
}

// 性能替身不改变生产业务或原有测试顺序。
void RuntimePerf_Initialize(void) {}
int64_t RuntimePerf_Begin(void) {return 0;}
void RuntimePerf_End(RuntimePerfId id,int64_t begin) {(void)id;(void)begin;}
void RuntimePerf_FrameEnd(void) {}
