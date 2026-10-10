/* 复用32位真实游戏适配fixture，新增真正Menu.c的虚表包装与Control/Game隔离回放。
 * 不复制生产路由/导航算法，不读取玩家进程，也不把宿主回放当成实机通过。 */
#define Memory_Patch baseline_memory_patch
#define main baseline_game_main
#include "test_game.c"
#undef main
#undef Memory_Patch
#include "Menu.h"
#include "Cursor.h"
#include "../../src/Runtime/Focus.c"

static BYTE menu_roots[15][0x300],menu_children[15][16][0xE4],menu_sprites[15][16][4*32];
static BYTE unknown_root[0xD0],menu_resource[16];
static uintptr_t menu_tables[19][26];
static BYTE new_roots[2][0x300],new_children[2][3][0xE4];
static unsigned character_requests,newgame_requests,difficulty_requests;
static BYTE settings_root[0x300],settings_children[8][0xF0];
static unsigned settings_applies,settings_choices,settings_closes;
static ControlState menu_control;
static bool expansion_case,transition_after_action;
static unsigned title_actions,system_actions,confirm_actions,native_hovers,tick_calls,animation_resets;
static unsigned selected_action,pending_ticks;
static unsigned patch_attempt,patch_fail_at;
static unsigned focus_sounds,load_actions,cursor_draws,delete_requests,deleted_records;
static void *message_pointer;
static BYTE talk_picker_code[8];
static unsigned talk_selected,talk_cancelled,text_next_count,talk_delay;
static BYTE skill_calls[3][8],quest_nodes[3][12],quest_records[3][8],quest_list_data[0x100];
static BYTE combo_list_data[16],combo_nodes[18][12];
static unsigned combo_methods[18];
static bool reject_skill_learning;
static bool reject_combo_delete;
static unsigned combo_capacity=18,quest_switches,quest_selections,skill_actions,skill_details;
static int combo_mouse_index=-1,skill_operation_index=-2;
static BYTE grid_player[0x400];static void *bag_pointer,*storage_pointer;
static BYTE shop_position_code[3][6],nonroot_resource[16];
static unsigned grid_details,grid_swaps,grid_panel_actions;
static unsigned shop_requests,shop_trades;
static int __fastcall grid_player_get(void *root, void *unused_edx) { (void)unused_edx;(void)root;return (int)(uintptr_t)grid_player;}
static int __fastcall grid_item_get(void *container, void *unused_edx,int slot)
{ (void)unused_edx;CHECK(container==grid_player && slot>=0 && slot<136);return Read32(container,0xA4+slot*4)==UINT32_MAX ? 0:1;}
static int __fastcall grid_empty(void *container, void *unused_edx)
{ (void)unused_edx;CHECK(container==grid_player);for (int i=0;i<50;++i) if (Read32(container,0xA4+i*4)==UINT32_MAX) return i;return -1;}
static int __fastcall shop_switch(void *root, void *unused_edx,int mode)
{ (void)unused_edx;CHECK(root==menu_roots[11] && (mode==0x57 || mode==0x58));Write32(root,0xD0,mode);return 1;}
static int __fastcall grid_swap(void *container, void *unused_edx,int slot)
{ (void)unused_edx;
    CHECK(container==grid_player && slot>=0 && slot<136);++grid_swaps;
    unsigned item=Read32(container,0xA4+slot*4),held=Read32(container,0x2C4);
    Write32(container,0xA4+slot*4,held);Write32(container,0x2C4,item);return 1;
}
static int __fastcall combo_mouse_hit(void *root, void *unused_edx) { (void)unused_edx;CHECK(root==menu_roots[8]);return combo_mouse_index;}
static int __fastcall talk_picker(void *root, void *unused_edx) { (void)unused_edx;CHECK(root==menu_roots[5]);return (int)(uintptr_t)menu_children[5][1];}
static int __fastcall talk_select(void *root, void *unused_edx) { (void)unused_edx;CHECK(root==menu_roots[5]);talk_selected=Read32(ReadPtr(root,0xA8),0xC4);return 1;}
static int __fastcall talk_cancel(void *root, void *unused_edx,int e,int x,void *y) { (void)unused_edx;CHECK(root==menu_roots[5] && !e && !x && !y);++talk_cancelled;Write32(root,0x64,0);ptr(ui_data,0x3C,NULL);return 1;}
static int __fastcall text_next(void *root, void *unused_edx) { (void)unused_edx;CHECK(root==menu_roots[6]);++text_next_count;Write32(root,0x64,0);ptr(ui_data,0x3C,NULL);return 1;}
static BYTE load_nodes[12][12],load_data[12][0x40],cursor_code[32];
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
    if(self==new_roots[0])return 17;
    if(self==new_roots[1])return 18;
    if (self==settings_root) return 16;
    if (self==hud_data) return 15;
    for (int i=0;i<15;++i) if (self==menu_roots[i]) return i;
    CHECK(false);return -1;
}
static int __fastcall menu_property(void *self, void *unused_edx,int field)
{ (void)unused_edx;
    if (self==nonroot_resource) return field==13 ? 0:field==2 ? 3:0;
    if (self!=menu_resource) return native_property(self, NULL,field);
    return field==19 ? 111:field==13 ? 1:field==2 ? 3:0;
}
static int __fastcall menu_show(void *self, void *unused_edx,int active,int mode)
{ (void)unused_edx;
    int k=root_kind(self);CHECK(mode==0 || mode==-1);
    Write32(self,0x64,active!=0);Write32(self,0x68,0);
    if (k==2 || k==4 || k==5 || k==6) ptr(ui_data,0x3C,active ? self:NULL);
    if (k==6 && active) {Write32(self,0xF0,1);Write32(self,0xF4,(unsigned)-20);}
    if (k==3 && active) {Write32(self,0xD0,0);Write32(self,0xD4,0);}
    return 1;
}
static int __fastcall menu_tick(void *self, void *unused_edx)
{ (void)unused_edx;
    int k=root_kind(self);++tick_calls;
    if (k==8) {
        ((This0)patched_callee((uintptr_t)skill_calls[0]))(self, NULL);
        if (ReadPtr(self,0xA8)) ++skill_details;
        return 7;
    }
    if (k>=9) {
        if (Read32(self,0xBC) && (k>=12 ? ReadPtr(self,0xA8)!=NULL:Read32(self,k==9 ? 0xFC:0xC0)<50)) ++grid_details;
        /* 原base Tick用物理路由清BC/A8；生产wrapper必须在结束后恢复独立焦点。 */
        Write32(self,0xBC,0);ptr(self,0xA8,NULL);return 7;
    }
    /* 模拟原base Tick因鼠标离开根页而清悬停；生产wrapper必须重新投影自己的选择。 */
    ptr(self,0xA8,NULL);
    if (k==5) ptr(self,0xA8,(void *)(uintptr_t)((This0)patched_callee((uintptr_t)talk_picker_code))(self, NULL));
    if (k==6 && Read32(self,0x64)) Write32(self,0xF4,Read32(self,0xF4)-Read32(self,0xF0));
    if (k==0 && pending_ticks) {
        for (unsigned i=0;i<6;++i)
            if (Read32(menu_children[0][i],0x28)==selected_action) Write32(menu_children[0][i],0x40,3);
        if (!--pending_ticks) {
            Write32(self,0xC0,(uint32_t)-1);
            if (transition_after_action) ((This2)menu_tables[k][0x1C/4])(self, NULL,0,0);
        }
    }
    return 7;
}
static int __fastcall skill_base_tick(void *self, void *unused_edx)
{ (void)unused_edx;int k=root_kind(self);if (k==4) return menu_tick(self, NULL);CHECK(k==8);ptr(self,0xA8,NULL);return 7;}
static int __fastcall menu_get_jm(void *self, void *unused_edx,int id)
{ (void)unused_edx;
    CHECK(self==ui_data);
    if(id==0x1A)return (int)(uintptr_t)new_roots[0];
    if(id==0x9C)return (int)(uintptr_t)new_roots[1];
    if(id==0x9F)return (int)(uintptr_t)new_children[1][0];
    if (id==0xAA) return (int)(uintptr_t)settings_root;
    if (id==0x66) return (int)(uintptr_t)quest_list_data;
    for (unsigned i=0;i<15;++i) if (Read32(menu_roots[i],0x28)==(unsigned)id) return (int)(uintptr_t)menu_roots[i];
    return 0;
}
static int __fastcall quest_switch(void *self, void *unused_edx,int id,int selection)
{ (void)unused_edx;
    CHECK(root_kind(self)==7 && id>=0x78 && id<=0x7B && selection==-1);
    ++quest_switches;Write32(self,0xC0,id);Write32(quest_list_data,0xF4,0);return 1;
}
static int __fastcall quest_select(void *self, void *unused_edx,int index)
{ (void)unused_edx;
    CHECK(root_kind(self)==7 && index>=0 && index<3);
    ++quest_selections;Write32(quest_list_data,0xF4,index);
    Write32(quest_list_data,0xDC,index>0 ? (unsigned)(-(index*20)):0);return 1;
}
static int __fastcall skill_switch(void *self, void *unused_edx,int id)
{ (void)unused_edx;CHECK(root_kind(self)==8 && (id==0x7F || id==0x80));Write32(self,0xC0,id);return 1;}
static int __fastcall skill_slot(void *self, void *unused_edx,int slot)
{ (void)unused_edx;CHECK(root_kind(self)==8 && slot>=0 && slot<4);Write32(self,0xC4,slot);return 1;}
static int __fastcall skill_combo_get(void *self, void *unused_edx,int slot)
{ (void)unused_edx;CHECK(self==hud_data && slot>=0 && slot<4);return (int)(uintptr_t)combo_list_data;}
static int __fastcall skill_primary(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    CHECK(root_kind(self)==8 && !e && !x && !y);
    ++skill_actions;skill_operation_index=((This0)patched_callee((uintptr_t)skill_calls[1]))(self, NULL);
    selected_action=Read32(ReadPtr(self,0xA8),0x28);
    /* 此替身代表游戏业务：上方清单以位置删除（同招式也不混淆），下方技能点击则追加。
     * 插件生产代码没有这些数组写入；用业务结果验证它送来的焦点与参数。 */
    if (Read32(self,0xC0)==0x80) {
        unsigned total=Read32(combo_list_data,0);
        if (skill_operation_index>=0 && (unsigned)skill_operation_index<total) {
            if (reject_combo_delete) return 0;
            for (unsigned i=(unsigned)skill_operation_index;i+1<total;++i) combo_methods[i]=combo_methods[i+1];
            Write32(combo_list_data,0,total-1);
        } else if ((selected_action==0x84 || selected_action==0x85) && total<18) {
            combo_methods[total]=selected_action==0x84 ? 1001:1002;
            Write32(combo_list_data,0,total+1);
        }
    } else if (reject_skill_learning) return 0;
    return 1;
}
static int __fastcall skill_secondary(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    CHECK(root_kind(self)==8 && !e && !x && !y);
    skill_operation_index=((This0)patched_callee((uintptr_t)skill_calls[2]))(self, NULL);return 1;
}
static int __fastcall grid_primary(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    int kind=root_kind(self);CHECK(kind>=9 && !e && !x && !y);
    void *child=ReadPtr(self,0xA8);
    if (kind==11) {
        if (child) {Write32(self,0xD0,Read32(child,0x28));return 1;}
        if (Read32(self,0xC0)<50) {
            POINT point;CHECK(((BOOL (WINAPI *)(POINT *))patched_callee((uintptr_t)shop_position_code[0]))(&point));
            ++shop_requests;ptr(menu_roots[4],0xCC,self);((This2)menu_tables[4][0x1C/4])(menu_roots[4], NULL,1,0);
        }
        return 1;
    }
    if (kind>=12 && child) {
        unsigned id=Read32(child,0x28),first=kind==12 ? 0x41:kind==13 ? 0x49:0x5E;
        unsigned count=kind==12 ? 7:kind==13 ? 12:5;
        if (id>=first && id<first+count) return grid_swap(grid_player, NULL,(int)(id-first)+(kind==12 ? 62:kind==13 ? 69:81));
    }
    if (child) {++grid_panel_actions;selected_action=Read32(child,0x28);return 1;}
    if (kind>=12) return 1;
    unsigned local=Read32(self,kind==9 ? 0xFC:0xC0);
    return local<50 ? grid_swap(grid_player, NULL,(int)local+(kind==10 ? 86:0)):0;
}
static int __fastcall menu_hover(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    int k=root_kind(self);++native_hovers;
    CHECK(e==12 && x==34 && y==(void *)56);
    ptr(self,0xA8,menu_children[k][1]);return 9;
}
static int __fastcall menu_activate(void *self, void *unused_edx,int id)
{ (void)unused_edx;
    CHECK(root_kind(self)==0);++title_actions;selected_action=(unsigned)id;
    if (!expansion_case) { Write32(self,0xC0,id);pending_ticks=3; }
    else if (transition_after_action) ((This2)menu_tables[0][0x1C/4])(self, NULL,0,0);
    return 1;
}
static int __fastcall menu_primary(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    int kind=root_kind(self);
    CHECK((kind==1 && e==0 && x==0 && y==NULL) || (kind==3 && e==0 && x==-1 && y==(void *)(intptr_t)-1));
    void *child=ReadPtr(self,0xA8);CHECK(ReadPtr(child,0xA4)==self);
    selected_action=Read32(child,0x28);
    if (kind==3) {
        CHECK(selected_action==0x97);((This2)menu_tables[3][0x1C/4])(self, NULL,0,0);
        ((This2)menu_tables[0][0x1C/4])(menu_roots[0], NULL,1,0);
    } else {
        ++system_actions;
        if (transition_after_action) ((This2)menu_tables[1][0x1C/4])(self, NULL,0,0);
    }
    return 1;
}
static int __fastcall menu_confirm(void *self, void *unused_edx)
{ (void)unused_edx;
    CHECK(root_kind(self)==2);++confirm_actions;
    /* 原回调解析原数值文本；菜单适配不能绕过回调直接改owner/金钱。 */
    CHECK(Read32(menu_children[2][2],0xC8)==42);
    ((This2)menu_tables[2][0x1C/4])(self, NULL,0,0);return 1;
}
static int __fastcall menu_texture(void *self, void *unused_edx,int x,int y)
{ (void)unused_edx;
    CHECK(ReadPtr(self,0xA4)==menu_roots[1]);CHECK((x==-1 || x==0) && y==0);
    Write32(self,0x58,x==-1 ? 77u:0u);return 1;
}
static int __fastcall menu_animation_reset(void *self, void *unused_edx,int zero)
{ (void)unused_edx;
    CHECK(Memory_Readable(self,32));CHECK(zero==0);++animation_resets;return 1;
}
static int __cdecl menu_sound(int id,int a,int volume,int b)
{ CHECK(id==0x93 && a==0 && volume==100 && b==0);++focus_sounds;return 1; }
static int __fastcall load_select(void *self, void *unused_edx,int slot)
{ (void)unused_edx;
    CHECK(root_kind(self)==3 && slot>=0 && slot<4);
    if (Read32(self,0xD0)+(unsigned)slot>=Read32(self,0xC0)) return 0;
    Write32(self,0xD4,(unsigned)slot);return 1;
}
static int __fastcall load_page(void *self, void *unused_edx,int delta)
{ (void)unused_edx;
    CHECK(root_kind(self)==3 && (delta==-4 || delta==4));int next=(int)Read32(self,0xD0)+delta;
    if (next>=0 && (unsigned)next<Read32(self,0xC0)) Write32(self,0xD0,(unsigned)next);
    return 1;
}
static int __fastcall load_submit(void *self, void *unused_edx,int slot)
{ (void)unused_edx;
    CHECK(root_kind(self)==3 && slot==-1 && Read32(self,0xD0)+Read32(self,0xD4)<Read32(self,0xC0));
    ++load_actions;((This2)menu_tables[3][0x1C/4])(self, NULL,0,0);return 1;
}
static int __fastcall menu_template(void *self, void *unused_edx,int index,int mode)
{ (void)unused_edx;
    if (self!=menu_resource && self!=nonroot_resource) return native_template(self, NULL,index,mode);
    CHECK((mode==1 || mode==3) && index>=5 && index<=8);static const int rect[]={40,60,300,50};return rect[index-5];
}
static int __fastcall message_lookup(void *table, void *unused_edx,int id,int language)
{ (void)unused_edx; CHECK(table==(void *)0x345 && id==0x13D && language==1);return (int)(uintptr_t)"原删除提示"; }
static int __fastcall message_open(void *self, void *unused_edx,void *owner,const char *text,int x,int y,int a,int b)
{ (void)unused_edx;
    CHECK(root_kind(self)==4 && owner==menu_roots[3] && !strcmp(text,"原删除提示") && x>=0 && y>=0 && a==0 && b==0);
    ++delete_requests;ptr(self,0xCC,owner);((This2)menu_tables[4][0x1C/4])(self, NULL,1,0);return 1;
}
static int __fastcall message_primary(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    CHECK(root_kind(self)==4 && e==0 && x==0 && y==NULL);
    void *child=ReadPtr(self,0xA8),*owner=ReadPtr(self,0xCC);
    CHECK((owner==menu_roots[3] || !owner || (owner>= (void *)menu_roots[9] && owner<= (void *)menu_roots[14])) && Read32(child,0x28)==0x2A && Read32(child,0xC4)<=1);
    bool yes=Read32(child,0xC4)==1;((This2)menu_tables[4][0x1C/4])(self, NULL,0,0);ptr(self,0xCC,NULL);
    if (yes && owner==menu_roots[3]) {++deleted_records;Write32(owner,0xC0,Read32(owner,0xC0)-1);}
    if (yes && owner==menu_roots[11]) ++shop_trades;
    return 1;
}
static BOOL WINAPI cursor_position(POINT *point) { point->x=37;point->y=49;return TRUE; }
static BYTE focus_sprites[6*32],focus_surface[0x20];
static void record_focus(int x,int y,int width,int height);
static int __fastcall cursor_sprite(void *self, void *unused_edx,void *surface,int x,int y,int frame,int shade,int flags)
{ (void)unused_edx;
    if(self==focus_sprites+5*32) {CHECK(surface==focus_surface && !frame && !shade && !flags);record_focus(x,y,26,26);return 6;}
    if(self==menu_sprites[8][0] && surface==focus_surface) {CHECK(frame==0 && shade==-1 && !flags);++cursor_draws;return 6;}
    CHECK(self==menu_sprites[0][0] && surface==(void *)0x246 && frame==0 && shade==-1 && flags==0);
    ++cursor_draws;cursor_x=x;cursor_y=y;return 6;
}
static BYTE focus_description[22],focus_image[0x20],focus_bank[8],focus_entry[0x28];
static void *focus_entries[1]={focus_entry};
static unsigned focus_draws,drop_requests;static RECT focus_rectangle;
static void record_focus(int x,int y,int width,int height)
{
    int16_t values[4];memcpy(values,focus_description+0xE,8);
    x+=values[0]-values[2];y+=values[1]-values[3];
    RECT r={x,y,x+width,y+height};
    if(!focus_draws)focus_rectangle=r;
    else {
        if(r.left<focus_rectangle.left)focus_rectangle.left=r.left;
        if(r.top<focus_rectangle.top)focus_rectangle.top=r.top;
        if(r.right>focus_rectangle.right)focus_rectangle.right=r.right;
        if(r.bottom>focus_rectangle.bottom)focus_rectangle.bottom=r.bottom;
    }
    ++focus_draws;
}
static int __fastcall focus_frame_get(void *sprite, void *unused_edx)
{ (void)unused_edx;CHECK(sprite==focus_sprites+5*32);return (int)(uintptr_t)focus_description;}
static int __fastcall focus_image_get(void *description, void *unused_edx)
{ (void)unused_edx;CHECK(description==focus_description);return (int)(uintptr_t)focus_image;}

static int __fastcall scaled_focus(void *sprite, void *unused_edx,void *surface,int x,int y,int width,int height,
                                                 int sx,int sy,int mode,int shade,void *clip)
{ (void)unused_edx;
    CHECK(sprite==focus_sprites+5*32 && surface==focus_surface && !mode && !shade && !clip);
    /* 原入口裁取源图，不会缩放：每片都必须在真实26×26源帧里。旧32×32直接调用会失败。 */
    CHECK(sx>=0 && sy>=0 && width>0 && height>0 && sx+width<=26 && sy+height<=26);
    record_focus(x,y,width,height);return 1;
}
static int __fastcall native_drop(void *container, void *unused_edx,int item)
{ (void)unused_edx;
    CHECK(container==grid_player);++drop_requests;
    if (item==-1) Write32(container,0x2C4,UINT32_MAX);
    else {
        /* 原丢弃参数为物品编号。若生产误传格号，这里不会凭巧合丢掉正确物品。 */
        for (unsigned i=0;i<50;++i) if ((int)Read32(container,0xA4+i*4)==item) {
            Write32(container,0xA4+i*4,UINT32_MAX);return 1;
        }
        CHECK(false);
    }
    return 1;
}
static void emit_focus(void)
{
    CHECK(cursor_draw_callback && cursor_begin_callback);cursor_begin_callback(RUNTIME_EVENT_UI_DRAW_BEGIN,ui_data,0,0,NULL);
    RuntimeFocusRequest request={0};
    RECT r;unsigned reason;void *root=Menu_Context(&reason);
    if(g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_MOUSE && (ActionMenu_FocusFrame(&r) || (Read32(root,0)==g_profile->menu_skill_vtable && Menu_FocusFrame(&r)))) {
        request.rectangle=(RuntimeFocusRect){r.left,r.top,r.right,r.bottom};
        typedef int (__fastcall *DrawIcon)(void *, void *,void *,int,int,int,int,int);
        ((DrawIcon)patched_callee(g_profile->icon_focus_call))(menu_sprites[8][0], NULL,focus_surface,
            request.rectangle.left-1,request.rectangle.top-1,0,-1,0);
        unsigned before=focus_draws;
        /* 原图标后的框已经画完；END不能重画到随后原说明文字上。 */
        cursor_draw_callback(RUNTIME_EVENT_UI_DRAW_END,ui_data,(unsigned long)(uintptr_t)focus_surface,0,NULL);CHECK(focus_draws==before);
    } else cursor_draw_callback(RUNTIME_EVENT_UI_DRAW_END,ui_data,(unsigned long)(uintptr_t)focus_surface,0,NULL);
}
static void cursor_fixture(Profile *profile)
{
    memset(cursor_code,0x90,sizeof cursor_code);cursor_position_pointer=(uintptr_t)cursor_position;
    cursor_code[0]=0xFF;cursor_code[1]=0x15;uintptr_t iat=(uintptr_t)&cursor_position_pointer;
    memcpy(cursor_code+2,&iat,4);make_call(cursor_code+8,(uintptr_t)cursor_sprite);make_call(cursor_code+16,(uintptr_t)cursor_sprite);make_call(cursor_code+24,(uintptr_t)cursor_sprite);
    profile->cursor_position_call=(uintptr_t)cursor_code;profile->cursor_position_iat=iat;
    profile->cursor_sprite_call1=(uintptr_t)(cursor_code+8);profile->cursor_sprite_call2=(uintptr_t)(cursor_code+16);
    profile->icon_focus_call=(uintptr_t)(cursor_code+24);
    profile->cursor_sprite_draw=(uintptr_t)cursor_sprite;profile->focus_rect_draw=(uintptr_t)scaled_focus;
    profile->menu_item_drop=(uintptr_t)native_drop;profile->focus_frame_get=(uintptr_t)focus_frame_get;
    profile->focus_image_get=(uintptr_t)focus_image_get;
    backend=(FocusBackend){.hud_global=(uintptr_t)profile->skill_global,.hud_vtable=profile->menu_hud_vtable,
        .sprite_draw=(uintptr_t)cursor_sprite,.rect_draw=(uintptr_t)scaled_focus,
        .frame_get=(uintptr_t)focus_frame_get,.image_get=(uintptr_t)focus_image_get};ready=true;
    if(!subscribed) {CHECK(Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_END,draw,NULL));CHECK(Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_BEGIN,draw,NULL));subscribed=true;}
    CHECK(Cursor_Initialize());cursor_draws=focus_draws=drop_requests=0;
    ptr(hud_data,0x48,focus_sprites);Write32(hud_data,0x44,6);
    memset(focus_description,0,sizeof focus_description);ptr(focus_description,8,focus_bank);Write32(focus_description,4,0);ptr(focus_bank,4,focus_entries);
    ptr(focus_sprites+5*32,0,focus_description);Write32(focus_sprites+5*32,4,0);Write32(focus_sprites+5*32,8,1);
    Write32(focus_image,0xC,26);Write32(focus_image,0x10,26);Write32(focus_surface,0xC,856);Write32(focus_surface,0x10,480);
}
static void activate_page(unsigned kind)
{
    for (unsigned k=0;k<15;++k) if (k!=kind) Write32(menu_roots[k],0x64,0);
    Write32(settings_root,0x64,0);
    Write32(unknown_root,0x64,0);
    ((This2)menu_tables[kind][0x1C/4])(menu_roots[kind], NULL,1,0);
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
/* 替身模拟原设置业务，验证手柄只提供焦点/数值而不接管保存内容。 */
static int __fastcall settings_slider(void *self, void *unused_edx,int value)
{ (void)unused_edx;
    CHECK(self==settings_children[0] || self==settings_children[1] || self==settings_children[2]);
    Write32(self,0xDC,(unsigned)(value<0 ? 0:value>100 ? 100:value));return 1;
}
static int __fastcall settings_apply(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    CHECK(self==settings_root && !e && !x && !y && !ReadPtr(self,0xA8));++settings_applies;return 1;
}
static int __fastcall settings_close(void *self, void *unused_edx)
{ (void)unused_edx;
    CHECK(self==settings_root);++settings_closes;
    ((This2)menu_tables[16][0x1C/4])(self, NULL,0,0);return 1;
}
static int __fastcall settings_primary(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    CHECK(self==settings_root && !e && !x && !y);
    unsigned id=Read32(ReadPtr(self,0xA8),0x28);CHECK(id>=0xAE && id<=0xB2);
    if(id==0xB2)return settings_close(self, NULL);
    ++settings_choices;Write32(self,id<=0xAF ? 0xC4:0xC8,id);return 1;
}
static void settings_fixture(Profile *profile)
{
    memset(settings_root,0,sizeof settings_root);memset(settings_children,0,sizeof settings_children);
    ptr(settings_root,0,menu_tables[16]);Write32(settings_root,0x28,0xAA);ptr(settings_root,0x50,menu_resource);
    ptr(settings_root,0x9C,settings_children[0]);
    /* 按真实布局分行：三个滑块、两组选项、返回；业务仍按原控件ID。 */
    for(unsigned i=0;i<8;++i) {
        BYTE *c=settings_children[i];ptr(c,0xA4,settings_root);ptr(c,8,i<7 ? settings_children[i+1]:NULL);
        Write32(c,0x28,0xAB+i);Write32(c,0x64,1);Write32(c,0x1C,i<3 ? 200:80);Write32(c,0x20,20);
        Write32(c,0x14,i==4 || i==6 ? 220:100);Write32(c,0x18,50+(i<3 ? i:i<5 ? 3:i<7 ? 4:5)*40);
        Write32(c,0xD8,100);Write32(c,0xDC,50);
    }
    menu_tables[16][1]=(uintptr_t)menu_tick;menu_tables[16][0x1C/4]=(uintptr_t)menu_show;
    menu_tables[16][0x30/4]=(uintptr_t)menu_hover;menu_tables[16][0x24/4]=(uintptr_t)settings_primary;
    profile->menu_settings_vtable=(uintptr_t)menu_tables[16];profile->menu_settings_tick=(uintptr_t)menu_tick;
    profile->menu_settings_show=(uintptr_t)menu_show;profile->menu_settings_hover=(uintptr_t)menu_hover;
    profile->menu_settings_primary=(uintptr_t)settings_primary;profile->menu_settings_close=(uintptr_t)settings_close;
    profile->menu_settings_apply=(uintptr_t)settings_apply;profile->menu_settings_slider_set=(uintptr_t)settings_slider;
    settings_applies=settings_choices=settings_closes=0;
}
static int __fastcall character_primary(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    CHECK(self==new_roots[0] && !e && !x && !y);void *c=ReadPtr(self,0xA8);CHECK(ReadPtr(c,0xA4)==self);
    ++character_requests;Write32(self,0xC0,Read32(c,0xC4));
    ((This2)menu_tables[18][0x1C/4])(new_roots[1], NULL,1,0);
    /* 原提交在Show第二层之后再读第一层A8和子资源，不能提前清空。 */
    CHECK(ReadPtr(self,0xA8)==c && ReadPtr(c,0x50)!=NULL);
    ((This2)menu_tables[17][0x1C/4])(self, NULL,0,0);ptr(ui_data,0x3C,new_roots[1]);return 1;
}
static int __fastcall newgame_primary(void *self, void *unused_edx,int e,int x,void *y)
{ (void)unused_edx;
    CHECK(self==new_roots[1] && !e && !x && !y);unsigned id=Read32(ReadPtr(self,0xA8),0x28);CHECK(id==0xA0 || id==0xA1);
    if(id==0xA1){((This2)menu_tables[17][0x1C/4])(new_roots[0], NULL,1,0);((This2)menu_tables[18][0x1C/4])(self, NULL,0,0);ptr(ui_data,0x3C,new_roots[0]);}
    else ++newgame_requests; /* 替身代表原名称校验拒绝空名，不伪造进入世界。 */
    return 1;
}
static int __fastcall difficulty_cycle(void *self, void *unused_edx,int delta)
{ (void)unused_edx;CHECK(self==new_roots[1] && (delta==1 || delta==-1));++difficulty_requests;Write32(new_children[1][0],0xC4,(Read32(new_children[1][0],0xC4)+3+delta)%3);return 1;}
static void newgame_fixture(Profile *p)
{
    memset(new_roots,0,sizeof new_roots);memset(new_children,0,sizeof new_children);
    character_requests=newgame_requests=difficulty_requests=random_name_requests=0;
    for(unsigned k=0;k<2;++k) {
        void *root=new_roots[k];ptr(root,0,menu_tables[17+k]);ptr(root,0x50,menu_resource);Write32(root,0x28,k ? 0x9C:0x1A);ptr(root,0x9C,new_children[k][0]);
        for(unsigned j=0;j<3;++j) {
            void *c=new_children[k][j];ptr(c,0xA4,root);ptr(c,0x50,menu_resource);ptr(c,8,j<2 ? new_children[k][j+1]:NULL);
            Write32(c,0x28,k ? (j==0 ? 0x9F:j==1 ? 0xA0:0xA1):(j==0 ? 0x24:j==1 ? 0x25:0xA8));
            Write32(c,0x64,1);Write32(c,0x14,k ? (j==2 ? 350:120):80+j*160);Write32(c,0x18,k ? (j ? 300:140):100);
            Write32(c,0x1C,k ? 170:100);Write32(c,0x20,k ? 40:200);Write32(c,0xC4,k ? 1:j+4);
        }
        menu_tables[17+k][1]=(uintptr_t)menu_tick;menu_tables[17+k][0x1C/4]=(uintptr_t)menu_show;
        menu_tables[17+k][0x30/4]=(uintptr_t)menu_hover;menu_tables[17+k][0x24/4]=k ? (uintptr_t)newgame_primary:(uintptr_t)character_primary;
    }
    p->menu_character_vtable=(uintptr_t)menu_tables[17];p->menu_character_tick=(uintptr_t)menu_tick;
    p->menu_character_show=(uintptr_t)menu_show;p->menu_character_hover=(uintptr_t)menu_hover;p->menu_character_primary=(uintptr_t)character_primary;
    p->menu_newgame_vtable=(uintptr_t)menu_tables[18];p->menu_newgame_tick=(uintptr_t)menu_tick;p->menu_newgame_show=(uintptr_t)menu_show;
    p->menu_newgame_hover=(uintptr_t)menu_hover;p->menu_newgame_primary=(uintptr_t)newgame_primary;p->menu_newgame_cycle=(uintptr_t)difficulty_cycle;
}
static void menu_fixture(Profile *profile,bool expansion)
{
    configure(profile,expansion);expansion_case=expansion;entry_enabled=0;entry_opened=0;
    memset(menu_roots,0,sizeof menu_roots);memset(menu_children,0,sizeof menu_children);
    memset(menu_tables,0,sizeof menu_tables);memset(unknown_root,0,sizeof unknown_root);
    memset(&menu_control,0,sizeof menu_control);pending_ticks=0;
    title_actions=system_actions=confirm_actions=native_hovers=tick_calls=animation_resets=0;
    transition_after_action=false;selected_action=0;
    static const unsigned ids[15][6]={{0x1F,0x20,0x21,0x22,0x23,0xA7},
        {0x2E,0x30,0x31,0x2F,0,0},{0x9A,0x9B,0x99,0,0,0},{0x97,0x96,0xA2,0xA3,0,0},{0x2A,0x2A,0x2A,0,0,0},{0x2A,0x2A,0x2A,0,0,0},{0,0,0,0,0,0},
        {0x78,0x79,0x7A,0x7B,0x7C,0},{0x84,0x85,0x7E,0x7F,0x80,0x90},
        {0x15,0x16,0x17,0x5A,0,0},{0xB5,0xB6,0,0,0,0},
        {0x57,0x58,0x59,0,0,0},{0},{0},{0}};
    for (unsigned k=0;k<15;++k) {
        ptr(menu_roots[k],0,menu_tables[k]);ptr(menu_roots[k],0x50,menu_resource);
        unsigned root_ids[15]={0x19,0x2D,0x98,0x94,0x29,0x28,0x2C,0x63,0x7D,0x14,0xB4,0x56,0x3D,0x3E,0x5B};
        Write32(menu_roots[k],0x28,root_ids[k]);
        /* 原构造器的依附页初值为-1，0不能代表无依附。 */
        Write32(menu_roots[k],0xAC,UINT32_MAX);Write32(menu_roots[k],0xB0,UINT32_MAX);
        ptr(menu_roots[k],0x0C,k<14 ? menu_roots[k+1]:NULL);
        ptr(menu_roots[k],0x9C,menu_children[k][0]);
        menu_tables[k][1]=(uintptr_t)menu_tick;menu_tables[k][0x1C/4]=(uintptr_t)menu_show;
        menu_tables[k][0x30/4]=(uintptr_t)menu_hover;
        menu_tables[k][0x24/4]=(uintptr_t)menu_primary;menu_tables[k][0x3C/4]=(uintptr_t)menu_confirm;
        for (unsigned j=0;j<16;++j) {
            BYTE *c=menu_children[k][j];ptr(c,0xA4,menu_roots[k]);ptr(c,0x50,menu_resource);
            ptr(c,8,j<15 ? menu_children[k][j+1]:NULL);
            unsigned id=j<6 ? ids[k][j]:0;
            if (k==12) id=j<7 ? 0x41+j:j==7 ? 0x3F:j==8 ? 0x40:0;
            if (k==13) id=j<12 ? 0x49+j:j==12 ? 0x48:0;
            if (k==14) id=j<5 ? 0x5E + j:0;
            Write32(c,0x28,id);Write32(c,0x64,id!=0);
            Write32(c,0x14,(k==2 || k==4) ? j*100:100);Write32(c,0x18,(k==2 || k==4) ? 200:j*50+50);
            /* 技能页的可选技能位于下方；上方连招位置另按原六列几何生成。 */
            if (k==8) Write32(c,0x18,200+j*50);
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
    /* 技能原Tick的base调用和两个连招鼠标查询在fixture中也真实经过CALL包装。 */
    profile->menu_message_base_tick=(uintptr_t)skill_base_tick;
    make_call(skill_calls[0],(uintptr_t)skill_base_tick);
    make_call(skill_calls[1],(uintptr_t)combo_mouse_hit);make_call(skill_calls[2],(uintptr_t)combo_mouse_hit);
    profile->menu_skill_base_call=(uintptr_t)skill_calls[0];profile->menu_skill_combo_hit=(uintptr_t)combo_mouse_hit;
    profile->menu_skill_combo_call1=(uintptr_t)skill_calls[1];profile->menu_skill_combo_call2=(uintptr_t)skill_calls[2];
    profile->menu_quest_vtable=(uintptr_t)menu_tables[7];profile->menu_skill_vtable=(uintptr_t)menu_tables[8];
    profile->menu_quest_tick=profile->menu_skill_tick=(uintptr_t)menu_tick;
    profile->menu_quest_show=profile->menu_skill_show=(uintptr_t)menu_show;
    profile->menu_quest_hover=profile->menu_skill_hover=(uintptr_t)menu_hover;
    profile->menu_quest_primary=(uintptr_t)menu_primary;
    profile->menu_quest_switch=(uintptr_t)quest_switch;profile->menu_quest_select=(uintptr_t)quest_select;
    profile->menu_skill_primary=(uintptr_t)skill_primary;profile->menu_skill_secondary=(uintptr_t)skill_secondary;
    profile->menu_skill_switch=(uintptr_t)skill_switch;profile->menu_skill_slot=(uintptr_t)skill_slot;
    menu_tables[8][0x24/4]=(uintptr_t)skill_primary;menu_tables[8][0x2C/4]=(uintptr_t)skill_secondary;
    profile->get_jm=(uintptr_t)menu_get_jm;profile->combo_get=(uintptr_t)skill_combo_get;
    profile->menu_skill_combo_capacity=(uintptr_t)&combo_capacity;
    bag_pointer=menu_roots[9];storage_pointer=menu_roots[10];
    profile->menu_bag_global=(uintptr_t)&bag_pointer;profile->menu_storage_global=(uintptr_t)&storage_pointer;
    profile->menu_bag_vtable=(uintptr_t)menu_tables[9];profile->menu_storage_vtable=(uintptr_t)menu_tables[10];
    profile->menu_bag_tick=profile->menu_storage_tick=(uintptr_t)menu_tick;
    profile->menu_bag_show=profile->menu_storage_show=(uintptr_t)menu_show;
    profile->menu_bag_hover=profile->menu_storage_hover=(uintptr_t)menu_hover;
    profile->menu_bag_primary=profile->menu_storage_primary=(uintptr_t)grid_primary;
    menu_tables[9][0x24/4]=menu_tables[10][0x24/4]=(uintptr_t)grid_primary;
    profile->inventory_get=(uintptr_t)grid_player_get;profile->item_at=(uintptr_t)grid_item_get;
    profile->menu_item_swap=(uintptr_t)grid_swap;profile->menu_bag_empty=(uintptr_t)grid_empty;
    uintptr_t *extra_tables[4]={&profile->menu_shop_vtable,&profile->menu_craft_vtable,&profile->menu_inlay_vtable,&profile->menu_charm_vtable};
    uintptr_t *extra_ticks[4]={&profile->menu_shop_tick,&profile->menu_craft_tick,&profile->menu_inlay_tick,&profile->menu_charm_tick};
    uintptr_t *extra_shows[4]={&profile->menu_shop_show,&profile->menu_craft_show,&profile->menu_inlay_show,&profile->menu_charm_show};
    uintptr_t *extra_hovers[4]={&profile->menu_shop_hover,&profile->menu_craft_hover,&profile->menu_inlay_hover,&profile->menu_charm_hover};
    uintptr_t *extra_primary[4]={&profile->menu_shop_primary,&profile->menu_craft_primary,&profile->menu_inlay_primary,&profile->menu_charm_primary};
    for (unsigned i=0;i<4;++i) {
        *extra_tables[i]=(uintptr_t)menu_tables[i+11];*extra_ticks[i]=(uintptr_t)menu_tick;
        *extra_shows[i]=(uintptr_t)menu_show;*extra_hovers[i]=(uintptr_t)menu_hover;*extra_primary[i]=(uintptr_t)grid_primary;
        menu_tables[i+11][0x24/4]=(uintptr_t)grid_primary;
    }
    cursor_position_pointer=(uintptr_t)cursor_position;profile->cursor_position_iat=(uintptr_t)&cursor_position_pointer;
    for (unsigned i=0;i<3;++i) {shop_position_code[i][0]=0xFF;shop_position_code[i][1]=0x15;memcpy(shop_position_code[i]+2,&profile->cursor_position_iat,4);}
    profile->menu_shop_position_call1=(uintptr_t)shop_position_code[0];profile->menu_shop_position_call2=(uintptr_t)shop_position_code[1];
    profile->menu_inlay_position_call=(uintptr_t)shop_position_code[2];profile->menu_shop_switch=(uintptr_t)shop_switch;
    grid_details=grid_swaps=grid_panel_actions=shop_requests=shop_trades=0;memset(grid_player,0,sizeof grid_player);
    for (unsigned i=0;i<136;++i) Write32(grid_player,0xA4+i*4,UINT32_MAX);
    Write32(grid_player,0x2C4,UINT32_MAX);Write32(grid_player,0xA4,1);
    Write32(menu_roots[7],0xC0,0x78);Write32(menu_roots[8],0xC0,0x7F);
    memset(quest_list_data,0,sizeof quest_list_data);ptr(quest_list_data,0xA4,menu_roots[7]);
    Write32(quest_list_data,0x64,1);Write32(quest_list_data,0xE0,3);Write32(quest_list_data,0xF0,20);
    Write32(quest_list_data,0x14,10);Write32(quest_list_data,0x18,20);Write32(quest_list_data,0x1C,200);Write32(quest_list_data,0x20,40);
    ptr(quest_list_data,0xE4,quest_nodes[0]);Write32(combo_list_data,0,3);ptr(combo_list_data,4,combo_nodes[0]);
    for (unsigned i=0;i<3;++i) {
        ptr(quest_nodes[i],0,i<2 ? quest_nodes[i+1]:NULL);ptr(quest_nodes[i],8,quest_records[i]);
    }
    for (unsigned i=0;i<18;++i) ptr(combo_nodes[i],0,i<17 ? combo_nodes[i+1]:NULL);
    combo_methods[0]=combo_methods[1]=1001;combo_methods[2]=1002;reject_skill_learning=false;reject_combo_delete=false;
    quest_switches=quest_selections=skill_actions=skill_details=0;combo_mouse_index=-1;skill_operation_index=-2;
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
    ptr(ui_data,0x18,menu_roots[0]);ptr(ui_data,0x1C,menu_roots[14]);ptr(ui_data,0x20,(void *)0x12345678);
    ptr(unknown_root,0x50,menu_resource);Write32(unknown_root,0x28,0xAA);
    Write32(menu_roots[3],0xC0,6);ptr(menu_roots[3],0xC4,load_nodes[0]);
    for (unsigned i=0;i<6;++i) {
        ptr(load_nodes[i],0,i<5 ? load_nodes[i+1]:NULL);ptr(load_nodes[i],8,load_data[i]);
    }
    memset(hud_data,0,sizeof hud_data);ptr(hud_data,0,menu_tables[15]);Write32(hud_data,0x64,1);
    menu_tables[15][1]=(uintptr_t)menu_tick;menu_tables[15][0x1C/4]=(uintptr_t)menu_show;
    menu_tables[15][0x30/4]=(uintptr_t)menu_hover;
    profile->menu_hud_vtable=(uintptr_t)menu_tables[15];profile->menu_hud_tick=(uintptr_t)menu_tick;
    profile->menu_hud_show=(uintptr_t)menu_show;profile->menu_hud_hover=(uintptr_t)menu_hover;
    for(unsigned i=0;i<12;++i) {
        BYTE *slot=hud_data+0x13C+i*0xE4;ptr(slot,0xA4,hud_data);Write32(slot,0x64,1);
        Write32(slot,0x14,10+(i<6 ? i:i+2)*24);Write32(slot,0x18,350);
        Write32(slot,0x1C,24);Write32(slot,0x20,24);Write32(slot,0xCC,50+i);
    }
    settings_fixture(profile);newgame_fixture(profile);
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
    typedef int (__fastcall *Draw)(void *, void *,void *,int,int,int,int,int);
    CHECK(((Draw)patched_callee((uintptr_t)(cursor_code+16)))(menu_sprites[0][0], NULL,(void *)0x246,anchor.x,anchor.y,0,-1,0)==(expansion ? 0:6));
    CHECK(cursor_draws==(expansion ? 0u:1u));
    if (!expansion) CHECK(cursor_x==174 && cursor_y==224);
    menu_step(KEY(PAD_UP),0,0);CHECK(focus_id(0)==0x21);CHECK(focus_sounds==2);
    for (unsigned i=0;i<10;++i) menu_step(KEY(PAD_UP),0,0);
    CHECK(focus_id(0)==0x21);g_input.now+=160;menu_step(KEY(PAD_UP),0,0);CHECK(focus_id(0)==0x20);
    CHECK(focus_sounds==3);
    CHECK(((This3)menu_tables[0][0x30/4])(menu_roots[0], NULL,12,34,(void *)56)==0);
    CHECK(native_hovers==0 && focus_id(0)==0x20);
    CHECK(((This0)menu_tables[0][1])(menu_roots[0], NULL)==7 && focus_id(0)==0x20);
    CHECK(ReadPtr(ui_data,0x20)==(void *)0x12345678);
    Write32(unknown_root,0x64,1);ptr(ui_data,0x40,unknown_root);
    unsigned routed_reason;CHECK(Menu_Context(&routed_reason)==menu_roots[0]);
    Write32(settings_root,0x64,0);
    Write32(unknown_root,0x64,0);ptr(ui_data,0x40,NULL);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(title_actions==1 && selected_action==0x20);
    for (unsigned i=0;i<7;++i) menu_step(KEY(PAD_A),0,0);
    CHECK(title_actions==1);game_isolated();
    if (!expansion) {
        ((This0)menu_tables[0][1])(menu_roots[0], NULL);CHECK(Read32(menu_children[0][1],0x40)==3);
        neutral_menu();menu_step(KEY(PAD_DOWN)|KEY(PAD_A),0,0);CHECK(title_actions==1);
        ((This0)menu_tables[0][1])(menu_roots[0], NULL);((This0)menu_tables[0][1])(menu_roots[0], NULL);
    }
    neutral_menu();Write32(menu_children[0][1],0x64,0);
    ((This0)menu_tables[0][1])(menu_roots[0], NULL);CHECK(ReadPtr(menu_roots[0],0xA8)==NULL);
    neutral_menu();CHECK(focus_id(0)==0x22);menu_step(KEY(PAD_UP),0,0);CHECK(focus_id(0)==0x21);
    /* 几何导航覆盖外传额外按钮，普通本体不能选到隐藏A7。 */
    for (unsigned i=0;i<6;++i) { neutral_menu();menu_step(KEY(PAD_DOWN),0,0); }
    CHECK(focus_id(0)==(expansion ? 0xA7u:0x23u));
    /* 开页时旧A/左摇杆不执行；系统按视觉最上方选第一项，不能按ID误选底部取消。 */
    Write32(menu_children[1][0],0x18,200);Write32(menu_children[1][1],0x18,50);Write32(menu_children[1][2],0x18,100);Write32(menu_children[1][3],0x18,150);
    world_ptr=old_world;activate_page(1);menu_step(KEY(PAD_A),1,0);CHECK(system_actions==0);
    neutral_menu();CHECK(focus_id(1)==0x30);CHECK(Read32(menu_children[1][1],0x58)==0);
    menu_step(0,0,1);CHECK(focus_id(1)==0x31);
    CHECK(Read32(menu_children[1][1],0x58)==77 && Read32(menu_children[1][2],0x58)==0);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(system_actions==1 && selected_action==0x31);
    /* 物理来源完整回到原hover；新的手柄接管不拿物理悬停当默认选择。 */
    Menu_Suspend();g_intent.layer=LAYER_NATIVE;
    CHECK(((This3)menu_tables[1][0x30/4])(menu_roots[1], NULL,12,34,(void *)56)==9);
    CHECK(native_hovers==1);menu_step(KEY(PAD_A),0,0);CHECK(system_actions==1);
    neutral_menu();CHECK(focus_id(1)==0x30);
    /* capture子对象归一化到确认框，优先于下面仍显示的系统页。 */
    ((This2)menu_tables[2][0x1C/4])(menu_roots[2], NULL,1,0);
    ptr(ui_data,0x3C,menu_children[2][2]);ptr(ui_data,0x40,menu_roots[1]);
    unsigned reason;CHECK(Menu_Context(&reason)==menu_roots[2] && reason==1);
    neutral_menu();CHECK(focus_id(2)==0x9A);
    menu_step(KEY(PAD_RIGHT),0,0);CHECK(focus_id(2)==0x9B);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(!Read32(menu_roots[2],0x64) && confirm_actions==0);
    CHECK(Menu_CapturesInput());game_isolated();
    ((This2)menu_tables[2][0x1C/4])(menu_roots[2], NULL,1,0);neutral_menu();
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
    /* 第一页末项下接第二页首项，第二页首项上接第一页末项；不改变显式左右分页。 */
    neutral_menu();menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(menu_roots[3],0xD0)==0 && Read32(menu_roots[3],0xD4)==1);
    for (unsigned i=0;i<2;++i) {neutral_menu();menu_step(KEY(PAD_DOWN),0,0);}
    CHECK(Read32(menu_roots[3],0xD4)==3);
    neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(menu_roots[3],0xD0)==4 && Read32(menu_roots[3],0xD4)==0);
    neutral_menu();menu_step(KEY(PAD_UP),0,0);CHECK(Read32(menu_roots[3],0xD0)==0 && Read32(menu_roots[3],0xD4)==3);
    for (unsigned i=0;i<4;++i) {neutral_menu();menu_step(KEY(PAD_UP),0,0);}
    CHECK(Read32(menu_roots[3],0xD0)==0 && Read32(menu_roots[3],0xD4)==0);
    neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();menu_step(KEY(PAD_DOWN),0,0);
    CHECK(Read32(menu_roots[3],0xD0)==4 && Read32(menu_roots[3],0xD4)==1);
    /* 第三页只有一项：连续下到8号，上回第二页末项；最后一页下不能回绕。 */
    Write32(menu_roots[3],0xC0,9);
    for (unsigned i=0;i<9;++i) {ptr(load_nodes[i],0,i<8 ? load_nodes[i+1]:NULL);ptr(load_nodes[i],8,load_data[i]);}
    for (unsigned i=0;i<3;++i) {neutral_menu();menu_step(KEY(PAD_DOWN),0,0);}
    CHECK(Read32(menu_roots[3],0xD0)==8 && Read32(menu_roots[3],0xD4)==0);
    neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(menu_roots[3],0xD0)==8 && Read32(menu_roots[3],0xD4)==0);
    neutral_menu();menu_step(KEY(PAD_UP),0,0);CHECK(Read32(menu_roots[3],0xD0)==4 && Read32(menu_roots[3],0xD4)==3);
    Write32(menu_roots[3],0xC0,6);neutral_menu();menu_step(KEY(PAD_UP),0,0);
    CHECK(Read32(menu_roots[3],0xD4)==1);
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
    /* 原删除可以留下D0=8而总数只剩8的空尾页；新逻辑退到4并聚焦其末项。 */
    Write32(menu_roots[3],0xC0,8);Write32(menu_roots[3],0xD0,8);
    neutral_menu();CHECK(Read32(menu_roots[3],0xD0)==4 && Read32(menu_roots[3],0xD4)==3);
    menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(menu_roots[3],0xD0)==0);
    Write32(menu_roots[3],0xC0,0);Write32(menu_roots[3],0xD0,0);neutral_menu();
    menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(menu_roots[3],0xD0)==0 && !Menu_CursorAnchor(&anchor));
    /* NPC选项按脚本记录，提示项不选；Tick原命中调用被独立焦点替代。 */
    activate_page(5);neutral_menu();CHECK(Read32(ReadPtr(menu_roots[5],0xA8),0xC4)==10);
    ((This0)menu_tables[5][1])(menu_roots[5], NULL);CHECK(Read32(ReadPtr(menu_roots[5],0xA8),0xC4)==10);
    menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(ReadPtr(menu_roots[5],0xA8),0xC4)==20);
    talk_delay=2;neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(talk_selected==0);
    talk_delay=0;neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(talk_selected==20);
    neutral_menu();menu_step(KEY(PAD_B),0,0);CHECK(talk_cancelled==1);
    activate_page(6);neutral_menu();int before_scroll=(int)Read32(menu_roots[6],0xF4);
    menu_step(KEY(PAD_A),0,0);CHECK(text_next_count==0 && Read32(menu_roots[6],0x64));
    ((This0)menu_tables[6][1])(menu_roots[6], NULL);CHECK((int)Read32(menu_roots[6],0xF4)==before_scroll-3 && text_next_count==0);
    menu_step(0,0,0);before_scroll=(int)Read32(menu_roots[6],0xF4);
    ((This0)menu_tables[6][1])(menu_roots[6], NULL);CHECK((int)Read32(menu_roots[6],0xF4)==before_scroll-1);
    menu_step(KEY(PAD_B),0,0);CHECK(text_next_count==1);
    /* 日志使用原列表编排而非伪造方向键，分类切换不泄漏世界药品/投掷快捷键。 */
    activate_page(7);neutral_menu();CHECK(Menu_CursorAnchor(&anchor));CHECK(anchor.y==34);
    menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(quest_list_data,0xF4)==1 && quest_selections==1);
    CHECK(Menu_CursorAnchor(&anchor) && anchor.y==34);
    for (unsigned i=0;i<5;++i) menu_step(KEY(PAD_DOWN),0,0);
    CHECK(quest_selections==1);g_input.now+=400;menu_step(KEY(PAD_DOWN),0,0);
    CHECK(Read32(quest_list_data,0xF4)==2 && quest_selections==2);
    neutral_menu();menu_step(KEY(PAD_RB),0,0);CHECK(Read32(menu_roots[7],0xC0)==0x79 && quest_switches==1);
    for (unsigned i=0;i<5;++i) menu_step(KEY(PAD_RB),0,0);
    CHECK(quest_switches==1);game_isolated();
    neutral_menu();menu_step(KEY(PAD_LB),0,0);CHECK(Read32(menu_roots[7],0xC0)==0x78);
    neutral_menu();menu_step(KEY(PAD_LB),0,0);CHECK(Read32(menu_roots[7],0xC0)==0x7B);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(quest_switches==3 && quest_selections==2);
    /* 原fixture视口40像素、行高20，左右每次跨2项；LB/RB仍是分类切换。 */
    neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(quest_list_data,0xF4)==2 && quest_selections==3);
    neutral_menu();menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(quest_list_data,0xF4)==0 && quest_selections==4);
    neutral_menu();menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(quest_list_data,0xF4)==0 && quest_selections==4 && quest_switches==3);
    Write32(quest_list_data,0xE0,0);CHECK(!Menu_CursorAnchor(&anchor));
    neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(quest_selections==4);
    neutral_menu();menu_step(KEY(PAD_B),0,0);CHECK(!Read32(menu_roots[7],0x64));
    /* 技能学习焦点经过原base Tick后仍在，详情不会受物理鼠标悬停清空。 */
    activate_page(8);neutral_menu();CHECK(focus_id(8)==0x84);
    CHECK(Menu_CursorAnchor(&anchor) && anchor.y==224);
    ((This0)menu_tables[8][1])(menu_roots[8], NULL);CHECK(focus_id(8)==0x84 && skill_details==1);
    menu_step(KEY(PAD_DOWN),0,0);CHECK(focus_id(8)==0x85);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(skill_actions==1 && selected_action==0x85);
    neutral_menu();menu_step(KEY(PAD_RB),0,0);CHECK(Read32(menu_roots[8],0xC0)==0x80);
    neutral_menu();CHECK(focus_id(8)==0x84);
    /* X只切上下区域，完全不发删除；连招条是原六列布局，A带自己的节点序号。 */
    menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(ReadPtr(menu_roots[8],0xA8)==NULL);
    CHECK(skill_actions==1 && skill_operation_index==-1);CHECK(Menu_CursorAnchor(&anchor));
    CHECK(anchor.x==77 && anchor.y==101);
    menu_step(KEY(PAD_RIGHT),0,0);CHECK(Menu_CursorAnchor(&anchor) && anchor.x==121);
    Write32(combo_list_data,0,12);neutral_menu();menu_step(KEY(PAD_DOWN),0,0);
    CHECK(Menu_CursorAnchor(&anchor) && anchor.x==121 && anchor.y==149);
    neutral_menu();menu_step(KEY(PAD_UP),0,0);CHECK(Menu_CursorAnchor(&anchor) && anchor.y==101);
    Write32(combo_list_data,0,3);
    combo_mouse_index=2;neutral_menu();menu_step(KEY(PAD_A),0,0);
    CHECK(skill_actions==2 && skill_operation_index==1);game_isolated();
    CHECK(Read32(combo_list_data,0)==2 && combo_methods[0]==1001 && combo_methods[1]==1002);
    for (unsigned i=0;i<5;++i) menu_step(KEY(PAD_A),0,0);
    CHECK(skill_actions==2);
    for (unsigned i=0;i<4;++i) {neutral_menu();menu_step(KEY(PAD_Y),0,0);CHECK(Read32(menu_roots[8],0xC4)==(i+1)%4);}
    neutral_menu();menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(focus_id(8)==0x84);
    menu_step(KEY(PAD_A),0,0);CHECK(skill_actions==3 && skill_operation_index==-1 && selected_action==0x84);
    CHECK(Read32(combo_list_data,0)==3 && combo_methods[2]==1001);
    neutral_menu();menu_step(KEY(PAD_X),0,0);neutral_menu();Write32(combo_list_data,0,0);
    CHECK(Menu_CursorAnchor(&anchor));menu_step(KEY(PAD_A),0,0);CHECK(skill_operation_index==-1);
    /* 真实鼠标接管后，原命中函数恢复自己的鼠标节点；手柄没有改写全局鼠标。 */
    Menu_Suspend();g_intent.layer=LAYER_NATIVE;
    CHECK(((This0)patched_callee((uintptr_t)skill_calls[1]))(menu_roots[8], NULL)==2);
    neutral_menu();menu_step(KEY(PAD_LB),0,0);CHECK(Read32(menu_roots[8],0xC0)==0x7F);
    neutral_menu();menu_step(KEY(PAD_B)|KEY(PAD_RB),0,0);CHECK(!Read32(menu_roots[8],0x64) && Read32(menu_roots[8],0xC0)==0x7F);
    /* 未实现页面保留B/方向旧键盘能力，仍绝不注入Enter或直接猜+3C。 */
    for (unsigned k=0;k<15;++k) Write32(menu_roots[k],0x64,0);
    Write32(unknown_root,0x64,1);ptr(ui_data,0x18,unknown_root);ptr(ui_data,0x1C,unknown_root);
    neutral_menu();menu_step(KEY(PAD_B)|KEY(PAD_A),0,0);
    BYTE keys[256]={0};Game_Keyboard(keys);CHECK(keys[VK_ESCAPE]==0x80 && keys[VK_RETURN]==0);
    /* 普通手柄世界不画鼠标图样，物理来源仍用原位置与全部参数。 */
    g_intent.layer=LAYER_GAME;
    CHECK(((Draw)patched_callee((uintptr_t)(cursor_code+16)))(menu_sprites[0][0], NULL,(void *)0x246,37,49,0,-1,0)==0);
    g_intent.layer=LAYER_NATIVE;CHECK(((BOOL (WINAPI *)(POINT *))patched_callee((uintptr_t)cursor_code))(&anchor));CHECK(anchor.x==37 && anchor.y==49);
    CHECK(((Draw)patched_callee((uintptr_t)(cursor_code+8)))(menu_sprites[0][0], NULL,(void *)0x246,37,49,0,-1,0)==6);
    Cursor_Shutdown();CHECK(cursor_code[0]==0xFF && cursor_code[1]==0x15);
    Menu_Shutdown();
    for (unsigned k=0;k<15;++k) CHECK(menu_tables[k][1]==(uintptr_t)menu_tick && menu_tables[k][0x30/4]==(uintptr_t)menu_hover);
    HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
    CHECK(!Menu_BlocksGameplay());
    /* 每个入口写失败都回滚，不能留下半套Tick/Show/hover。所有原槽先核对再允许重试。 */
    for (unsigned fail=1;fail<=64;++fail) {
        patch_attempt=0;patch_fail_at=fail;CHECK(!Menu_Initialize());
        for (unsigned k=0;k<15;++k) {
            CHECK(menu_tables[k][1]==(uintptr_t)menu_tick);
            CHECK(menu_tables[k][0x1C/4]==(uintptr_t)menu_show);
            CHECK(menu_tables[k][0x30/4]==(uintptr_t)menu_hover);
        }
        CHECK(menu_tables[16][1]==(uintptr_t)menu_tick && menu_tables[16][0x1C/4]==(uintptr_t)menu_show && menu_tables[16][0x30/4]==(uintptr_t)menu_hover);
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
static void combo_select_position(unsigned index)
{
    /* 通过真实导航回到首格，再定位测试位置；不直接改插件内部焦点来绕过行为验证。 */
    for (unsigned i=0;i<3;++i) {neutral_menu();menu_step(KEY(PAD_UP),0,0);}
    for (unsigned i=0;i<6;++i) {neutral_menu();menu_step(KEY(PAD_LEFT),0,0);}
    for (unsigned i=0;i<index%6;++i) {neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);}
    for (unsigned i=0;i<index/6;++i) {neutral_menu();menu_step(KEY(PAD_DOWN),0,0);}
    neutral_menu();
}
static void combo_check_anchor(unsigned index)
{
    POINT anchor;CHECK(Menu_CursorAnchor(&anchor));
    CHECK(anchor.x==77+(LONG)(index%6)*44 && anchor.y==101+(LONG)(index/6)*48);
}
static void combo_delete_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);
    activate_page(8);neutral_menu();menu_step(KEY(PAD_RB),0,0);neutral_menu();
    menu_step(KEY(PAD_X),0,0);neutral_menu();Write32(combo_list_data,0,18);
    for (unsigned i=0;i<18;++i) combo_methods[i]=1000+i;
    combo_select_position(17);combo_check_anchor(17);
    reject_combo_delete=true;menu_step(KEY(PAD_A),0,0);
    CHECK(Read32(combo_list_data,0)==18);combo_check_anchor(17);
    reject_combo_delete=false;neutral_menu();
    menu_step(KEY(PAD_A),0,0);CHECK(Read32(combo_list_data,0)==17);
    /* 原调用返回的同一帧就位于新末项；不能等下一帧才回退或跳首格。 */
    combo_check_anchor(16);CHECK(combo_methods[16]==1016);
    unsigned actions=skill_actions;
    for (unsigned i=0;i<6;++i) menu_step(KEY(PAD_A),0,0);
    CHECK(skill_actions==actions && Read32(combo_list_data,0)==17);
    neutral_menu();menu_step(KEY(PAD_A),0,0);combo_check_anchor(15);
    CHECK(Read32(combo_list_data,0)==16);
    combo_select_position(7);menu_step(KEY(PAD_A),0,0);combo_check_anchor(7);
    CHECK(Read32(combo_list_data,0)==15 && combo_methods[7]==1008);
    combo_select_position(0);menu_step(KEY(PAD_A),0,0);combo_check_anchor(0);
    CHECK(combo_methods[0]==1001 && Read32(combo_list_data,0)==14);
    /* 六列换行边界：第二行首项被删后仍取补位项；删首行末项后停在新末项。 */
    Write32(combo_list_data,0,7);combo_select_position(6);menu_step(KEY(PAD_A),0,0);
    CHECK(Read32(combo_list_data,0)==6);combo_check_anchor(5);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(Read32(combo_list_data,0)==5);combo_check_anchor(4);
    Write32(combo_list_data,0,1);combo_select_position(0);menu_step(KEY(PAD_A),0,0);
    CHECK(Read32(combo_list_data,0)==0);combo_check_anchor(0);
    actions=skill_actions;neutral_menu();menu_step(KEY(PAD_A),0,0);
    CHECK(skill_operation_index==-1 && Read32(combo_list_data,0)==0 && skill_actions==actions+1);
    game_isolated();Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
/* 此大型宿主回放不内联，保留清晰的测试调用边界和源码调试位置。
 * 只约束测试宿主的组织方式；生产代码的优化设置和实际回放步骤都不改变。 */
static void __declspec(noinline) grid_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);activate_page(9);neutral_menu();
    CHECK(Read32(menu_roots[9],0xFC)==0 && Read32(menu_roots[9],0xBC)==1);
    POINT anchor;CHECK(Menu_CursorAnchor(&anchor) && anchor.x==58 && anchor.y==78);
    ((This0)menu_tables[9][1])(menu_roots[9], NULL);CHECK(grid_details==1 && Read32(menu_roots[9],0xBC)==1);
    /* 左上边界不绕行；空格也可聚焦，保证手柄能放置物品。 */
    menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(menu_roots[9],0xFC)==0);
    neutral_menu();menu_step(KEY(PAD_UP),0,0);CHECK(Read32(menu_roots[9],0xFC)==0);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(Read32(grid_player,0x2C4)==1);
    CHECK(Menu_CursorAnchor(&anchor) && anchor.x==52 && anchor.y==72);
    CHECK(Read32(grid_player,0xA4)==UINT32_MAX);
    neutral_menu();menu_step(KEY(PAD_B),0,0);
    CHECK(Read32(grid_player,0x2C4)==UINT32_MAX && Read32(grid_player,0xA4)==1 && Read32(menu_roots[9],0x64));
    CHECK(Menu_CursorAnchor(&anchor) && anchor.x==58 && anchor.y==78);
    neutral_menu();menu_step(KEY(PAD_A),0,0);neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);
    Write32(grid_player,0xA8,2);neutral_menu();menu_step(KEY(PAD_A),0,0);
    CHECK(Read32(grid_player,0xA8)==1 && Read32(grid_player,0x2C4)==2);
    /* 交换后原格被占据，B找空位而不是再拿出另一件；仍不关闭菜单。 */
    neutral_menu();menu_step(KEY(PAD_B),0,0);
    CHECK(Read32(grid_player,0xA4)==2 && Read32(grid_player,0x2C4)==UINT32_MAX);
    unsigned before=grid_swaps;neutral_menu();menu_step(KEY(PAD_X),0,0);
    CHECK(grid_swaps==before && !Read32(menu_roots[10],0x64));
    CHECK(Menu_Context(&(unsigned){0})==hud_data);neutral_menu();menu_step(KEY(PAD_X),0,0);neutral_menu();
    ((This2)menu_tables[10][0x1C/4])(menu_roots[10], NULL,1,0);neutral_menu();
    menu_step(KEY(PAD_X),0,0);neutral_menu();
    CHECK(Read32(menu_roots[10],0xC0)==0 && Read32(menu_roots[9],0xFC)==UINT32_MAX);
    CHECK(Menu_CursorAnchor(&anchor));unsigned reason;CHECK(Menu_Context(&reason)==menu_roots[10]);
    CHECK(grid_swaps==before);game_isolated();
    menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(menu_roots[10],0xC0)==1);
    neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(menu_roots[10],0xC0)==11);
    Write32(grid_player,0xA4+(86+11)*4,3);neutral_menu();menu_step(KEY(PAD_A),0,0);
    CHECK(Read32(grid_player,0x2C4)==3);neutral_menu();menu_step(KEY(PAD_X),0,0);neutral_menu();
    CHECK(Menu_Context(&reason)==hud_data);menu_step(KEY(PAD_X),0,0);neutral_menu();
    CHECK(Menu_Context(&reason)==menu_roots[9] && Read32(menu_roots[9],0xFC)==1);
    /* 原来源记忆经过X切页仍有效，B可以把仓库拿起的物品放回原仓库逻辑槽。 */
    menu_step(KEY(PAD_B),0,0);CHECK(Read32(grid_player,0x2C4)==UINT32_MAX && Read32(grid_player,0xA4+97*4)==3);
    /* 10列5行的末格与上下左右边界真实导航，不将二维边缘误当线性下一格。 */
    for (unsigned i=0;i<9;++i) {neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);}
    for (unsigned i=0;i<4;++i) {neutral_menu();menu_step(KEY(PAD_DOWN),0,0);}
    CHECK(Read32(menu_roots[9],0xFC)==49);
    neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(menu_roots[9],0xFC)==49);
    neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(menu_roots[9],0xFC)==49);
    for (unsigned i=0;i<4;++i) {neutral_menu();menu_step(KEY(PAD_UP),0,0);}
    for (unsigned i=0;i<9;++i) {neutral_menu();menu_step(KEY(PAD_LEFT),0,0);}
    CHECK(Read32(menu_roots[9],0xFC)==0);
    neutral_menu();menu_step(KEY(PAD_Y),0,0);neutral_menu();CHECK(Read32(menu_roots[9],0xFC)==UINT32_MAX);
    CHECK(ReadPtr(menu_roots[9],0xA8));menu_step(KEY(PAD_DOWN),0,0);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(grid_panel_actions==1 && selected_action==0x16);
    neutral_menu();menu_step(KEY(PAD_Y),0,0);neutral_menu();CHECK(Read32(menu_roots[9],0xFC)==0);
    /* 已满背包又持有物品时，B不丢物品或关闭窗口；用户仍可手动选择放置目标。 */
    for (unsigned i=0;i<50;++i) Write32(grid_player,0xA4+i*4,i+10);
    Write32(grid_player,0x2C4,99);menu_step(KEY(PAD_B),0,0);
    CHECK(Read32(menu_roots[9],0x64) && Read32(grid_player,0x2C4)==99);
    Write32(grid_player,0x2C4,UINT32_MAX);neutral_menu();menu_step(KEY(PAD_B),0,0);
    CHECK(!Read32(menu_roots[9],0x64) && Read32(menu_roots[10],0x64));
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
static void __declspec(noinline) all_regions_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);activate_page(9);neutral_menu();
    /* 辅助页不在普通根链，属性13也为0，但原登记对象和显示状态确实存在。 */
    ptr(ui_data,0x1C,menu_roots[9]);ptr(menu_roots[9],0x0C,NULL);
    for (unsigned k=10;k<15;++k) {Write32(menu_roots[k],0x64,k!=13);ptr(menu_roots[k],0x50,nonroot_resource);}
    unsigned reason;
    for (unsigned k=10;k<15;++k) {
        if (k==13) continue;
        menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(Menu_Context(&reason)==menu_roots[k]);
        ((This0)menu_tables[k][1])(menu_roots[k], NULL);CHECK(Read32(menu_roots[k],0xBC)==1);
    }
    menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(Menu_Context(&reason)==hud_data);
    menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(Menu_Context(&reason)==menu_roots[9]);
    /* 隐藏页不被X打开或选到；新根链也不影响登记页循环。 */
    Write32(menu_roots[10],0x64,0);menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(Menu_Context(&reason)==menu_roots[11]);
    unsigned shop_mode=Read32(menu_roots[11],0xD0);
    menu_step(KEY(PAD_RB),0,0);neutral_menu();CHECK(Read32(menu_roots[11],0xD0)==shop_mode && Menu_Context(&reason)==menu_roots[11]);
    menu_step(KEY(PAD_LB),0,0);neutral_menu();CHECK(Read32(menu_roots[11],0xD0)==shop_mode);
    menu_step(KEY(PAD_Y),0,0);neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(Read32(menu_roots[11],0xD0)==0x57);
    neutral_menu();menu_step(KEY(PAD_Y),0,0);neutral_menu();menu_step(KEY(PAD_A),0,0);
    CHECK(shop_requests==1 && shop_trades==0);neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(shop_trades==1);neutral_menu();CHECK(Menu_Context(&reason)==menu_roots[11]);
    /* 单独三种特殊页均按真实槽控件调用原业务；持有图样取槽中心，不破坏其它字段。 */
    for (unsigned k=12;k<15;++k) {
        activate_page(k);neutral_menu();unsigned logical=k==12 ? 62:k==13 ? 69:81;
        Write32(menu_roots[k],0xC0,0x12345678);Write32(grid_player,0xA4+logical*4,7+k);
        CHECK(ReadPtr(menu_roots[k],0xA8)==menu_children[k][0]);
        menu_step(KEY(PAD_A),0,0);CHECK(Read32(grid_player,0x2C4)==7+k);
        POINT anchor;CHECK(Menu_CursorAnchor(&anchor));CHECK(anchor.x==140 && anchor.y==65);
        CHECK(Read32(menu_roots[k],0xC0)==0x12345678);
        neutral_menu();menu_step(KEY(PAD_B),0,0);CHECK(Read32(grid_player,0x2C4)==UINT32_MAX && Read32(menu_roots[k],0x64));
        if (k==12 || k==13) {neutral_menu();menu_step(KEY(PAD_Y),0,0);neutral_menu();CHECK(Read32(ReadPtr(menu_roots[k],0xA8),0x28)==(k==12 ? 0x3Fu:0x48u));}
        neutral_menu();menu_step(KEY(PAD_B),0,0);CHECK(!Read32(menu_roots[k],0x64));
    }
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
static void __declspec(noinline) modal_region_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);unsigned reason;
    for (unsigned k=9;k<15;++k) {
        activate_page(k);neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();
        POINT before;CHECK(Menu_CursorAnchor(&before));
        ptr(menu_roots[4],0xCC,menu_roots[k]);((This2)menu_tables[4][0x1C/4])(menu_roots[4], NULL,1,0);
        neutral_menu();CHECK(Menu_Context(&reason)==menu_roots[4]);
        menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();menu_step(KEY(PAD_A),0,0);neutral_menu();
        CHECK(Menu_Context(&reason)==menu_roots[k]);POINT after;CHECK(Menu_CursorAnchor(&after));
        CHECK(before.x==after.x && before.y==after.y);
    }
    /* 镶嵌会话的第三个非模态说明页不抢焦点，X仅循环道具箱与镶嵌。 */
    activate_page(9);Write32(menu_roots[13],0x64,1);Write32(menu_roots[14],0x64,1);
    Write32(unknown_root,0x64,1);ptr(unknown_root,0x0C,menu_roots[9]);ptr(ui_data,0x18,unknown_root);
    neutral_menu();menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(Menu_Context(&reason)==menu_roots[13]);
    menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(Menu_Context(&reason)==hud_data);
    menu_step(KEY(PAD_X),0,0);neutral_menu();CHECK(Menu_Context(&reason)==menu_roots[9]);
    BYTE keys[256]={0};g_intent.pressed=KEY(PAD_R3);Game_Keyboard(keys);CHECK(keys[VK_TAB]==0x80);
    /* 依附已关闭页的过期辅助窗口不能继续截住世界输入。 */
    Write32(settings_root,0x64,0);
    Write32(unknown_root,0x64,0);Write32(menu_roots[9],0x64,0);Write32(menu_roots[14],0x64,0);
    Write32(menu_roots[13],0xAC,0x14);ptr(ui_data,0x3C,menu_roots[13]);
    CHECK(Menu_Context(&reason)==NULL);neutral_menu();CHECK(!Game_Menu());
    /* 旧辅助窗口不再挡世界A：这一步经过真实Game_Update/Inspect原事件路径。 */
    menu_step(KEY(PAD_A),0,0);last_opcode=0;Game_Update();CHECK(last_opcode==19);
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
/* 原动作菜单替身保留真实节点布局；生产导航和Control/Game仍直接链接。 */
static BYTE action_root[0xF0],action_nodes[2][3][0x40];
static uintptr_t action_table[16];static void *action_pointer=action_root;
static unsigned action_commits,action_builds;
static int __fastcall action_rebuild(void *root, void *unused_edx,int side)
{ (void)unused_edx;
    CHECK(root==action_root && (side==0 || side==1));++action_builds;
    Write32(root,0xC8,side);ptr(root,0xE8,action_nodes[side][0]);
    return 1;
}
static int __fastcall action_open(void *root, void *unused_edx,int side)
{ (void)unused_edx;action_rebuild(root, NULL,side);Write32(root,0x64,1);ptr(ui_data,0x3C,root);return 1;}
static int __fastcall action_commit(void *root, void *unused_edx)
{ (void)unused_edx;
    CHECK(root==action_root);void *candidate=ReadPtr(root,0xC0);
    if (candidate) {Write32(hud_data,Read32(root,0xC8) ? 0x128:0x12C,Read32(candidate,0x28));++action_commits;}
    Write32(root,0x64,0);ptr(ui_data,0x3C,NULL);return 1;
}
static int __fastcall action_base(void *root, void *unused_edx) { (void)unused_edx;CHECK(root==action_root);ptr(root,0xC0,NULL);return 7;}
static void action_step_axes(unsigned buttons,float lx,float ly,float rx,float ry)
{
    g_input.now+=20;g_input.buttons=buttons;g_input.lx=lx;g_input.ly=ly;g_input.rx=rx;g_input.ry=ry;
    g_input.menu=Game_Menu();g_input.action_menu=ActionMenu_Active();
    g_intent=Control_Step(&menu_control,&g_input);
    bool was_open=g_input.action_menu,closed=ActionMenu_Update();
    if (closed) Control_BlockMenuInputs(&menu_control,&g_input);
    if (!was_open && !closed && !ActionMenu_Active()) Menu_Update();
}
static void action_step(unsigned buttons,float rx,float ry)
{action_step_axes(buttons,0,0,rx,ry);}
static void action_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);
    Write32(combo_list_data,0,0); /* 本回放明确使用空套组，不能继承此前连招编辑样本。 */
    for (unsigned k=0;k<15;++k) Write32(menu_roots[k],0x64,0);
    ptr(ui_data,0x18,NULL);ptr(ui_data,0x1C,NULL);ptr(ui_data,0x3C,NULL);
    memset(action_root,0,sizeof action_root);memset(action_nodes,0,sizeof action_nodes);
    action_table[1]=(uintptr_t)action_base;action_table[0x30/4]=(uintptr_t)menu_hover;
    ptr(action_root,0,action_table);
    for (unsigned side=0;side<2;++side) for (unsigned i=0;i<3;++i) {
        BYTE *node=action_nodes[side][i];ptr(node,8,i<2 ? action_nodes[side][i+1]:NULL);
        Write32(node,0x14,100+i*32);Write32(node,0x18,100);Write32(node,0x1C,30);Write32(node,0x20,30);
        Write32(node,0x28,i==0 ? 111:i==1 ? 222:(uint32_t)-1);
    }
    profile.menu_action_vtable=(uintptr_t)action_table;profile.menu_action_global=(uintptr_t)&action_pointer;
    profile.menu_action_open=(uintptr_t)action_open;profile.menu_action_rebuild=(uintptr_t)action_rebuild;
    profile.menu_action_commit=(uintptr_t)action_commit;profile.menu_action_tick=(uintptr_t)action_base;
    profile.menu_action_hover=(uintptr_t)menu_hover;profile.menu_message_base_tick=(uintptr_t)action_base;
    /* 两个写槽失败各自回滚，不留下半装菜单。 */
    for (unsigned fail=1;fail<=2;++fail) {
        patch_attempt=0;patch_fail_at=fail;CHECK(!ActionMenu_Initialize());
        CHECK(action_table[1]==(uintptr_t)action_base && action_table[12]==(uintptr_t)menu_hover);
    }
    patch_attempt=patch_fail_at=0;CHECK(ActionMenu_Initialize());action_commits=action_builds=0;
    Write32(hud_data,0x128,111);Write32(hud_data,0x12C,111);
    action_step(0,0,0);action_step(0,0,0);g_input.lt=g_input.rt=true;
    action_step(0,0,0);CHECK(!ActionMenu_Active() && g_intent.layer==LAYER_DUAL);
    action_step(0,1,0);CHECK(!ActionMenu_Active() && action_builds==0);
    action_step(KEY(PAD_L3)|KEY(PAD_R3),0,0);CHECK(!ActionMenu_Active());action_step(0,0,0);
    action_step(KEY(PAD_R3),0,0);CHECK(ActionMenu_Active() && action_builds==1 && Read32(action_root,0xC8)==0);
    CHECK(((This0)action_table[1])(action_root, NULL)==7 && ReadPtr(action_root,0xC0)==action_nodes[0][0]);
    action_step(0,0,0);action_step(0,1,0);CHECK(ReadPtr(action_root,0xC0)==action_nodes[0][1]);
    CHECK(Menu_HidesCursor());emit_focus();CHECK(focus_draws>1 && focus_rectangle.left==133 && focus_rectangle.top==101 && focus_rectangle.right==161 && focus_rectangle.bottom==129);
    CHECK(Read32(hud_data,0x12C)==111); /* 预览不能直接改右手 */
    action_step(KEY(PAD_L3),0,0);CHECK(Read32(action_root,0xC8)==1 && action_commits==0);
    unsigned builds_before=action_builds;action_step(KEY(PAD_L3),0,0);CHECK(action_builds==builds_before);
    action_step(0,0,0);action_step(KEY(PAD_L3)|KEY(PAD_R3),0,0);CHECK(Read32(action_root,0xC8)==1 && action_builds==builds_before);
    action_step(0,0,0);action_step(KEY(PAD_R3),0,0);CHECK(Read32(action_root,0xC8)==0 && action_builds==builds_before+1);
    action_step(0,0,0);action_step(KEY(PAD_L3),0,0);CHECK(Read32(action_root,0xC8)==1 && action_commits==0);
    action_step(0,0,0);action_step_axes(0,1,0,0,0);CHECK(ReadPtr(action_root,0xC0)==action_nodes[1][1]);
    BYTE keys[256]={0};g_intent.pressed=KEY(PAD_R3)|KEY(PAD_A)|KEY(PAD_START);Game_Keyboard(keys);
    for (unsigned i=0;i<256;++i) CHECK(keys[i]==0);
    unsigned old_releases=releases;Game_Update();CHECK((unsigned)releases==old_releases);
    g_input.rt=false;action_step(KEY(PAD_A),1,0);
    CHECK(!ActionMenu_Active() && action_commits==1 && Read32(hud_data,0x128)==222 && Read32(hud_data,0x12C)==111);
    action_step(KEY(PAD_A),1,0);CHECK(g_intent.layer==LAYER_GUARD && !g_intent.held && !g_intent.rx);
    action_step(0,0,0);g_input.rt=true;action_step(KEY(PAD_L3),0,0);CHECK(ActionMenu_Active());
    g_input.lt=false;action_step(0,0,0);CHECK(!ActionMenu_Active() && action_commits==2);
    /* 换图、角色变化只关闭，绝不提交尚未确认的候选。 */
    action_step(0,0,0);g_input.lt=true;action_step(KEY(PAD_L3),0,0);CHECK(ActionMenu_Active());
    Write32(manager_data,0x0C,2);action_step(0,0,0);CHECK(!ActionMenu_Active() && action_commits==2);
    Write32(manager_data,0x0C,1);action_step(0,0,0);action_step(KEY(PAD_L3),0,0);CHECK(ActionMenu_Active());
    Write32(world_data,0x58,0);action_step(0,0,0);CHECK(!ActionMenu_Active() && action_commits==2);
    Write32(world_data,0x58,1);action_step(0,0,0);action_step(KEY(PAD_L3),0,0);CHECK(ActionMenu_Active());
    g_intent.layer=LAYER_NATIVE;CHECK(!ActionMenu_Update() && !ActionMenu_Active() && Read32(action_root,0x64));
    /* 三种模式分别在两个侧别回放。闲置摇杆不夺焦点，双杆同时输入只采负责的一根。 */
    for(int mode=0;mode<3;++mode)for(unsigned side=0;side<2;++side){
        ActionMenu_Suspend();Write32(action_root,0x64,0);ptr(ui_data,0x3C,NULL);
        memset(&menu_control,0,sizeof menu_control);g_input.lt=g_input.rt=false;
        Write32(hud_data,side ? 0x128:0x12C,111);test_action_menu_nav=mode;
        action_step(0,0,0);action_step(0,0,0);g_input.lt=g_input.rt=true;action_step(0,0,0);action_step(side ? KEY(PAD_L3):KEY(PAD_R3),0,0);
        CHECK(ActionMenu_Active() && Read32(action_root,0xC8)==side);
        bool left=mode==0 || (mode==2 && side==1);
        action_step(0,0,0);
        action_step_axes(0,left ? 0:1,0,left ? 1:0,0);
        CHECK(ReadPtr(action_root,0xC0)==action_nodes[side][0]);
        action_step(0,0,0);
        unsigned prior_builds=action_builds,prior_commits=action_commits;
        action_step_axes(0,left ? 1:-1,0,left ? -1:1,0);
        CHECK(ReadPtr(action_root,0xC0)==action_nodes[side][1]);
        CHECK(action_builds==prior_builds && action_commits==prior_commits && Read32(action_root,0xC8)==side);
        CHECK(Read32(hud_data,side ? 0x128:0x12C)==111);
        /* 同方向持续推动可连发，回中后反向能返回；空连招候选仍不能进入。 */
        g_input.now+=400;action_step_axes(0,left ? 1:0,0,left ? 0:1,0);
        CHECK(ReadPtr(action_root,0xC0)==action_nodes[side][1]);
        action_step(0,0,0);action_step_axes(0,left ? -1:0,0,left ? 0:-1,0);
        CHECK(ReadPtr(action_root,0xC0)==action_nodes[side][0]);
        g_input.rt=false;action_step_axes(0,1,0,1,0);
        CHECK(!ActionMenu_Active() && action_commits==prior_commits+1);
        action_step_axes(0,1,0,1,0);CHECK(g_intent.lx==0 && g_intent.rx==0);
    }
    test_action_menu_nav=2;
    ActionMenu_Shutdown();CHECK(action_table[1]==(uintptr_t)action_base && action_table[12]==(uintptr_t)menu_hover);
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
/* HUD原点击替身：确认生产代码传来格中心和50..61原槽编号，再由原业务交换。 */
static unsigned quick_button_dispatches;static bool quick_reject_type;
static int __fastcall quick_primary(void *root, void *unused_edx,int event,int x,void *y)
{ (void)unused_edx;
    CHECK(root==hud_data && event==0);
    /* 原HUD主操作先调用自己的hover，再用两个坐标取快捷格；旧替身直接
     * 从A8取槽，漏掉原函数后半段，恰好接受了生产代码的错误对象。
     * 此处按原控制流重放：hover重新投影也不能把快捷格写回主按钮A8。 */
    ((This3)menu_tables[15][0x30/4])(root, NULL,event,x,y);
    unsigned slot=12;
    for(unsigned i=0;i<12;++i) {
        BYTE *node=hud_data+0x13C+i*0xE4;
        int left=(int)Read32(node,0x14),top=(int)Read32(node,0x18);
        if(x>=left && x<left+24 && (int)(intptr_t)y>=top && (int)(intptr_t)y<top+24) {slot=i;break;}
    }
    CHECK(slot<12);
    unsigned held=Read32(grid_player,0x2C4);
    if(held==UINT32_MAX || !quick_reject_type) grid_swap(grid_player, NULL,50+(int)slot);
    /* 原函数不在交换后提前返回：A8非空还会读+50按钮模板与执行其它HUD业务。
     * 让旧实现确定失败，不把真实快捷格伪装成有合法按钮模板的CJm子按钮。 */
    void *button=ReadPtr(root,0xA8);
    if(button) {++quick_button_dispatches;CHECK(Memory_Readable(ReadPtr(button,0x50),12));}
    CHECK(button==NULL);return 1;
}
static void quickbar_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);profile.menu_hud_primary=(uintptr_t)quick_primary;
    quick_button_dispatches=0;quick_reject_type=false;
    for(unsigned i=0;i<12;++i) Write32(hud_data+0x13C+i*0xE4,0x64,0);
    activate_page(9);neutral_menu();Write32(grid_player,0xA4,101);
    menu_step(KEY(PAD_A),0,0);CHECK(Read32(grid_player,0x2C4)==101);
    neutral_menu();menu_step(KEY(PAD_X),0,0);neutral_menu();unsigned reason;
    CHECK(Menu_Context(&reason)==hud_data && ReadPtr(hud_data,0xA8)==NULL);
    emit_focus();CHECK(focus_draws>0 && focus_rectangle.left==11 && focus_rectangle.top==351 && focus_rectangle.right==33 && focus_rectangle.bottom==373);
    unsigned valid_draws=focus_draws;
    Write32(focus_sprites+5*32,8,0);emit_focus();CHECK(focus_draws==valid_draws);
    Write32(focus_sprites+5*32,8,1);Write32(focus_sprites+5*32,4,1);emit_focus();CHECK(focus_draws==valid_draws);
    Write32(focus_sprites+5*32,4,0);Write32(focus_image,0xC,0);emit_focus();CHECK(focus_draws==valid_draws);
    Write32(focus_image,0xC,26);ptr(focus_bank,4,NULL);emit_focus();CHECK(focus_draws==valid_draws);
    ptr(focus_bank,4,focus_entries);focus_entries[0]=NULL;emit_focus();CHECK(focus_draws==valid_draws);
    focus_entries[0]=focus_entry;emit_focus();CHECK(focus_draws>valid_draws);
    CHECK(!Menu_HidesCursor()); /* 持有时原图标保留，位置在动态框正中 */
    unsigned before_draw=focus_draws;g_intent.layer=LAYER_NATIVE;emit_focus();CHECK(focus_draws==before_draw);g_intent.layer=LAYER_MENU;
    menu_step(KEY(PAD_A),0,0);CHECK(Read32(grid_player,0x2C4)==UINT32_MAX && Read32(grid_player,0xA4+50*4)==101);
    CHECK(Menu_HidesCursor());
    /* 手柄不按物品类别跳区：十二格都使用同一个原点击入口。 */
    for (unsigned i=1;i<12;++i) {
        neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);Write32(grid_player,0x2C4,101+i);
        neutral_menu();menu_step(KEY(PAD_A),0,0);
        CHECK(Read32(grid_player,0xA4+(50+i)*4)==101+i && Read32(grid_player,0x2C4)==UINT32_MAX);
    }
    CHECK(quick_button_dispatches==0 && ReadPtr(hud_data,0xA8)==NULL);
    /* 向有物品的快捷格放置：原业务把旧物品变为持有物，不能进入主HUD按钮。 */
    Write32(grid_player,0x2C4,999);neutral_menu();menu_step(KEY(PAD_A),0,0);
    CHECK(Read32(grid_player,0xA4+61*4)==999 && Read32(grid_player,0x2C4)==112 && quick_button_dispatches==0);
    neutral_menu();menu_step(KEY(PAD_A),0,0);
    CHECK(Read32(grid_player,0xA4+61*4)==112 && Read32(grid_player,0x2C4)==999);
    /* 类型资格拒绝仍不丢弃、不交换，也不得触发后续主按钮业务。 */
    quick_reject_type=true;neutral_menu();unsigned swaps=grid_swaps;menu_step(KEY(PAD_A),0,0);
    CHECK(grid_swaps==swaps && Read32(grid_player,0x2C4)==999 && quick_button_dispatches==0);
    quick_reject_type=false;Write32(grid_player,0x2C4,UINT32_MAX);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(Read32(grid_player,0x2C4)==112);
    neutral_menu();menu_step(KEY(PAD_B),0,0);CHECK(Read32(grid_player,0x2C4)==UINT32_MAX && Read32(hud_data,0x64));
    neutral_menu();menu_step(KEY(PAD_B),0,0);CHECK(Read32(hud_data,0x64) && Read32(menu_roots[9],0x64));
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
static void discard_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);activate_page(9);neutral_menu();
    Write32(grid_player,0xA4,777);menu_step(KEY(PAD_BACK),0,0);
    CHECK(drop_requests==1 && Read32(grid_player,0xA4)==UINT32_MAX && Read32(menu_roots[9],0x64));
    menu_step(KEY(PAD_BACK),0,0);CHECK(drop_requests==1); /* 持键不连续丢 */
    neutral_menu();menu_step(KEY(PAD_BACK),0,0);CHECK(drop_requests==1); /* 空格无动作 */
    neutral_menu();Write32(grid_player,0x2C4,888);menu_step(KEY(PAD_BACK),0,0);
    CHECK(drop_requests==2 && Read32(grid_player,0x2C4)==UINT32_MAX);
    for(unsigned kind=9;kind<15;++kind) {
        activate_page(kind);neutral_menu();unsigned reason;
        void *before=Menu_Context(&reason);
        menu_step(KEY(PAD_LB),0,0);CHECK(Menu_Context(&reason)==before);
        neutral_menu();menu_step(KEY(PAD_RB),0,0);CHECK(Menu_Context(&reason)==before);
        neutral_menu();
        if(kind!=9) {menu_step(KEY(PAD_BACK),0,0);CHECK(drop_requests==2);}
    }
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
static bool other_focus_active,other_focus_invalid,other_focus_unregister;
static int other_focus(RuntimeFocusRequest *request,void *user)
{
    CHECK(user==&other_focus_active);
    if(other_focus_unregister) RuntimeFocus_Unregister(RUNTIME_MODULE_QOL);
    request->rectangle=other_focus_invalid ? (RuntimeFocusRect){0,0,INT32_MAX,INT32_MAX}:(RuntimeFocusRect){450,100,476,126};
    request->priority=200;return other_focus_active;
}
static void shared_focus_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);activate_page(9);neutral_menu();
    ready=false;CHECK(!RuntimeFocus_Initialize(NULL));
    GameProfile wrong={.game_id=GAME_ID_UNKNOWN};RuntimeContext invalid={.profile=&wrong};
    CHECK(!RuntimeFocus_Initialize(&invalid));
    wrong.game_id=expansion ? GAME_ID_WAIZHUAN:GAME_ID_DAOJIAN;
    CHECK(!RuntimeFocus_Initialize(&invalid)); /* 宿主不是游戏原签名：共享服务必须拒绝 */
    ready=true;
    for(unsigned kind=9;kind<=15;++kind) {
        if(kind<15)activate_page(kind);
        else {activate_page(9);neutral_menu();menu_step(KEY(PAD_X),0,0);}
        neutral_menu();RECT r;CHECK(Menu_FocusFrame(&r));focus_draws=0;emit_focus();
        CHECK(focus_draws>0 && RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER));
        CHECK(focus_rectangle.left==r.left && focus_rectangle.top==r.top && focus_rectangle.right==r.right && focus_rectangle.bottom==r.bottom);
        CHECK(Menu_HidesCursor()); /* 格子页空手均不用鼠标箭头 */
        Write32(grid_player,0x2C4,999);POINT point;CHECK(!Menu_HidesCursor() && Menu_CursorAnchor(&point));
        CHECK(point.x==(r.left+r.right)/2 && point.y==(r.top+r.bottom)/2);
        Write32(grid_player,0x2C4,UINT32_MAX);
    }
    activate_page(9);neutral_menu();unsigned reason;
    if(Menu_Context(&reason)==hud_data) {menu_step(KEY(PAD_X),0,0);neutral_menu();}
    CHECK(Menu_Context(&reason)==menu_roots[9]);
    menu_step(KEY(PAD_Y),0,0);neutral_menu();RECT panel;
    CHECK(!Menu_FocusFrame(&panel) && !Menu_HidesCursor());focus_draws=0;emit_focus();CHECK(focus_draws==0);
    ptr(menu_roots[4],0xCC,menu_roots[9]);((This2)menu_tables[4][0x1C/4])(menu_roots[4], NULL,1,0);
    neutral_menu();CHECK(!Menu_FocusFrame(&panel) && !Menu_HidesCursor());focus_draws=0;emit_focus();
    CHECK(!RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER) && focus_draws==0); /* 非格子确认项恢复原指针 */
    menu_step(KEY(PAD_B),0,0);neutral_menu();menu_step(KEY(PAD_Y),0,0);neutral_menu();
    CHECK(!RuntimeFocus_Register(RUNTIME_MODULE_NONE,other_focus,&other_focus_active));
    CHECK(!RuntimeFocus_Register(RUNTIME_MODULE_COUNT,other_focus,&other_focus_active));
    CHECK(!RuntimeFocus_Register((RuntimeModuleId)-1,other_focus,&other_focus_active));
    other_focus_active=true;other_focus_invalid=other_focus_unregister=false;
    CHECK(RuntimeFocus_Register(RUNTIME_MODULE_QOL,other_focus,&other_focus_active));
    CHECK(RuntimeFocus_Register(RUNTIME_MODULE_QOL,other_focus,&other_focus_active));
    CHECK(!RuntimeFocus_Register(RUNTIME_MODULE_QOL,other_focus,NULL));
    focus_draws=0;emit_focus();CHECK(RuntimeFocus_WasDrawn(RUNTIME_MODULE_QOL) && !RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER));
    CHECK(focus_rectangle.left==450 && focus_rectangle.right==476);
    other_focus_active=false;emit_focus();CHECK(RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER));
    other_focus_active=true;other_focus_invalid=true;emit_focus();CHECK(RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER));
    other_focus_invalid=false;other_focus_unregister=true;emit_focus();CHECK(RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER));
    CHECK(RuntimeFocus_Stage()==NULL);RuntimeFocus_Unregister(RUNTIME_MODULE_QOL);
    g_intent.layer=LAYER_NATIVE;emit_focus();CHECK(!RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER));
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
    CHECK(!RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER));
}
/* 以可交互格的真实24像素边界为验收，不以当前请求矩形作为自己的正确答案。 */
static void containment_regression(bool expansion,unsigned limit)
{
    Profile profile;menu_fixture(&profile,expansion);activate_page(9);neutral_menu();
    int16_t origin[4]={4,-1,1,-3};memcpy(focus_description+0xE,origin,8);
    for(unsigned kind=9;kind<=limit;++kind) {
        if(kind<15)activate_page(kind);
        else {activate_page(9);neutral_menu();menu_step(KEY(PAD_X),0,0);}
        neutral_menu();RECT r;CHECK(Menu_FocusFrame(&r));focus_draws=0;emit_focus();
        POINT center;Write32(grid_player,0x2C4,999);CHECK(Menu_CursorAnchor(&center));Write32(grid_player,0x2C4,UINT32_MAX);
        int w=kind<=11 || kind==15 ? 24:80,h=kind<=11 || kind==15 ? 24:30;
        CHECK(focus_rectangle.left>=center.x-w/2 && focus_rectangle.top>=center.y-h/2);
        CHECK(focus_rectangle.right<=center.x+w/2 && focus_rectangle.bottom<=center.y+h/2);
        CHECK(!memcmp(focus_description+0xE,origin,8)); /* 补偿不改原动画数据 */
        int16_t alternate[4]={-4,5,1,2};memcpy(focus_description+0xE,alternate,8);focus_draws=0;emit_focus();
        CHECK(focus_rectangle.left>=center.x-w/2 && focus_rectangle.top>=center.y-h/2);
        CHECK(focus_rectangle.right<=center.x+w/2 && focus_rectangle.bottom<=center.y+h/2);
        CHECK(!memcmp(focus_description+0xE,alternate,8));memcpy(focus_description+0xE,origin,8);

    }
    activate_page(9);neutral_menu();unsigned reason;
    if(Menu_Context(&reason)==hud_data) {menu_step(KEY(PAD_X),0,0);neutral_menu();}
    CHECK(Menu_Context(&reason)==menu_roots[9]);
    menu_step(KEY(PAD_Y),0,0);neutral_menu();RECT r;
    CHECK(!Menu_FocusFrame(&r) && !Menu_HidesCursor()); /* 非格子按钮区保留手型 */
    activate_page(8);neutral_menu();CHECK(Menu_FocusFrame(&r));focus_draws=0;emit_focus();CHECK(focus_draws>0);
    menu_step(KEY(PAD_RB),0,0);neutral_menu();menu_step(KEY(PAD_X),0,0);neutral_menu();
    CHECK(Menu_FocusFrame(&r));focus_draws=0;emit_focus();CHECK(focus_draws>0);

    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
/* 按实际截图提供左侧3动作与右侧4×3技能，左右列有小幅错位，不能靠节点顺序导航。 */
static void skill_visual_regression(bool expansion,const char *scenario)
{
    Profile profile;menu_fixture(&profile,expansion);
    for(unsigned i=0;i<15;++i) {
        BYTE *node=menu_children[8][i];Write32(node,0x28,i<3 ? 0x81+i:0x84+i-3);
        Write32(node,0x14,i<3 ? 20:100+(i-3)%4*60);
        Write32(node,0x18,i<3 ? 110+i*50:100+(i-3)/4*50);
        Write32(node,0x1C,44);Write32(node,0x20,48);Write32(node,0x64,1);
    }
    Write32(menu_children[8][15],0x64,0);
    if(!strcmp(scenario,"default")) {
        /* 学习页业务编号84故意放在右边，验收必须按视觉左上而非编号。 */
        Write32(menu_children[8][3],0x14,400);activate_page(8);neutral_menu();CHECK(focus_id(8)==0x85);
        Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);return;
    }
    activate_page(8);neutral_menu();menu_step(KEY(PAD_RB),0,0);neutral_menu();CHECK(focus_id(8)==0x84);
    if(!strcmp(scenario,"navigation")) {
        menu_step(KEY(PAD_DOWN),0,0);neutral_menu();menu_step(KEY(PAD_DOWN),0,0);neutral_menu();CHECK(focus_id(8)==0x8C);
        menu_step(KEY(PAD_DOWN),0,0);CHECK(focus_id(8)==0x8C); /* 不能溜到左侧跳跃 */
        neutral_menu();menu_step(KEY(PAD_LEFT),0,0);neutral_menu();CHECK(focus_id(8)==0x83);
        menu_step(KEY(PAD_UP),0,0);neutral_menu();menu_step(KEY(PAD_UP),0,0);neutral_menu();CHECK(focus_id(8)==0x81);
        menu_step(KEY(PAD_UP),0,0);CHECK(focus_id(8)==0x81); /* 左侧顶端不能跳右侧 */
    } else if(!strcmp(scenario,"frame")) {
        menu_step(KEY(PAD_LEFT),0,0);neutral_menu();CHECK(focus_id(8)==0x81);RECT r;
        for(unsigned id=0x81;id<=0x83;++id) {
            CHECK(focus_id(8)==id && Menu_FocusFrame(&r) && Menu_HidesCursor());
            focus_draws=0;emit_focus();CHECK(focus_draws>0);
            if(id<0x83) {menu_step(KEY(PAD_DOWN),0,0);neutral_menu();}
        }
    } else {
        menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();unsigned before=focus_id(8);CHECK(before==0x85);
        /* 四次新按键绕回第一套，按住同一次Y不得多切，技能位置始终不变。 */
        for(unsigned set=1;set<=4;++set) {
            menu_step(KEY(PAD_Y),0,0);CHECK(Read32(menu_roots[8],0xC4)==set%4 && focus_id(8)==before);
            menu_step(KEY(PAD_Y),0,0);CHECK(Read32(menu_roots[8],0xC4)==set%4 && focus_id(8)==before);
            neutral_menu();
        }
    }
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
/* 类型说明和必杀说明是原有效控件，不要求已学习或能主动加点。 */
static void skill_description_regression(bool expansion)
{
    const unsigned role_classes[]={4,30,40,0xDF,1};unsigned count=expansion ? 5:3;
    for(unsigned group=0;group<count;++group) {
        Profile p;menu_fixture(&p,expansion);p.game_id=expansion ? 2:1;Write32(grid_player,0x348,role_classes[group]);
        unsigned category=(expansion ? 0xE3:0xCF)+group*4;
        Write32(menu_roots[8],0xC0,0x7F);
        for(unsigned j=0;j<16;++j)Write32(menu_children[8][j],0x64,0);
        for(unsigned j=0;j<3;++j) {
            void *node=menu_children[8][j];Write32(node,0x64,1);Write32(node,0x28,j==0 ? category:j==1 ? 0x84:0x90);
            Write32(node,0x14,20+j*60);Write32(node,0x18,100);Write32(node,0x1C,44);Write32(node,0x20,48);
        }
        activate_page(8);neutral_menu();CHECK(focus_id(8)==0x84);
        menu_step(KEY(PAD_LEFT),0,0);neutral_menu();RECT r;
        CHECK(focus_id(8)==category && !Menu_FocusFrame(&r) && !Menu_HidesCursor());
        menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();
        CHECK(focus_id(8)==0x90 && Menu_FocusFrame(&r));
        Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
    }
}
static void empty_skill_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);
    Write32(menu_children[8][2],0x28,0x81);Write32(menu_roots[8],0xC0,0x80);
    unavailable_skill=true;activate_page(8);neutral_menu();CHECK(focus_id(8)==0x81);
    unavailable_skill=false;Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
static void settings_regression(bool expansion)
{
    Profile profile;menu_fixture(&profile,expansion);
    for(unsigned k=0;k<15;++k)Write32(menu_roots[k],0x64,0);
    ptr(ui_data,0x3C,settings_root);((This2)menu_tables[16][0x1C/4])(settings_root, NULL,1,0);neutral_menu();
    CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xAB && Menu_CapturesInput());game_isolated();
    POINT anchor;RECT rect;CHECK(Menu_CursorAnchor(&anchor) && !Menu_FocusFrame(&rect) && !Menu_HidesCursor());
    menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(settings_children[0],0xDC)==51 && settings_applies==1);
    menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(settings_children[0],0xDC)==51);
    g_input.now+=350;menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(settings_children[0],0xDC)==52);
    g_input.now+=2000;menu_step(KEY(PAD_RIGHT),0,0);
    unsigned accelerated=Read32(settings_children[0],0xDC);
    g_input.now+=40;menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(settings_children[0],0xDC)>accelerated);
    unsigned value_after=Read32(settings_children[0],0xDC);neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(Read32(settings_children[0],0xDC)==value_after && !settings_choices);
    neutral_menu();Write32(settings_children[0],0xDC,100);menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(settings_children[0],0xDC)==100);
    neutral_menu();Write32(settings_children[0],0xDC,0);menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(settings_children[0],0xDC)==0);
    for(unsigned i=0;i<3;++i){neutral_menu();menu_step(KEY(PAD_DOWN),0,0);}
    CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xAE);neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xAF && Read32(settings_root,0xC4)==0xAF);
    neutral_menu();menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xAE && Read32(settings_root,0xC4)==0xAE);
    neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xB0);
    neutral_menu();menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xB1 && Read32(settings_root,0xC8)==0xB1);
    unsigned before=settings_choices;neutral_menu();menu_step(KEY(PAD_LB)|KEY(PAD_RB)|KEY(PAD_X)|KEY(PAD_Y),0,0);
    CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xB1 && settings_choices==before);
    neutral_menu();menu_step(KEY(PAD_B)|KEY(PAD_A)|KEY(PAD_RIGHT),0,0);CHECK(settings_closes==1 && settings_choices==before && !Read32(settings_root,0x64));
    ptr(ui_data,0x3C,NULL);neutral_menu();CHECK(!Menu_Context(&before));
    /* 无capture的标题入口也必须从登记链找到设置，不能只在游戏内可用。 */
    void *head=ReadPtr(ui_data,0x18),*tail=ReadPtr(ui_data,0x1C);
    ptr(ui_data,0x18,settings_root);ptr(ui_data,0x1C,settings_root);
    ((This2)menu_tables[16][0x1C/4])(settings_root, NULL,1,0);neutral_menu();CHECK(Menu_Context(&before)==settings_root);
    for(unsigned i=1;i<3;++i) {
        neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xAB+i);
        unsigned value=Read32(settings_children[i],0xDC);neutral_menu();menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(settings_children[i],0xDC)==value-1);
    }
    /* A返回仍调用原close；隐藏控件不进入导航，按住方向具有延迟后连发。 */
    Write32(settings_children[3],0x64,0);Write32(settings_children[4],0x64,0);
    Write32(settings_children[5],0x64,0);Write32(settings_children[6],0x64,0);
    neutral_menu();menu_step(KEY(PAD_DOWN),0,0);CHECK(Read32(ReadPtr(settings_root,0xA8),0x28)==0xB2);
    neutral_menu();menu_step(KEY(PAD_A),0,0);CHECK(settings_closes==2 && !Read32(settings_root,0x64));
    ptr(ui_data,0x18,head);ptr(ui_data,0x1C,tail);
    entry_enabled=1;ptr(ui_data,0x3C,settings_root);((This2)menu_tables[16][0x1C/4])(settings_root, NULL,1,0);neutral_menu();
    for(unsigned i=0;i<4;++i){menu_step(KEY(PAD_DOWN),0,0);neutral_menu();}
    CHECK(ReadPtr(settings_root,0xA8)==NULL && Menu_CursorAnchor(&anchor));
    menu_step(KEY(PAD_A),0,0);CHECK(entry_opened==1);
    /* 下方原选项向右只聚焦入口，A才打开；向左回原选项不自动切换配置。 */
    neutral_menu();menu_step(KEY(PAD_LEFT),0,0);neutral_menu();unsigned unchanged=settings_choices;
    menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();CHECK(ReadPtr(settings_root,0xA8)==NULL && settings_choices==unchanged);
    entry_enabled=0;ptr(ui_data,0x3C,NULL);
    Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
static void newgame_regression(bool expansion)
{
    Profile p;menu_fixture(&p,expansion);for(unsigned i=0;i<15;++i)Write32(menu_roots[i],0x64,0);
    Write32(new_roots[0],0x64,1);ptr(ui_data,0x3C,new_roots[0]);neutral_menu();
    CHECK(Read32(ReadPtr(new_roots[0],0xA8),0x28)==0x24);POINT pt;RECT r;CHECK(Menu_CursorAnchor(&pt) && !Menu_FocusFrame(&r));
    menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();CHECK(Read32(ReadPtr(new_roots[0],0xA8),0x28)==0x25);
    menu_step(KEY(PAD_A),0,0);CHECK(character_requests==1 && Read32(new_roots[0],0xC0)==5);neutral_menu();
    CHECK(Read32(ReadPtr(new_roots[1],0xA8),0x28)==0x9F && !Menu_FocusFrame(&r));
    CHECK(Menu_CursorAnchor(&pt) && pt.x==284 && pt.y==174);
    menu_step(KEY(PAD_Y)|KEY(PAD_A),0,0);CHECK(random_name_requests==1 && !newgame_requests);neutral_menu();
    menu_step(KEY(PAD_RIGHT),0,0);CHECK(Read32(new_children[1][0],0xC4)==2);neutral_menu();
    menu_step(KEY(PAD_LEFT),0,0);CHECK(Read32(new_children[1][0],0xC4)==1);neutral_menu();
    menu_step(KEY(PAD_DOWN),0,0);neutral_menu();CHECK(Read32(ReadPtr(new_roots[1],0xA8),0x28)==0xA0 && Menu_FocusFrame(&r));
    Write32(hud_data,0x64,0);focus_draws=0;emit_focus();CHECK(focus_draws>0);Write32(hud_data,0x64,1);
    menu_step(KEY(PAD_A),0,0);CHECK(newgame_requests==1 && Read32(new_roots[1],0x64));neutral_menu();
    menu_step(KEY(PAD_RIGHT),0,0);neutral_menu();CHECK(Read32(ReadPtr(new_roots[1],0xA8),0x28)==0xA1 && Menu_FocusFrame(&r));
    menu_step(KEY(PAD_B),0,0);neutral_menu();CHECK(Read32(new_roots[0],0x64) && !Read32(new_roots[1],0x64));
    menu_step(KEY(PAD_B),0,0);CHECK(Read32(menu_roots[0],0x64) && !Read32(new_roots[0],0x64));
    ptr(ui_data,0x3C,NULL);Cursor_Shutdown();Menu_Shutdown();HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
}
int main(int argc,char **argv)
{
    if(argc>2 && !strcmp(argv[1],"--skill-focus")) {
        skill_visual_regression(false,argv[2]);skill_visual_regression(true,argv[2]);
        printf("两作技能视觉导航/动作框/Y焦点场景通过：%s\n",argv[2]);return 0;
    }
    if(argc>1 && !strcmp(argv[1],"--containment")) {unsigned limit=argc>2 && !strcmp(argv[2],"bag") ? 9:15;containment_regression(false,limit);containment_regression(true,limit);printf("两作最终绘制边界格内、动画偏移与非格子指针回放通过\n");return 0;}
    newgame_regression(false);newgame_regression(true);
    skill_description_regression(false);skill_description_regression(true);
    empty_skill_regression(false);empty_skill_regression(true);
    settings_regression(false);settings_regression(true);
    shared_focus_regression(false);shared_focus_regression(true);
    discard_regression(false);discard_regression(true);
    action_regression(false);action_regression(true);
    quickbar_regression(false);quickbar_regression(true);
    menu_regression(false);menu_regression(true);
    combo_delete_regression(false);combo_delete_regression(true);
    grid_regression(false);grid_regression(true);
    all_regions_regression(false);all_regions_regression(true);
    modal_region_regression(false);modal_region_regression(true);
    printf("两作菜单原生虚表、焦点、动画与输入隔离回放通过：%u项\n",checks);
    return 0;
}
