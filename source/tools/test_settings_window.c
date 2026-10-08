#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/Runtime/SettingsModel.h"
#include "../src/Runtime/Win32Bridge.h"
static int fixture_foreground=1;
static int fixture_key;
static SHORT __stdcall fixture_async(int key){return key==fixture_key ? (SHORT)0x8000:0;}
static HWND __stdcall fake_foreground(void){return (HWND)1;}
static DWORD __stdcall fake_pid(HWND w,LPDWORD pid){(void)w;*pid=GetCurrentProcessId()+(fixture_foreground ? 0:1);return 1;}
#define GetForegroundWindow fake_foreground
#define GetWindowThreadProcessId fake_pid
#define GetAsyncKeyState fixture_async
#include "../src/Runtime/SettingsWindow.c"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetAsyncKeyState
static unsigned checks,pauses,resumes,original_actions;
static int fixture_write_result=1;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"设置窗口失败 行%d：%s\n",__LINE__,#x);return 1;}}while(0)
static BYTE fake_world[0x100],fake_actor[0x500],fake_player[0x400],fake_root[0x300],fake_ui[0x80],fake_hud[0xD00];
static void *world_ptr=fake_world,*hud_ptr=fake_hud;
static uintptr_t table[32];
int RuntimeWin32_Read(unsigned long address,void *out,unsigned long size)
{
    MEMORY_BASIC_INFORMATION i;
    if(!address || !VirtualQuery((void *)address,&i,sizeof i) || i.State!=MEM_COMMIT ||
        (i.Protect&(PAGE_NOACCESS|PAGE_GUARD)) || address+size>(uintptr_t)i.BaseAddress+i.RegionSize)return 0;
    memcpy(out,(void *)address,size);return 1;
}
int RuntimeWin32_IsReadable(unsigned long address,unsigned long size){BYTE scratch[0x600];return size<=sizeof scratch && RuntimeWin32_Read(address,scratch,size);}
int RuntimeWin32_WriteCode(unsigned long address,const void *bytes,unsigned long size)
{int result=fixture_write_result;fixture_write_result=1;if(result)memcpy((void *)address,bytes,size);return result;}
int Runtime_Subscribe(RuntimeEventId e,RuntimeEventCallback cb,void *user){(void)e;(void)cb;(void)user;return 1;}
void RuntimeLog_Write(const char *format,...){(void)format;}
static void wr(void *p,unsigned offset,uint32_t v){memcpy((BYTE *)p+offset,&v,4);}
static int __attribute__((thiscall)) get_actor(void *manager){(void)manager;return (int)(uintptr_t)fake_actor;}
static int __attribute__((thiscall)) get_player(void *manager){(void)manager;return (int)(uintptr_t)fake_player;}
static int __attribute__((thiscall)) get_root(void *manager,int id){(void)manager;return id==0x2D ? (int)(uintptr_t)fake_root:0;}
static int __attribute__((thiscall)) show_root(void *p,int show,int mode)
{
    (void)mode;wr(p,0x64,(unsigned)show);wr(fake_ui,0x3C,show ? (uint32_t)(uintptr_t)p:0);
    if(show)++pauses;else ++resumes;return 1;
}
static int __attribute__((thiscall)) native_action(void *p,int e,int x,void *y){(void)p;(void)e;(void)x;(void)y;++original_actions;return 1;}
static BYTE fixture_record[0x40],fixture_choices[16];
static int __attribute__((thiscall)) no_property(void *p,int i)
{return p==fixture_record && i==2 ? 701:p==fixture_choices ? (i==1 ? 1:i==2 ? 701:0):0;}
static BYTE fixture_group[0x50],fixture_method[0x50];
static char game_name[64],game_description[256];static int name_available=1,descriptions,destroyed;
static void *empty_string=game_description;
static int __attribute__((thiscall)) fixture_name(void *p){(void)p;return name_available ? (int)(uintptr_t)game_name:0;}
static void __attribute__((thiscall)) fixture_query(void *p,void **learned,void **next,int slot)
{(void)p;*learned=slot==0 ? fixture_record:NULL;*next=NULL;}
static void __cdecl fixture_description(void *player,void *record,unsigned *string,int detail)
{if(player==fake_player && record==fixture_record && detail==1)++descriptions;*string=(unsigned)(uintptr_t)game_description;}
static int __attribute__((thiscall)) fixture_destroy(void *p){(void)p;++destroyed;return 1;}
static int __attribute__((thiscall)) fixture_lookup(void *p,int id)
{(void)p;return (int)(uintptr_t)(id==701 ? fixture_group:id==10023 ? fixture_method:NULL);}
static int __attribute__((thiscall)) fixture_eligibility(void *p,int id){(void)p;(void)id;return 1;}
static HDC fixture_dc;static unsigned releases;static int dc_failed;
static BYTE fixture_icons[0x60],fixture_sprites[128];static void *icons_pointer=fixture_icons;
static unsigned icon_calls;static int icon_abi_ok;
static int __attribute__((thiscall)) fixture_icon_draw(void *self,int context,int icon,int selector,int x,int y,int mode,int shade,int bindings,int side)
{
    (void)context;++icon_calls;
    icon_abi_ok=self==fixture_icons && icon==2 && selector==123 && x==42 && y==138 && mode==2 && shade==-1 && !bindings && !side;
    /* 图标只是本进程蓝色方块夹具，验证原绘制参数/位置；不是原游戏素材。 */
    box(fixture_dc,x,y,43,47,RGB(38,66,92),RGB(145,174,196));return 1;
}
static HRESULT __stdcall fixture_get_dc(void *self,HDC *out)
{(void)self;if(dc_failed)return E_FAIL;*out=fixture_dc;return S_OK;}
static HRESULT __stdcall fixture_release_dc(void *self,HDC dc)
{(void)self;(void)dc;++releases;return S_OK;}
static const char *snapshot_path="settings_window_fixture.bmp";
static int snapshot(const BITMAPINFOHEADER *header,const void *pixels)
{
    /* 仅保存本进程离线回放位图，方便检查文字与布局；不是游戏截图。 */
    FILE *file=fopen(snapshot_path,"wb");if(!file)return 0;
    BITMAPFILEHEADER head={0};head.bfType=0x4D42;head.bfOffBits=sizeof head+sizeof *header;
    unsigned size=(unsigned)header->biWidth*(unsigned)-header->biHeight*4;head.bfSize=head.bfOffBits+size;
    int result=fwrite(&head,sizeof head,1,file)==1 && fwrite(header,sizeof *header,1,file)==1 && fwrite(pixels,size,1,file)==1;
    return fclose(file)==0 && result;
}
int wmain(void)
{
    wchar_t path[1024];swprintf(path,1024,L"窗口配置回归_%lu.toml",GetCurrentProcessId());
    CHECK(RuntimeConfig_OpenPath(path));ready=1;game=2;
    backend=(SettingsBackend){.world_global=(uintptr_t)&world_ptr,.ui=(uintptr_t)fake_ui,.skill_global=(uintptr_t)&hud_ptr,
        .inventory_root=(uintptr_t)fake_player,.inventory_get=(uintptr_t)get_player,.get_jm=(uintptr_t)get_root,
        .menu_system_vtable=(uintptr_t)table,.menu_system_show=(uintptr_t)show_root,.menu_system_primary=(uintptr_t)native_action,
        .settings_actor_get=(uintptr_t)get_actor,.ui_property=(uintptr_t)no_property,.active_offset=0x359,.invalid_offset=0x446,
        .settings_skill_name=(uintptr_t)fixture_name,.settings_skill_description=(uintptr_t)fixture_description,
        .settings_string_destroy=(uintptr_t)fixture_destroy,.settings_query_skill=(uintptr_t)fixture_query,
        .settings_empty_string=(uintptr_t)&empty_string};
    WideCharToMultiByte(936,0,L"踢击",-1,game_name,sizeof game_name,NULL,NULL);
    WideCharToMultiByte(936,0,L"原版技能说明",-1,game_description,sizeof game_description,NULL,NULL);
    table[0x24/4]=(uintptr_t)native_action;wr(fake_root,0,(uint32_t)(uintptr_t)table);
    wr(fake_world,0x30,(uint32_t)(uintptr_t)fake_ui);wr(fake_world,0x58,1);wr(fake_player,0x348,50);
    /* 战斗中并推杆仍可打开，由原Show暂停；不再以待机拒绝。 */
    wr(fake_actor,0x359,123);wr(fake_actor,0x73,17);
    CHECK(SettingsWindow_Pad(1u<<4,1u<<4,1,1,1,0,0,0,10));
    CHECK(active && pauses==1 && model.role==50);
    CHECK(capture_primary(fake_root,NULL,0,0,NULL)==1 && original_actions==0);
    SettingsWindow_Pad(0,0,0,0,0,0,0,0,20);CHECK(!barrier);
    SettingsWindow_Pad(1u<<3,1u<<3,0,0,0,0,0,0,30);CHECK(model.help);
    SettingsWindow_Pad(1u<<10,1u<<10,0,0,0,0,0,0,40);CHECK(model.page==1);
    CHECK(SettingsModel_SetInt(&model,CONFIG_AIM_EXPAND_MS,550));cancel();CHECK(confirm_discard && active);
    cancel();CHECK(!confirm_discard && active);cancel();activate();CHECK(!active && resumes==1);
    CHECK(table[0x24/4]==(uintptr_t)native_action);
    /* 读图／已有捕获不叠加，单RT+Back不进入；双扳机+START救援不进入。 */
    wr(fake_world,0x58,0);CHECK(!SettingsWindow_Pad(1u<<4,1u<<4,1,1,0,0,0,0,50));wr(fake_world,0x58,1);
    wr(fake_ui,0x3C,1);CHECK(!open_window());wr(fake_ui,0x3C,0);
    CHECK(!SettingsWindow_Pad(1u<<4,1u<<4,0,1,0,0,0,0,60));
    CHECK(!SettingsWindow_Pad((1u<<4)|(1u<<6),1u<<4,1,1,0,0,0,0,70));
    CHECK(open_window());SettingsWindow_Close();CHECK(pauses==2 && resumes==2);
    fixture_write_result=2;CHECK(!open_window() && table[0x24/4]==(uintptr_t)native_action);
    CHECK(open_window());fixture_write_result=0;SettingsWindow_Close();CHECK(active && old_primary);
    SettingsWindow_Close();CHECK(!active && table[0x24/4]==(uintptr_t)native_action);
    /* 实际GDI绘制到本进程位图：检查最小画布、宽画布、DC故障与原状态恢复。 */
    BYTE surface[0x40]={0};uintptr_t dd_table[32]={0},dd=(uintptr_t)dd_table;
    wr(surface,0x2D,(uint32_t)(uintptr_t)&dd);
    dd_table[0x44/4]=(uintptr_t)fixture_get_dc;dd_table[0x68/4]=(uintptr_t)fixture_release_dc;
    fixture_dc=CreateCompatibleDC(NULL);CHECK(fixture_dc!=NULL);
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=960;
    info.bmiHeader.biHeight=-600;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void *pixels=NULL;HBITMAP bitmap=CreateDIBSection(fixture_dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);
    CHECK(bitmap && pixels);HGDIOBJ previous_bitmap=SelectObject(fixture_dc,bitmap);
    CHECK(open_window());wr(surface,0xC,640);wr(surface,0x10,480);
    PatBlt(fixture_dc,0,0,960,600,WHITENESS);SetBkMode(fixture_dc,OPAQUE);
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    CHECK(releases==1 && origin_x==16 && origin_y==16);
    CHECK(GetPixel(fixture_dc,15,16)==RGB(255,255,255));
    CHECK(GetPixel(fixture_dc,16,16)==RGB(164,124,59) && GetBkMode(fixture_dc)==OPAQUE);
    CHECK(GetPixel(fixture_dc,624,16)==RGB(255,255,255));
    CHECK(snapshot(&info.bmiHeader,pixels));
    wr(surface,0xC,960);wr(surface,0x10,600);
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    CHECK(releases==2 && origin_x==176 && origin_y==76);
    dc_failed=1;paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);CHECK(releases==2);
    dc_failed=0;wr(surface,0xC,320);paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);CHECK(releases==3);
    /* 数值编辑取消只撤销当前字段；技能候选取消不改绑定，选中才写草稿。 */
    model.page=1;model.focus[1]=0;activate();CHECK(editing);move(4);cancel();CHECK(!editing && !SettingsModel_Dirty(&model));
    model.page=2;skill_count=1;skills[0].selector=123;strcpy(skills[0].name,"测试技能");activate();
    move(2);cancel();CHECK(!model.draft_bindings[0].custom);
    activate();activate();CHECK(model.draft_bindings[0].custom && model.draft_bindings[0].selector==123);
    SettingsModel_Discard(&model);SettingsWindow_Close();
    CHECK(RuntimeConfig_SetInt(CONFIG_MENU_SWAP_AB,1));RuntimeConfig_ApplyFrame(1);
    CHECK(open_window());SettingsWindow_Pad(0,0,0,0,0,0,0,0,80);
    fixture_foreground=0;SettingsWindow_Pad(1u<<0,1u<<0,0,0,0,0,0,0,85);CHECK(active && barrier);
    fixture_foreground=1;SettingsWindow_Pad(0,0,0,0,0,0,0,0,86);
    SettingsWindow_Pad(1u<<0,1u<<0,0,0,0,0,0,0,90);CHECK(!active); /* 交换后A取消 */
    /* 查表组号701实际动作123：回归不能把组编号保存成施放selector。 */
    backend.lookup=(uintptr_t)fixture_lookup;backend.skill_eligibility=(uintptr_t)fixture_eligibility;
    skill_count=0;wr(fixture_group,0x22,2);wr(fixture_group,0x24,123);add_skill(fake_actor,701,0);
    CHECK(skill_count==1 && skills[0].selector==123);add_skill(fake_actor,701,0);CHECK(skill_count==1);
    CHECK(!strcmp(skills[0].name,"踢击") && skills[0].icon==2);
    wr(fixture_group,0x32,2);skill_count=0;add_skill(fake_actor,701,0);CHECK(skill_count==0);
    add_skill(fake_actor,10023,1);CHECK(skill_count==0);
    wr(fixture_group,0x32,0);name_available=0;add_skill(fake_actor,701,0);CHECK(skill_count==0);
    name_available=1;learned_records[0]=fixture_record;skill_player=fake_player;add_skill(fake_actor,701,0);
    CHECK(skill_count==1 && !strcmp(skills[0].description,"原版技能说明") && descriptions==1 && destroyed==1);
    skills[1]=skills[0];skills[1].selector=124;skills[1].kind=0;skill_count=2;picker_style=0;picker_rebuild(123);CHECK(skill_view_count==0);
    skills[0].kind=1;picker_rebuild(123);CHECK(skill_view_count==1);picker_switch_style();CHECK(skill_view_count==2);
    skill_count=1;
    /* 大写键位只改显示，不改变保存枚举；三个页面都有真实滚动位置。 */
    char key_text[64];value_text(CONFIG_WORLD_INTERACT,key_text,sizeof key_text);CHECK(!strcmp(key_text,"A"));
    value_text(CONFIG_WORLD_SYSTEM,key_text,sizeof key_text);CHECK(!strcmp(key_text,"START"));
    picker=editing=0;model.page=0;model.scroll[0]=0;model.focus[0]=7;RECT h=help_rectangle();CHECK(h.left==16 && h.top==82);
    model.focus[0]=0;h=help_rectangle();CHECK(h.left==316 && h.top==194);
    RECT thumb=scroll_thumb();CHECK(thumb.bottom<370 && thumb.top==82);scroll_at(370);CHECK(model.scroll[0]>0 && model.focus[0]/2>=model.scroll[0]);
    model.page=1;model.scroll[1]=0;thumb=scroll_thumb();CHECK(thumb.top==82 && thumb.bottom==370);
    model.page=2;model.scroll[2]=0;thumb=scroll_thumb();CHECK(thumb.bottom<370);scroll_at(370);CHECK(model.scroll[2]==1);
    CHECK(open_window());SettingsModel_SetInt(&model,CONFIG_WORLD_LEFT,0);footer=1;activate();CHECK(error_modal && active && SettingsModel_Dirty(&model));
    unsigned before_focus=model.focus[model.page];move(2);CHECK(model.focus[model.page]==before_focus);
    mouse_click(10,10);CHECK(error_modal);cancel();CHECK(!error_modal && SettingsModel_Dirty(&model));
    SettingsModel_Discard(&model);footer=0;model.page=1;model.focus[1]=0;activate();CHECK(editing);
    int old=model.draft.values[CONFIG_DEADZONE];mouse_click(180,210);CHECK(model.draft.values[CONFIG_DEADZONE]>old);
    mouse_click(180,320);CHECK(!editing && model.draft.values[CONFIG_DEADZONE]==old);
    SettingsWindow_Close();
    /* 键盘入口只使用主键盘1左边的反引号；同一边沿不连续开关。 */
    fixture_key=VK_F12;keyboard(RUNTIME_EVENT_INPUT_FRAME_END,fake_root,0,0,NULL);CHECK(!active);
    fixture_key=VK_OEM_3;keyboard(RUNTIME_EVENT_INPUT_FRAME_END,fake_root,0,0,NULL);CHECK(active);
    keyboard(RUNTIME_EVENT_INPUT_FRAME_END,fake_root,0,0,NULL);CHECK(active);
    fixture_key=0;keyboard(RUNTIME_EVENT_INPUT_FRAME_END,fake_root,0,0,NULL);
    fixture_key=VK_OEM_3;keyboard(RUNTIME_EVENT_INPUT_FRAME_END,fake_root,0,0,NULL);CHECK(!active);
    fixture_key=0;keyboard(RUNTIME_EVENT_INPUT_FRAME_END,fake_root,0,0,NULL);
    /* 用实际候选生成链取得已学组、原名称和原说明，不直接把假名字填进列表冒充数据接通。 */
    wr(fake_actor,0x193,(uint32_t)(uintptr_t)fixture_choices);CHECK(open_window());
    CHECK(skill_count==1 && !strcmp(skills[0].name,"踢击") && !strcmp(skills[0].description,"原版技能说明"));
    wr(surface,0xC,640);wr(surface,0x10,480);
    model.page=0;model.focus[0]=7;model.help=1;
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    snapshot_path="settings_help_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));
    model.page=1;model.focus[1]=0;model.help=0;activate();
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    snapshot_path="settings_edit_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));cancel();
    model.page=2;activate();model.help=1;
    backend.settings_icon_global=(uintptr_t)&icons_pointer;backend.icon_draw=(uintptr_t)fixture_icon_draw;
    wr(fixture_icons,0x44,4);wr(fixture_icons,0x48,(uint32_t)(uintptr_t)fixture_sprites);
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    CHECK(icon_calls==1 && icon_abi_ok);
    snapshot_path="settings_skill_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));
    CHECK(picker && !picker_footer);cancel();SettingsWindow_Close();
    SelectObject(fixture_dc,previous_bitmap);DeleteObject(bitmap);DeleteDC(fixture_dc);
    if(font){DeleteObject(font);font=NULL;}
    if(help_font){DeleteObject(help_font);help_font=NULL;}
    for(unsigned i=0;i<brush_count;++i)DeleteObject(brushes[i].brush);
    CHECK(DeleteFileW(path));printf("原生设置暂停/捕获/关闭链与模型入口回放通过：%u项\n",checks);return 0;
}
