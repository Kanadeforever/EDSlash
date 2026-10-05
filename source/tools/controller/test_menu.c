/* 复用32位真实游戏适配fixture，新增真正Menu.c的虚表包装与Control/Game隔离回放。
 * 不复制生产路由/导航算法，不读取玩家进程，也不把宿主回放当成实机通过。 */
#define Memory_Patch baseline_memory_patch
#define main baseline_game_main
#include "test_game.c"
#undef main
#undef Memory_Patch
#include "Menu.h"
#include "Cursor.h"

static BYTE menu_roots[7][0x300],menu_children[7][6][0xE4],menu_sprites[7][6][4*32];
static BYTE unknown_root[0xD0],menu_resource[16];
static uintptr_t menu_tables[7][26];
static ControlState menu_control;
static bool expansion_case,transition_after_action;
static unsigned title_actions,system_actions,confirm_actions,native_hovers,tick_calls,animation_resets;
static unsigned selected_action,pending_ticks;
static unsigned patch_attempt,patch_fail_at;
static unsigned focus_sounds,load_actions,cursor_draws,delete_requests,deleted_records;
static void *message_pointer;
static BYTE talk_picker_code[8];
static unsigned talk_selected,talk_cancelled,text_next_count,talk_delay;
static int __attribute__((thiscall)) talk_picker(void *root) {CHECK(root==menu_roots[5]);return (int)(uintptr_t)menu_children[5][1];}
static int __attribute__((thiscall)) talk_select(void *root) {CHECK(root==menu_roots[5]);talk_selected=Read32(ReadPtr(root,0xA8),0xC4);return 1;}
static int __attribute__((thiscall)) talk_cancel(void *root,int e,int x,void *y) {CHECK(root==menu_roots[5] && !e && !x && !y);++talk_cancelled;Write32(root,0x64,0);ptr(ui_data,0x3C,NULL);return 1;}
static int __attribute__((thiscall)) text_next(void *root) {CHECK(root==menu_roots[6]);++text_next_count;Write32(root,0x64,0);ptr(ui_data,0x3C,NULL);return 1;}
static BYTE load_nodes[6][12],load_data[6][0x40],cursor_code[24];
static uintptr_t cursor_position_pointer;
static int cursor_x,cursor_y;
/* 故障只注入自己的fixture写槽，验证安装中途失败可以逐槽退回原函数。 */
bool Memory_Patch(void *target,const void *data,size_t bytes)
{
    if (++patch_attempt==patch_fail_at) return false;
    return baseline_memory_patch(target,data,bytes);
}

static int root_kind(void *self)
{
    for (int i=0;i<7;++i) if (self==menu_roots[i]) return i;
    CHECK(false);return -1;
}
static int __attribute__((thiscall)) menu_property(void *self,int field)
{
    if (self!=menu_resource) return native_property(self,field);
    return field==13 ? 1:field==2 ? 3:0;
}
static int __attribute__((thiscall)) menu_show(void *self,int active,int mode)
{
    int k=root_kind(self);CHECK(mode==0 || mode==-1);
    Write32(self,0x64,active!=0);Write32(self,0x68,0);
    if (k==2 || k==4 || k==5 || k==6) ptr(ui_data,0x3C,active ? self:NULL);
    if (k==6 && active) {Write32(self,0xF0,1);Write32(self,0xF4,(unsigned)-20);}
    if (k==3 && active) {Write32(self,0xD0,0);Write32(self,0xD4,0);}
    return 1;
}
static int __attribute__((thiscall)) menu_tick(void *self)
{
    int k=root_kind(self);++tick_calls;
    /* 模拟原base Tick因鼠标离开根页而清悬停；生产wrapper必须重新投影自己的选择。 */
    ptr(self,0xA8,NULL);
    if (k==5) ptr(self,0xA8,(void *)(uintptr_t)((This0)patched_callee((uintptr_t)talk_picker_code))(self));
    if (k==6 && Read32(self,0x64)) Write32(self,0xF4,Read32(self,0xF4)-Read32(self,0xF0));
    if (k==0 && pending_ticks) {
        for (unsigned i=0;i<6;++i)
            if (Read32(menu_children[0][i],0x28)==selected_action) Write32(menu_children[0][i],0x40,3);
        if (!--pending_ticks) {
            Write32(self,0xC0,(uint32_t)-1);
            if (transition_after_action) ((This2)menu_tables[k][0x1C/4])(self,0,0);
        }
    }
    return 7;
}
static int __attribute__((thiscall)) menu_hover(void *self,int e,int x,void *y)
{
    int k=root_kind(self);++native_hovers;
    CHECK(e==12 && x==34 && y==(void *)56);
    ptr(self,0xA8,menu_children[k][1]);return 9;
}
static int __attribute__((thiscall)) menu_activate(void *self,int id)
{
    CHECK(root_kind(self)==0);++title_actions;selected_action=(unsigned)id;
    if (!expansion_case) { Write32(self,0xC0,id);pending_ticks=3; }
    else if (transition_after_action) ((This2)menu_tables[0][0x1C/4])(self,0,0);
    return 1;
}
static int __attribute__((thiscall)) menu_primary(void *self,int e,int x,void *y)
{
    int kind=root_kind(self);
    CHECK((kind==1 && e==0 && x==0 && y==NULL) || (kind==3 && e==0 && x==-1 && y==(void *)(intptr_t)-1));
    void *child=ReadPtr(self,0xA8);CHECK(ReadPtr(child,0xA4)==self);
    selected_action=Read32(child,0x28);
    if (kind==3) {
        CHECK(selected_action==0x97);((This2)menu_tables[3][0x1C/4])(self,0,0);
        ((This2)menu_tables[0][0x1C/4])(menu_roots[0],1,0);
    } else {
        ++system_actions;
        if (transition_after_action) ((This2)menu_tables[1][0x1C/4])(self,0,0);
    }
    return 1;
}
static int __attribute__((thiscall)) menu_confirm(void *self)
{
    CHECK(root_kind(self)==2);++confirm_actions;
    /* 原回调解析原数值文本；菜单适配不能绕过回调直接改owner/金钱。 */
    CHECK(Read32(menu_children[2][2],0xC8)==42);
    ((This2)menu_tables[2][0x1C/4])(self,0,0);return 1;
}
static int __attribute__((thiscall)) menu_texture(void *self,int x,int y)
{
    CHECK(ReadPtr(self,0xA4)==menu_roots[1]);CHECK((x==-1 || x==0) && y==0);
    Write32(self,0x58,x==-1 ? 77u:0u);return 1;
}
static int __attribute__((thiscall)) menu_animation_reset(void *self,int zero)
{
    CHECK(Memory_Readable(self,32));CHECK(zero==0);++animation_resets;return 1;
}
static int __cdecl menu_sound(int id,int a,int volume,int b)
{ CHECK(id==0x93 && a==0 && volume==100 && b==0);++focus_sounds;return 1; }
static int __attribute__((thiscall)) load_select(void *self,int slot)
{
    CHECK(root_kind(self)==3 && slot>=0 && slot<4);
    if (Read32(self,0xD0)+(unsigned)slot>=Read32(self,0xC0)) return 0;
    Write32(self,0xD4,(unsigned)slot);return 1;
}
static int __attribute__((thiscall)) load_page(void *self,int delta)
{
    CHECK(root_kind(self)==3 && (delta==-4 || delta==4));int next=(int)Read32(self,0xD0)+delta;
    if (next>=0 && (unsigned)next<Read32(self,0xC0)) Write32(self,0xD0,(unsigned)next);
    return 1;
}
static int __attribute__((thiscall)) load_submit(void *self,int slot)
{
    CHECK(root_kind(self)==3 && slot==-1 && Read32(self,0xD0)+Read32(self,0xD4)<Read32(self,0xC0));
    ++load_actions;((This2)menu_tables[3][0x1C/4])(self,0,0);return 1;
}
static int __attribute__((thiscall)) menu_template(void *self,int index,int mode)
{
    if (self!=menu_resource) return native_template(self,index,mode);
    CHECK(mode==1 && index>=5 && index<=8);static const int rect[]={40,60,300,50};return rect[index-5];
}
static int __attribute__((thiscall)) message_lookup(void *table,int id,int language)
{ CHECK(table==(void *)0x345 && id==0x13D && language==1);return (int)(uintptr_t)"原删除提示"; }
static int __attribute__((thiscall)) message_open(void *self,void *owner,const char *text,int x,int y,int a,int b)
{
    CHECK(root_kind(self)==4 && owner==menu_roots[3] && !strcmp(text,"原删除提示") && x>=0 && y>=0 && a==0 && b==0);
    ++delete_requests;ptr(self,0xCC,owner);((This2)menu_tables[4][0x1C/4])(self,1,0);return 1;
}
static int __attribute__((thiscall)) message_primary(void *self,int e,int x,void *y)
{
    CHECK(root_kind(self)==4 && e==0 && x==0 && y==NULL);
    void *child=ReadPtr(self,0xA8),*owner=ReadPtr(self,0xCC);
    CHECK(owner==menu_roots[3] && Read32(child,0x28)==0x2A && Read32(child,0xC4)<=1);
    bool yes=Read32(child,0xC4)==1;((This2)menu_tables[4][0x1C/4])(self,0,0);ptr(self,0xCC,NULL);
    if (yes) {++deleted_records;Write32(owner,0xC0,Read32(owner,0xC0)-1);}
    return 1;
}
static BOOL WINAPI cursor_position(POINT *point) { point->x=37;point->y=49;return TRUE; }
static int __attribute__((thiscall)) cursor_sprite(void *self,void *surface,int x,int y,int frame,int shade,int flags)
{
    CHECK(self==menu_sprites[0][0] && surface==(void *)0x246 && frame==0 && shade==-1 && flags==0);
    ++cursor_draws;cursor_x=x;cursor_y=y;return 6;
}
static void cursor_fixture(Profile *profile)
{
    memset(cursor_code,0x90,sizeof cursor_code);cursor_position_pointer=(uintptr_t)cursor_position;
    cursor_code[0]=0xFF;cursor_code[1]=0x15;uintptr_t iat=(uintptr_t)&cursor_position_pointer;
    memcpy(cursor_code+2,&iat,4);make_call(cursor_code+8,(uintptr_t)cursor_sprite);make_call(cursor_code+16,(uintptr_t)cursor_sprite);
    profile->cursor_position_call=(uintptr_t)cursor_code;profile->cursor_position_iat=iat;
    profile->cursor_sprite_call1=(uintptr_t)(cursor_code+8);profile->cursor_sprite_call2=(uintptr_t)(cursor_code+16);
    profile->cursor_sprite_draw=(uintptr_t)cursor_sprite;CHECK(Cursor_Initialize());cursor_draws=0;
}
static void activate_page(unsigned kind)
{
    for (unsigned k=0;k<7;++k) if (k!=kind) Write32(menu_roots[k],0x64,0);
    Write32(unknown_root,0x64,0);
    ((This2)menu_tables[kind][0x1C/4])(menu_roots[kind],1,0);
}
static void menu_step(unsigned held,float x,float y)
{
    g_input.now+=20;g_input.buttons=held;g_input.lx=x;g_input.ly=y;g_input.menu=Game_Menu();
    g_intent=Control_Step(&menu_control,&g_input);
    Menu_Update();
}
static void neutral_menu(void) { menu_step(0,0,0);menu_step(0,0,0); }
static unsigned focus_id(unsigned kind) { return Read32(ReadPtr(menu_roots[kind],0xA8),0x28); }
static void game_isolated(void)
{
    BYTE keys[256]={0};Game_Keyboard(keys);
    for (unsigned i=0;i<256;++i) CHECK(keys[i]==0);
    unsigned old_releases=(unsigned)releases;last_opcode=0;Game_Update();
    CHECK((unsigned)releases==old_releases && last_opcode==0);
}
static void menu_fixture(Profile *profile,bool expansion)
{
    configure(profile,expansion);expansion_case=expansion;
    memset(menu_roots,0,sizeof menu_roots);memset(menu_children,0,sizeof menu_children);
    memset(menu_tables,0,sizeof menu_tables);memset(unknown_root,0,sizeof unknown_root);
    memset(&menu_control,0,sizeof menu_control);pending_ticks=0;
    title_actions=system_actions=confirm_actions=native_hovers=tick_calls=animation_resets=0;
    transition_after_action=false;selected_action=0;
    static const unsigned ids[7][6]={{0x1F,0x20,0x21,0x22,0x23,0xA7},
        {0x2E,0x30,0x31,0x2F,0,0},{0x9A,0x9B,0x99,0,0,0},{0x97,0x96,0xA2,0xA3,0,0},{0x2A,0x2A,0x2A,0,0,0},{0x2A,0x2A,0x2A,0,0,0},{0,0,0,0,0,0}};
    for (unsigned k=0;k<7;++k) {
        ptr(menu_roots[k],0,menu_tables[k]);ptr(menu_roots[k],0x50,menu_resource);
        Write32(menu_roots[k],0x28,k==0 ? 0x19:k==1 ? 0x2D:k==2 ? 0x98:k==3 ? 0x94:k==4 ? 0x29:k==5 ? 0x28:0x2C);
        ptr(menu_roots[k],0x0C,k<6 ? menu_roots[k+1]:NULL);
        ptr(menu_roots[k],0x9C,menu_children[k][0]);
        menu_tables[k][1]=(uintptr_t)menu_tick;menu_tables[k][0x1C/4]=(uintptr_t)menu_show;
        menu_tables[k][0x30/4]=(uintptr_t)menu_hover;
        menu_tables[k][0x24/4]=(uintptr_t)menu_primary;menu_tables[k][0x3C/4]=(uintptr_t)menu_confirm;
        for (unsigned j=0;j<6;++j) {
            BYTE *c=menu_children[k][j];ptr(c,0xA4,menu_roots[k]);
            ptr(c,8,j<5 ? menu_children[k][j+1]:NULL);
            Write32(c,0x28,ids[k][j]);Write32(c,0x64,ids[k][j]!=0);
            Write32(c,0x14,(k==2 || k==4) ? j*100:100);Write32(c,0x18,(k==2 || k==4) ? 200:j*50+50);
            Write32(c,0x1C,80);Write32(c,0x20,30);Write32(c,0x44,4);ptr(c,0x48,menu_sprites[k][j]);
            Write32(c,0xC8,42);
            if (k==4) Write32(c,0xC4,j<2 ? j:(unsigned)-1);
            if (k==5) Write32(c,0xC4,j<2 ? (j+1)*10:(unsigned)-1);
        }
    }
    if (!expansion) Write32(menu_children[0][5],0x64,0);
    Write32(menu_roots[0],0xC0,(uint32_t)-1);
    profile->menu_title_vtable=(uintptr_t)menu_tables[0];profile->menu_system_vtable=(uintptr_t)menu_tables[1];
    profile->menu_confirm_vtable=(uintptr_t)menu_tables[2];
    profile->menu_title_tick=profile->menu_system_tick=profile->menu_confirm_tick=(uintptr_t)menu_tick;
    profile->menu_title_show=profile->menu_system_show=profile->menu_confirm_show=(uintptr_t)menu_show;
    profile->menu_title_hover=profile->menu_system_hover=profile->menu_confirm_hover=(uintptr_t)menu_hover;
    profile->menu_system_primary=(uintptr_t)menu_primary;profile->menu_confirm_submit=(uintptr_t)menu_confirm;
    profile->menu_message_vtable=(uintptr_t)menu_tables[4];message_pointer=menu_roots[4];
    profile->menu_message_global=(uintptr_t)&message_pointer;profile->menu_message_open=(uintptr_t)message_open;
    profile->menu_message_text_lookup=(uintptr_t)message_lookup;profile->menu_message_text_table=0x345;
    profile->menu_message_tick=(uintptr_t)menu_tick;profile->menu_message_show=(uintptr_t)menu_show;
    profile->menu_message_hover=(uintptr_t)menu_hover;profile->menu_message_primary=(uintptr_t)message_primary;
    profile->menu_message_base_tick=(uintptr_t)menu_tick;menu_tables[4][0x24/4]=(uintptr_t)message_primary;
    delete_requests=deleted_records=0;
    profile->menu_talk_vtable=(uintptr_t)menu_tables[5];profile->menu_text_vtable=(uintptr_t)menu_tables[6];
    profile->menu_talk_tick=profile->menu_text_tick=(uintptr_t)menu_tick;
    profile->menu_talk_show=profile->menu_text_show=(uintptr_t)menu_show;
    profile->menu_talk_hover=profile->menu_text_hover=(uintptr_t)menu_hover;
    profile->menu_talk_delay_global=(uintptr_t)&talk_delay;talk_delay=0;Write32(menu_roots[5],0x2C4,0);
    profile->menu_talk_select=(uintptr_t)talk_select;profile->menu_talk_cancel=(uintptr_t)talk_cancel;profile->menu_text_next=(uintptr_t)text_next;
    make_call(talk_picker_code,(uintptr_t)talk_picker);profile->menu_talk_picker_call=(uintptr_t)talk_picker_code;profile->menu_talk_picker=(uintptr_t)talk_picker;
    talk_selected=talk_cancelled=text_next_count=0;
    Write32(menu_roots[6],0xD4,10);Write32(menu_roots[6],0xD8,20);Write32(menu_roots[6],0xDC,100);Write32(menu_roots[6],0xE0,40);
    profile->menu_load_vtable=(uintptr_t)menu_tables[3];
    profile->menu_load_tick=(uintptr_t)menu_tick;profile->menu_load_show=(uintptr_t)menu_show;profile->menu_load_hover=(uintptr_t)menu_hover;
    profile->menu_load_primary=(uintptr_t)menu_primary;profile->menu_load_select=(uintptr_t)load_select;
    profile->menu_load_page=(uintptr_t)load_page;profile->menu_load_submit=(uintptr_t)load_submit;profile->menu_sound=(uintptr_t)menu_sound;
    profile->template_value=(uintptr_t)menu_template;focus_sounds=load_actions=0;
    profile->menu_title_activate=(uintptr_t)menu_activate;profile->menu_texture=(uintptr_t)menu_texture;
    profile->menu_animation_reset=(uintptr_t)menu_animation_reset;profile->ui_property=(uintptr_t)menu_property;
    ptr(ui_data,0x18,menu_roots[0]);ptr(ui_data,0x1C,menu_roots[6]);ptr(ui_data,0x20,(void *)0x12345678);
    ptr(unknown_root,0x50,menu_resource);Write32(unknown_root,0x28,0xAA);
    Write32(menu_roots[3],0xC0,6);ptr(menu_roots[3],0xC4,load_nodes[0]);
    for (unsigned i=0;i<6;++i) {
        ptr(load_nodes[i],0,i<5 ? load_nodes[i+1]:NULL);ptr(load_nodes[i],8,load_data[i]);
    }
    patch_attempt=patch_fail_at=0;CHECK(Menu_Initialize());cursor_fixture(profile);
}
static void menu_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);
    /* 标题没有世界和玩家，菜单采样仍能导航和调用原生激活。 */
    void *old_world=world_ptr;world_ptr=NULL;activate_page(0);neutral_menu();
    CHECK(focus_id(0)==0x22);CHECK(Read32(menu_children[0][3],0x40)==1);CHECK(focus_sounds==1);
    POINT anchor;CHECK(Menu_CursorAnchor(&anchor));
    CHECK(anchor.x==174 && anchor.y==224);
    CHECK(((BOOL (WINAPI *)(POINT *))patched_callee((uintptr_t)cursor_code))(&anchor));CHECK(anchor.x==174 && anchor.y==224);
    typedef int (__attribute__((thiscall)) *Draw)(void *,void *,int,int,int,int,int);
    CHECK(((Draw)patched_callee((uintptr_t)(cursor_code+16)))(menu_sprites[0][0],(void *)0x246,anchor.x,anchor.y,0,-1,0)==6);
    CHECK(cursor_draws==1 && cursor_x==174 && cursor_y==224);
    menu_step(KEY(PAD_UP),0,0);CHECK(focus_id(0)==0x21);CHECK(focus_sounds==2);
    for (unsigned i=0;i<10;++i) menu_step(KEY(PAD_UP),0,0);
    CHECK(focus_id(0)==0x21);g_input.now+=160;menu_step(KEY(PAD_UP),0,0);CHECK(focus_id(0)==0x20);
    CHECK(focus_sounds==3);
    CHECK(((This3)menu_tables[0][0x30/4])(menu_roots[0],12,34,(void *)56)==0);
    CHECK(native_hovers==0 && focus_id(0)==0x20);
    CHECK(((This0)menu_tables[0][1])(menu_roots[0])==7 && focus_id(0)==0x20);
    CHECK(ReadPtr(ui_data,0x20)==(void *)0x12345678);
    Write32(unknown_root,0x64,1);ptr(ui_data,0x40,unknown_root);
    unsigned routed_reason;CHECK(Menu_Context(&routed_reason)==menu_roots[0]);
    Write32(unknown_root,0x64,0);ptr(ui_data,0x40,NULL);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(title_actions==1 && selected_action==0x20);
    for (unsigned i=0;i<7;++i) menu_step(KEY(PAD_A),0,0);
    CHECK(title_actions==1);game_isolated();
    if (!expansion) {
        ((This0)menu_tables[0][1])(menu_roots[0]);CHECK(Read32(menu_children[0][1],0x40)==3);
        neutral_menu();menu_step(KEY(PAD_DOWN)|KEY(PAD_A),0,0);CHECK(title_actions==1);
        ((This0)menu_tables[0][1])(menu_roots[0]);((This0)menu_tables[0][1])(menu_roots[0]);
    }
    neutral_menu();Write32(menu_children[0][1],0x64,0);
    ((This0)menu_tables[0][1])(menu_roots[0]);CHECK(ReadPtr(menu_roots[0],0xA8)==NULL);
    neutral_menu();CHECK(focus_id(0)==0x22);menu_step(KEY(PAD_UP),0,0);CHECK(focus_id(0)==0x21);
    /* 几何导航覆盖外传额外按钮，普通本体不能选到隐藏A7。 */
    for (unsigned i=0;i<6;++i) { neutral_menu();menu_step(KEY(PAD_DOWN),0,0); }
    CHECK(focus_id(0)==(expansion ? 0xA7:0x23));
    /* 开页时旧A/左摇杆输入不执行，松开后才能操作；系统默认继续而非鼠标指向的离开按钮。 */
    world_ptr=old_world;activate_page(1);menu_step(KEY(PAD_A),1,0);CHECK(system_actions==0);
    neutral_menu();CHECK(focus_id(1)==0x2E);CHECK(Read32(menu_children[1][0],0x58)==0);
    menu_step(0,0,1);CHECK(focus_id(1)==0x30);
    CHECK(Read32(menu_children[1][0],0x58)==77 && Read32(menu_children[1][1],0x58)==0);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(system_actions==1 && selected_action==0x30);
    /* 物理来源完整回到原hover；新的手柄接管不拿物理悬停当默认选择。 */
    Menu_Suspend();g_intent.layer=LAYER_NATIVE;
    CHECK(((This3)menu_tables[1][0x30/4])(menu_roots[1],12,34,(void *)56)==9);
    CHECK(native_hovers==1);menu_step(KEY(PAD_A),0,0);CHECK(system_actions==1);
    neutral_menu();CHECK(focus_id(1)==0x2E);
    /* capture子对象归一化到确认框，优先于下面仍显示的系统页。 */
    ((This2)menu_tables[2][0x1C/4])(menu_roots[2],1,0);
    ptr(ui_data,0x3C,menu_children[2][2]);ptr(ui_data,0x40,menu_roots[1]);
    unsigned reason;CHECK(Menu_Context(&reason)==menu_roots[2] && reason==1);
    neutral_menu();CHECK(focus_id(2)==0x9A);
    menu_step(KEY(PAD_RIGHT),0,0);CHECK(focus_id(2)==0x9B);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(!Read32(menu_roots[2],0x64) && confirm_actions==0);
    CHECK(Menu_CapturesInput());game_isolated();
    ((This2)menu_tables[2][0x1C/4])(menu_roots[2],1,0);neutral_menu();
    menu_step(KEY(PAD_A),0,0);CHECK(confirm_actions==1);
    /* 同帧A+B只取消，关闭后held X和推杆也不漏到攻击/移动。 */
    activate_page(2);neutral_menu();menu_step(KEY(PAD_A)|KEY(PAD_B)|KEY(PAD_X),1,0);
    CHECK(confirm_actions==1 && Menu_BlocksGameplay());game_isolated();
    menu_step(KEY(PAD_X),1,0);game_isolated();
    menu_step(0,0,0);CHECK(!Menu_BlocksGameplay());
    /* 只回中一帧后新的RT输入可以进入技能层，不因离开MENU重新锁住。 */
    g_input.rt=true;menu_step(0,0,0);CHECK(g_intent.layer==LAYER_SKILL && !Game_Menu());
    g_input.rt=false;menu_step(0,0,0);
    menu_step(KEY(PAD_X),0,0);unsigned before_attack=(unsigned)releases;Game_Update();
    CHECK((unsigned)releases==before_attack+1);Combat_Reset();neutral_menu();
    /* 纯世界物理/手柄交替不设菜单门，第一下新攻击必须沿已有战斗协议接受。 */
    Menu_Suspend();g_intent.layer=LAYER_NATIVE;Menu_Update();CHECK(!Game_Menu());
    menu_step(KEY(PAD_X),0,0);CHECK(g_intent.layer==LAYER_GAME && !Menu_CapturesInput());
    before_attack=(unsigned)releases;Game_Update();CHECK((unsigned)releases==before_attack+1);
    Combat_Reset();neutral_menu();
    /* 断线/失焦会停止拥有焦点，恢复原hover；重连仍需松开。 */
    activate_page(1);neutral_menu();g_input.connected=false;menu_step(KEY(PAD_A),0,0);
    CHECK(system_actions==1);g_input.connected=true;menu_step(KEY(PAD_A),0,0);CHECK(system_actions==1);
    neutral_menu();g_input.focused=false;menu_step(KEY(PAD_A),0,0);g_input.focused=true;
    menu_step(KEY(PAD_A),0,0);CHECK(system_actions==1);neutral_menu();
    /* 读档选择和翻页直接调用原服务，空槽拒绝，B真正返回标题。 */
    activate_page(3);neutral_menu();CHECK(Read32(menu_roots[3],0xD4)==0);
    menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(menu_roots[3],0xD4)==1);
    neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(menu_roots[3],0xD0)==4 && Read32(menu_roots[3],0xD4)==1);
    CHECK(Menu_CursorAnchor(&anchor));CHECK(anchor.x==334 && anchor.y==154);
    neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(menu_roots[3],0xD4)==1);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(load_actions==1);game_isolated();
    activate_page(3);neutral_menu();menu_step(KEY(PAD_B),0,0);
    CHECK(!Read32(menu_roots[3],0x64) && Read32(menu_roots[0],0x64));
    /* X是原设计的删除请求，不是直接删档；确认框方向不得改变后台读档页。 */
    activate_page(3);neutral_menu();menu_step(KEY(PAD_X),0,0);
    CHECK(delete_requests==1 && deleted_records==0 && Read32(menu_roots[4],0x64));
    unsigned background_slot=Read32(menu_roots[3],0xD4),background_page=Read32(menu_roots[3],0xD0);
    neutral_menu();CHECK(Read32(ReadPtr(menu_roots[4],0xA8),0xC4)==0);
    menu_step(KEY(PAD_B),0,0);CHECK(deleted_records==0 && !Read32(menu_roots[4],0x64));
    neutral_menu();menu_step(KEY(PAD_X),0,0);neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);
    CHECK(Read32(ReadPtr(menu_roots[4],0xA8),0xC4)==1);
    CHECK(Read32(menu_roots[3],0xD4)==background_slot && Read32(menu_roots[3],0xD0)==background_page);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(deleted_records==1 && Read32(menu_roots[3],0xC0)==5);
    CHECK(Menu_CapturesInput());game_isolated();
    activate_page(3);Write32(menu_roots[3],0xC0,0);neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(load_actions==1);
    CHECK(!Menu_CursorAnchor(&anchor));
    /* NPC选项按脚本记录，提示项不选；Tick原命中调用被独立焦点替代。 */
    activate_page(5);neutral_menu();CHECK(Read32(ReadPtr(menu_roots[5],0xA8),0xC4)==10);
    ((This0)menu_tables[5][1])(menu_roots[5]);CHECK(Read32(ReadPtr(menu_roots[5],0xA8),0xC4)==10);
    menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(ReadPtr(menu_roots[5],0xA8),0xC4)==20);
    talk_delay=2;neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(talk_selected==0);
    talk_delay=0;neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(talk_selected==20);
    neutral_menu();menu_step(KEY(PAD_B),0,0);CHECK(talk_cancelled==1);
    activate_page(6);neutral_menu();int before_scroll=(int)Read32(menu_roots[6],0xF4);
    menu_step(KEY(PAD_A),0,0);CHECK(text_next_count==0 && Read32(menu_roots[6],0x64));
    ((This0)menu_tables[6][1])(menu_roots[6]);CHECK((int)Read32(menu_roots[6],0xF4)==before_scroll-3 && text_next_count==0);
    menu_step(0,0,0);before_scroll=(int)Read32(menu_roots[6],0xF4);
    ((This0)menu_tables[6][1])(menu_roots[6]);CHECK((int)Read32(menu_roots[6],0xF4)==before_scroll-1);
    menu_step(KEY(PAD_B),0,0);CHECK(text_next_count==1);
    /* 未实现页面保留B/方向旧键盘能力，仍绝不注入Enter或直接猜+3C。 */
    for (unsigned k=0;k<7;++k) Write32(menu_roots[k],0x64,0);
    Write32(unknown_root,0x64,1);ptr(ui_data,0x18,unknown_root);ptr(ui_data,0x1C,unknown_root);
    neutral_menu();menu_step(KEY(PAD_B)|KEY(PAD_A),0,0);
    BYTE keys[256]={0};Game_Keyboard(keys);CHECK(keys[VK_ESCAPE]==0x80 && keys[VK_RETURN]==0);
    /* 普通手柄世界不画鼠标图样，物理来源仍用原位置与全部参数。 */
    g_intent.layer=LAYER_GAME;
    CHECK(((Draw)patched_callee((uintptr_t)(cursor_code+16)))(menu_sprites[0][0],(void *)0x246,37,49,0,-1,0)==0);
    g_intent.layer=LAYER_NATIVE;CHECK(((BOOL (WINAPI *)(POINT *))patched_callee((uintptr_t)cursor_code))(&anchor));CHECK(anchor.x==37 && anchor.y==49);
    CHECK(((Draw)patched_callee((uintptr_t)(cursor_code+8)))(menu_sprites[0][0],(void *)0x246,37,49,0,-1,0)==6);
    Cursor_Shutdown();CHECK(cursor_code[0]==0xFF && cursor_code[1]==0x15);
    Menu_Shutdown();
    for (unsigned k=0;k<7;++k) CHECK(menu_tables[k][1]==(uintptr_t)menu_tick && menu_tables[k][0x30/4]==(uintptr_t)menu_hover);
    HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
    CHECK(!Menu_BlocksGameplay());
    /* 每个入口写失败都回滚，不能留下半套Tick/Show/hover。所有原槽先核对再允许重试。 */
    for (unsigned fail=1;fail<=22;++fail) {
        patch_attempt=0;patch_fail_at=fail;CHECK(!Menu_Initialize());
        for (unsigned k=0;k<7;++k) {
            CHECK(menu_tables[k][1]==(uintptr_t)menu_tick);
            CHECK(menu_tables[k][0x1C/4]==(uintptr_t)menu_show);
            CHECK(menu_tables[k][0x30/4]==(uintptr_t)menu_hover);
        }
        CHECK(!Menu_BlocksGameplay());
    }
    patch_fail_at=0;patch_attempt=0;
    menu_tables[1][0x30/4]=0;CHECK(!Menu_Initialize());
    CHECK(menu_tables[0][1]==(uintptr_t)menu_tick);menu_tables[1][0x30/4]=(uintptr_t)menu_hover;
    CHECK(Menu_Initialize());
    /* Draw可能已被显示模块包装，不属于菜单验证/撤销范围；后来替换的hover也不能覆盖。 */
    menu_tables[1][2]=(uintptr_t)0x12345678;menu_tables[2][0x30/4]=(uintptr_t)0x23456789;
    Menu_Shutdown();CHECK(menu_tables[1][2]==(uintptr_t)0x12345678);
    CHECK(menu_tables[2][0x30/4]==(uintptr_t)0x23456789);
    HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
int main(void)
{
    menu_regression(false);menu_regression(true);
    printf("两作菜单原生虚表、焦点、动画与输入隔离回放通过：%u项\n",checks);
    return 0;
}
