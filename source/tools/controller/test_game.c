#include "Plugin.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    if (opcode==1 || opcode==2) { Write32(roles[0],0x73,0x0B); Write32(mouse_data,0x90,a);Write32(mouse_data,0x94,b); }
    return 1;
}
static int __attribute__((thiscall)) native_stop(void *self,int state,int a,int b,int c)
{ CHECK(self==roles[0] && state==1 && a==0 && b==0 && c==0);Write32(self,0x73,1);return 1; }
static int __attribute__((thiscall)) native_release(void *self,int method,int policy,void *target)
{ CHECK(self==mouse_data && (method==111 || method==222));++releases;last_policy=policy;last_target=target;return 1; }
static int __attribute__((thiscall)) native_left(void *self) { CHECK(self==hud_data);return 111; }
static int __attribute__((thiscall)) native_right(void *self) { CHECK(self==hud_data);return 222; }
static int __attribute__((thiscall)) native_select(void *self,int value) { CHECK(self==hud_data);selected=value;return 1; }
static int __attribute__((thiscall)) native_quick(void *self,int value) { CHECK(self==hud_data);quick_slot=value;return 1; }
static int __attribute__((thiscall)) native_inspect_gate(void *self,int handle) { CHECK(self==manager_data && handle==3);return busy_gate; }
static int __attribute__((thiscall)) native_hover(void *self,int handle) { CHECK(self==manager_data && handle==3);return 1; }
static int __cdecl native_relation(void *a,void *b) { CHECK(a==roles[0] && b==roles[1]);return 1; }
static int __attribute__((thiscall)) native_template(void *self,int a,int b) { CHECK(self==record_data && a==0x1F && b==1);return 0xFFFF; }
static int __attribute__((thiscall)) native_jm(void *self,int id)
{ CHECK(self==ui_data);return page_visible && id==0x94 ? (int)(uintptr_t)page_data:0; }
static int __attribute__((thiscall)) native_property(void *self,int index)
{ CHECK(self==record_data && index==0x0D);return page_visible ? 1:0; }

static void configure(Profile *profile, bool expansion)
{
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
    profile->invalid_offset=expansion?0x446:0x43A;profile->interact_offset=expansion?0x32D:0x321;
    profile->active_offset=expansion?0x359:0x34D;
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
    Write32(page_data,0x64,1);
    memset(&g_input,0,sizeof g_input);g_input.connected=g_input.focused=true;g_input.now=1000;
    memset(&g_intent,0,sizeof g_intent);g_intent.layer=LAYER_GAME;
    page_visible=physical_mouse=busy_gate=false;releases=0;last_opcode=0;
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
    g_intent.run=true;Game_Update();CHECK(last_opcode==3 && arg1==1);
    g_intent.lx=0;Game_Update();CHECK(Read32(roles[0],0x73)==1);
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

int main(void)
{
    exercise(false);exercise(true);
    printf("原生路由、双版本偏移与调用约定检查通过：%u 项\n",checks);
    return 0;
}
