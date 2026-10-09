/* 真实Inspect/Game/Control及32位游戏对象回放；只验证原事件与隔离，不代替地图脚本实机。 */
#define main baseline_game_main
#include "test_game.c"
#undef main
#include "Inspect.h"

static BYTE inspect_map[0x70],changed_map[0x70],inspect_cells[9*9*19];
static BYTE static_objects[3][0xC0],extra_actor[0x500],hover_code[8];
static unsigned packets,portal_packets,original_hover_calls;
static bool block_inspect,allow_portal;
static void *map_pointer;
static unsigned static_checks;
static int __fastcall static_gate(void *o, void *unused_edx) { (void)unused_edx; ++static_checks;return Read32(o,0xBC)==0 || Read32(o,0xBC)==2; }
static int __fastcall basic_get(void *role, void *unused_edx,int i) { (void)unused_edx;CHECK(role==roles[0] && i==1);return 111;}
static int __fastcall portal_gate(void *object, void *unused_edx)
{ (void)unused_edx; CHECK(object==static_objects[0] || object==static_objects[1] || object==static_objects[2]);return allow_portal; }
static BYTE *grid_cell(unsigned x,unsigned y) { return inspect_cells+(y*9+x)*19; }
static int __fastcall inspect_submit(void *self, void *unused_edx,int opcode,int a,int b,int c)
{ (void)unused_edx;
    ++packets;native_submit(self, NULL,opcode,a,b,c);
    if (opcode==20) {CHECK(a==5 && b==4 && c==7);++portal_packets;Write32(roles[0],g_profile->pending_offset,20);Write32(roles[0],g_profile->pending_offset+12,(uint32_t)c);}
    return 1;
}
static int __fastcall inspect_hover(void *self, void *unused_edx,int handle)
{ (void)unused_edx; CHECK(self==manager_data && handle>=0 && handle<=7);Write32(self,4,(uint32_t)handle);return 1; }
static int __fastcall inspect_gate(void *self, void *unused_edx,int handle)
{ (void)unused_edx; CHECK(self==manager_data && (handle==3 || handle==4));return block_inspect; }
static int __fastcall original_hover(void *self, void *unused_edx,int event,int x,void *y)
{ (void)unused_edx;
    CHECK(self==mouse_data && event==11 && x==22 && y==(void *)33);++original_hover_calls;
    Write32(manager_data,4,7);return 9;
}
static void put_static(unsigned index,unsigned subtype,unsigned x,unsigned y)
{
    memset(inspect_cells,0xFF,sizeof inspect_cells);
    for (unsigned i=0;i<3;++i) Write32(static_objects[i],0xBC,0);
    Write32(static_objects[index],0x54,subtype);Write32(static_objects[index],0xBC,0);
    BYTE *c=grid_cell(x,y);Write32(c,0x0C,index+5);c[0x10]=0x88;
}
static void point_actor(unsigned index,int dx,int dy)
{
    BYTE *r=index==4 ? extra_actor:roles[index-1];
    Write32(r,0x2C,288+dx);Write32(r,0x30,288+dy);
}
static void update(void)
{ g_input.now+=20;g_intent.pressed=0;g_intent.held=0;Inspect_Update(roles[0]); }
static void fixture(Profile *profile,bool expansion)
{
    configure(profile,expansion);block_inspect=allow_portal=false;profile->inspect_portal_gate=(uintptr_t)portal_gate;profile->inspect_static_gate=(uintptr_t)static_gate;profile->inspect_basic_get=(uintptr_t)basic_get;
    map_pointer=inspect_map;profile->inspect_map_global=(uintptr_t)&map_pointer;packets=portal_packets=original_hover_calls=0;
    memset(inspect_map,0,sizeof inspect_map);memset(changed_map,0,sizeof changed_map);
    memset(static_objects,0,sizeof static_objects);memset(extra_actor,0,sizeof extra_actor);
    memset(inspect_cells,0xFF,sizeof inspect_cells);
    Write32(inspect_map,8,9);Write32(inspect_map,12,9);ptr(inspect_map,0x14,inspect_cells);
    memcpy(changed_map,inspect_map,sizeof inspect_map);ptr(roles[0],0x6F,inspect_map);
    Write32(roles[0],0x2C,288);Write32(roles[0],0x30,288);Write32(roles[0],0x14B,0);
    point_actor(2,64,0);point_actor(3,128,0);
    for (unsigned i=0;i<3;++i) {
        Write32(static_objects[i],0x14,i+5);Write32(static_objects[i],0x67,0x0C);
        ptr(table_data,(i+5)*6+2,static_objects[i]);
    }
    Write32(extra_actor,0x14,4);Write32(extra_actor,0x67,0x3C);
    Write32(extra_actor,profile->interact_offset,1);Write32(extra_actor,profile->inspect_ready_offset,1);
    point_actor(4,64,0);ptr(table_data,4*6+2,extra_actor);ptr(roles[2],8,extra_actor);
    profile->hover_set=(uintptr_t)inspect_hover;profile->inspect_gate=(uintptr_t)inspect_gate;
    profile->submit=(uintptr_t)inspect_submit;profile->inspect_hover=(uintptr_t)original_hover;
    make_call(hover_code,(uintptr_t)original_hover);profile->inspect_hover_call=(uintptr_t)hover_code;
    CHECK(Inspect_Initialize());
}
static void regression(bool expansion)
{
    Profile profile;fixture(&profile,expansion);
    /* 同距前方优先、非常近的身后优于远前方，360度不会变成强制只选前面。 */
    point_actor(3,-64,0);point_actor(4,64,0);update();CHECK(Read32(manager_data,4)==4);
    point_actor(3,-16,0);update();CHECK(Read32(manager_data,4)==3);
    point_actor(3,128,0);point_actor(4,64,0);Inspect_Reset();
    /* 动态调查按真实route/ready资格，0x3C类也可交互，不再只准0x28。 */
    update();CHECK(Read32(manager_data,4)==4);
    Write32(extra_actor,profile.inspect_ready_offset,0);update();CHECK(Read32(manager_data,4)==3);
    point_actor(3,-64,0);update();CHECK(Read32(manager_data,4)==3); /* 身后近身仍可调查 */
    point_actor(3,64,64);update();CHECK(Read32(manager_data,4)==3); /* 正好45度边界 */
    point_actor(3,32,64);update();CHECK(Read32(manager_data,4)==3); /* 不再按角度排除 */
    Write32(roles[0],0x14B,2);update();CHECK(Read32(manager_data,4)==3); /* 松杆后按实际正面 */
    point_actor(3,64,0);update();CHECK(Read32(manager_data,4)==3);
    Write32(roles[0],0x14B,0);point_actor(3,64*3,0);update();CHECK(Read32(manager_data,4)==0);
    point_actor(3,64,0);Write32(roles[2],profile.invalid_offset,1);update();CHECK(Read32(manager_data,4)==0);
    Write32(roles[2],profile.invalid_offset,0);update();
    block_inspect=true;unsigned before=packets;Inspect_Activate();CHECK(packets==before);
    block_inspect=false;Inspect_Activate();CHECK(last_opcode==19 && arg1==3);
    Write32(roles[2],profile.inspect_ready_offset,0);
    /* 半径边界独立于方形格范围，默认160，上限480；原NPC格差门继续有效。 */
    Write32(roles[2],profile.inspect_ready_offset,1);point_actor(3,128,64);update();CHECK(Read32(manager_data,4)==3);
    test_inspect_distance=64;update();CHECK(Read32(manager_data,4)==0);
    test_inspect_distance=160;point_actor(3,64,0);Write32(roles[2],profile.inspect_ready_offset,0);
    /* 静态87/88来自地图格，原鼠标候选故意指向另一个对象也不影响结果。 */
    put_static(0,0x87,5,4);Write32(grid_cell(6,3),0x0C,5);grid_cell(6,3)[0x10]=0x88;
    ptr(mouse_data,0x40,static_objects[1]);static_checks=0;update();CHECK(static_checks==1);CHECK(Read32(manager_data,4)==5);
    Inspect_Activate();CHECK(last_opcode==23 && arg1==5);
    BYTE mouse_copy[sizeof mouse_data];memcpy(mouse_copy,mouse_data,sizeof mouse_data);
    CHECK(((This3)patched_callee((uintptr_t)hover_code))(mouse_data, NULL,11,22,(void *)33)==0);
    CHECK(original_hover_calls==0 && Read32(manager_data,4)==5 && !memcmp(mouse_copy,mouse_data,sizeof mouse_data));
    Write32(static_objects[0],0xBC,2);update();CHECK(Read32(manager_data,4)==5);
    Write32(static_objects[0],0xBC,1);update();CHECK(Read32(manager_data,4)==0);
    put_static(1,0x88,5,4);update();CHECK(Read32(manager_data,4)==6);Inspect_Activate();CHECK(last_opcode==23 && arg1==6);
    put_static(0,0x87,6,4);update();CHECK(Read32(manager_data,4)==5); /* 原邻接门之外 */
    put_static(0,0x87,3,4);update();CHECK(Read32(manager_data,4)==5); /* 身后近身也可选 */
    put_static(0,0x96,5,4);update();CHECK(Read32(manager_data,4)==0);
    put_static(0,0x80,5,4);update();CHECK(Read32(manager_data,4)==5);Inspect_Activate();CHECK(last_opcode==10 && arg1==1001 && arg2==352 && arg3==288);Combat_Reset();
    put_static(0,0x87,5,4);grid_cell(5,4)[0x10]=0;update();CHECK(Read32(manager_data,4)==0);
    /* 掉落句柄在E，不能套静态flags88；A沿原21，不受QOL过滤设置限制。 */
    put_static(0,0x87,5,4);Write32(static_objects[0],0x67,0x17);Write32(grid_cell(5,4),0x0E,5);grid_cell(5,4)[0x10]=0;
    update();CHECK(Read32(manager_data,4)==5);Inspect_Activate();CHECK(last_opcode==21 && arg1==5);
    Write32(static_objects[0],0x67,0x0C);
    /* 89仍是手动24，不误当出口；96必须原TransGo资格通过才走换区20。 */
    put_static(2,0x89,5,4);update();Inspect_Activate();CHECK(last_opcode==24 && portal_packets==0);
    put_static(2,0x96,5,4);update();CHECK(Read32(manager_data,4)==0);
    allow_portal=true;update();CHECK(Read32(manager_data,4)==7 && portal_packets==0);
    g_intent.lx=1;g_input.lx=1;
    for (unsigned i=0;i<20;++i) {g_input.now+=20;CHECK(!Inspect_Update(roles[0]));}
    CHECK(portal_packets==0); /* 走进原区域绝不自动读图 */
    Inspect_Activate();CHECK(portal_packets==1 && last_opcode==20 && arg1==5 && arg2==4 && arg3==7);
    CHECK(Inspect_Update(roles[0])); /* 只维护已经由A提交的原pending */
    g_intent.lx=-1;CHECK(!Inspect_Update(roles[0])); /* 新方向立即取消插件保护 */
    g_intent.lx=1;Inspect_Activate();CHECK(portal_packets==2);
    g_input.now+=700;Write32(roles[0],profile.pending_offset,(uint32_t)-1);Write32(roles[0],profile.pending_offset+12,0);
    CHECK(!Inspect_Update(roles[0]) && portal_packets==2);
    put_static(2,0x96,6,4);Inspect_Update(roles[0]);Inspect_Activate();CHECK(portal_packets==2); /* 远处聚焦不等于进入 */
    put_static(2,0x96,5,4);map_pointer=changed_map;Inspect_Update(roles[0]);CHECK(portal_packets==2);
    Write32(world_data,0x58,0);Inspect_Reset();Write32(world_data,0x58,1);Inspect_Update(roles[0]);CHECK(portal_packets==2);
    /* 物理来源恢复原WorldMouseMove，清理不得撤掉其它来源的新悬停。 */
    Inspect_Reset();g_intent.layer=LAYER_NATIVE;
    CHECK(((This3)patched_callee((uintptr_t)hover_code))(mouse_data, NULL,11,22,(void *)33)==9);
    CHECK(original_hover_calls==1 && Read32(manager_data,4)==7);Inspect_Reset();CHECK(Read32(manager_data,4)==7);
    g_input.connected=false;CHECK(!Inspect_Update(roles[0]));
    Inspect_Shutdown();CHECK(patched_callee((uintptr_t)hover_code)==(uintptr_t)original_hover);
    HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
int main(void)
{
    regression(false);regression(true);
    printf("两作调查扇区、动态/静态资格、原悬停及区域进入回放通过：%u项\n",checks);return 0;
}
