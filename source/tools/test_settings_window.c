#include "../src/Runtime/FileIO.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/Runtime/SettingsModel.h"
#include "../src/Runtime/Win32Bridge.h"
static int fixture_foreground=1;
static int fixture_key;
static SHORT __stdcall fixture_async(int key){return key==fixture_key ? (SHORT)-32768:0;}
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
static BYTE native_settings_root[0x300];static uintptr_t native_settings_table[26];static unsigned native_captions;static unsigned expected_caption_color;
static BYTE fake_world[0x100],fake_actor[0x500],fake_player[0x400],fake_root[0x300],fake_ui[0x80],fake_hud[0xD00],default_record[0x20];
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
static int __fastcall get_actor(void *manager, void *unused_edx){ (void)unused_edx;(void)manager;return (int)(uintptr_t)fake_actor;}
static int __fastcall get_player(void *manager, void *unused_edx){ (void)unused_edx;(void)manager;return (int)(uintptr_t)fake_player;}
static int __fastcall get_root(void *manager, void *unused_edx,int id){ (void)unused_edx;(void)manager;return id==0xAA ? (int)(uintptr_t)native_settings_root:id==0x2D ? (int)(uintptr_t)fake_root:0;}
static BYTE fixture_map_root[0x110];static uintptr_t fixture_map_table[8];static void *fixture_map_pointer=fixture_map_root;static int map_host_side_effect;static unsigned fixture_map_calls;
static int __fastcall fixture_map_show(void *self,void *edx,int shown,int mode)
{(void)edx;CHECK(self==fixture_map_root && !mode);wr(self,0x64,shown);++fixture_map_calls;return 1;}
static int __fastcall show_root(void *p, void *unused_edx,int show,int mode)
{if(map_host_side_effect)wr(fixture_map_root,0x64,show!=0); (void)unused_edx;
    (void)mode;wr(p,0x64,(unsigned)show);wr(fake_ui,0x3C,show ? (uint32_t)(uintptr_t)p:0);
    if(show)++pauses;else ++resumes;return 1;
}
static int __fastcall native_action(void *p, void *unused_edx,int e,int x,void *y){ (void)unused_edx;(void)p;(void)e;(void)x;(void)y;++original_actions;return 1;}
static int __fastcall native_caption(void *page, void *unused_edx,unsigned long context,const char *text,int x,int y,int mode)
{ (void)unused_edx;CHECK(page!=native_settings_root && context && !strncmp(text,"EDSlash",7) && x==488 && y==318 && !mode);CHECK(rd(page,0x60)==expected_caption_color && rd(page,0x78)==0);++native_captions;return 1;}
static BYTE fixture_record[0x40],fixture_choices[16];
static int __fastcall no_property(void *p, void *unused_edx,int i)
{ (void)unused_edx;return p==fixture_record ? (i==2 ? 701:i==15 ? 11:0):p==fixture_choices ? (i==1 ? 1:i==2 ? 701:0):0;}
static BYTE fixture_group[0x50],fixture_method[0x50];
static char game_name[64],game_description[256],game_narrative[256];static int name_available=1,descriptions,destroyed;
static void *empty_string=game_description;
static int __fastcall fixture_name(void *p, void *unused_edx){ (void)unused_edx;(void)p;return name_available ? (int)(uintptr_t)game_name:0;}
static void __fastcall fixture_query(void *p, void *unused_edx,void **learned,void **next,int slot)
{ (void)unused_edx;(void)p;*learned=slot==0 ? fixture_record:NULL;*next=NULL;}
static void __cdecl fixture_description(void *player,void *record,unsigned *string,int detail)
{if(player==fake_player && record==fixture_record && detail==0)++descriptions;*string=(unsigned)(uintptr_t)game_description;}
static int __fastcall fixture_text(void *self, void *unused_edx,int id,int column)
{ (void)unused_edx;(void)self;return id==11 && column==1 ? (int)(uintptr_t)game_narrative:0;}
static int __fastcall fixture_destroy(void *p, void *unused_edx){ (void)unused_edx;(void)p;++destroyed;return 1;}
static int __fastcall fixture_lookup(void *p, void *unused_edx,int id)
{ (void)unused_edx;(void)p;return (int)(uintptr_t)(id==701 ? fixture_group:id==10023 ? fixture_method:NULL);}
static int __fastcall fixture_eligibility(void *p, void *unused_edx,int id){ (void)unused_edx;(void)p;(void)id;return 1;}
static HDC fixture_dc;static unsigned releases;static int dc_failed;
static BYTE fixture_icons[0x60],fixture_sprites[128];static void *icons_pointer=fixture_icons;
static unsigned icon_calls;static int icon_abi_ok;
static BYTE icon_frame[22],icon_bank[8],icon_image[0x40];static void *icon_entry=icon_image;
static int __fastcall fixture_frame(void *p, void *unused_edx){ (void)unused_edx;(void)p;return (int)(uintptr_t)icon_frame;}
static int __fastcall fixture_image(void *p, void *unused_edx){ (void)unused_edx;(void)p;return (int)(uintptr_t)icon_image;}
static int __fastcall fixture_icon_draw(void *self, void *unused_edx,int context,int icon,int selector,int x,int y,int mode,int shade,int bindings,int side)
{ (void)unused_edx;
    (void)context;++icon_calls;
    icon_abi_ok=self==fixture_icons && icon==2 && selector==123 && x==40 && y==106 && mode==0 && shade==-1 && !bindings && !side;
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
/* 鼠标滚轮应滚动列表，而不是从右列缺项跳进底部按钮；两种开窗入口共用回放。 */
static int wheel_last_row_regression(void)
{
    SettingsModel before=model;int old_footer=footer;
    model.page=1;model.scroll[1]=0;model.focus[1]=11;footer=editing=picker=0;
    CHECK(SettingsModel_Field(1,11)==CONFIG_WORLD_MAP && SettingsModel_Field(1,12)==CONFIG_WORLD_SYSTEM);
    CHECK(SettingsWindow_Wheel(-60) && !model.scroll[1] && !footer);
    CHECK(SettingsWindow_Wheel(-60) && model.scroll[1]==1 && !footer);
    mouse_click(24,342);CHECK(model.focus[1]==12 && editing && edit_id==CONFIG_WORLD_SYSTEM);
    cancel();CHECK(SettingsWindow_Wheel(120) && !model.scroll[1]);
    CHECK(SettingsWindow_Wheel(-1200) && model.scroll[1]==1);
    CHECK(SettingsWindow_Wheel(1200) && !model.scroll[1]);
    model=before;footer=old_footer;return 0;
}
int wmain(void)
{
    /* 每次回放保留系统分配的独立文件名，写入默认配置后再读取。
     * 即使检查失败留下文件、Windows复用进程编号，也不会读到其它回放的草稿。 */
    wchar_t path[1100],directory[1024],skill_file[1100];CHECK(GetTempFileNameW(L".",L"ESW",0,directory));
    CHECK(DeleteFileW(directory) && CreateDirectoryW(directory,NULL));
    swprintf(path,1100,L"%ls\\EDSlash.toml",directory);swprintf(skill_file,1100,L"%ls\\EDSlash.SkillContols.toml",directory);
    static char defaults[65536];size_t defaults_size;
    CHECK(RuntimeConfig_DefaultText(defaults,sizeof defaults,&defaults_size));
    CHECK(RuntimeFile_WriteAtomic(path,defaults,defaults_size,1));
    CHECK(RuntimeConfig_OpenPathForGame(path,2));ready=1;game=2;
    backend=(SettingsBackend){.world_global=(uintptr_t)&world_ptr,.ui=(uintptr_t)fake_ui,.skill_global=(uintptr_t)&hud_ptr,
        .inventory_root=(uintptr_t)fake_player,.inventory_get=(uintptr_t)get_player,.get_jm=(uintptr_t)get_root,
        .menu_system_vtable=(uintptr_t)table,.menu_system_show=(uintptr_t)show_root,.menu_system_primary=(uintptr_t)native_action,
        .settings_actor_get=(uintptr_t)get_actor,.ui_property=(uintptr_t)no_property,.active_offset=0x359,.invalid_offset=0x446,
        .settings_skill_name=(uintptr_t)fixture_name,.settings_skill_description=(uintptr_t)fixture_description,
        .settings_string_destroy=(uintptr_t)fixture_destroy,.settings_query_skill=(uintptr_t)fixture_query,
        .settings_empty_string=(uintptr_t)&empty_string,.settings_text_get=(uintptr_t)fixture_text};
    WideCharToMultiByte(936,0,L"踢击",-1,game_name,sizeof game_name,NULL,NULL);
    WideCharToMultiByte(936,0,L"原版技能说明",-1,game_description,sizeof game_description,NULL,NULL);
    WideCharToMultiByte(936,0,L"技能原版描述句",-1,game_narrative,sizeof game_narrative,NULL,NULL);
    table[0x24/4]=(uintptr_t)native_action;wr(fake_root,0,(uint32_t)(uintptr_t)table);
    wr(fake_world,0x30,(uint32_t)(uintptr_t)fake_ui);wr(fake_world,0x58,1);wr(fake_player,0x348,50);
    /* 战斗中并推杆仍可打开，由原Show暂停；不再以待机拒绝。 */
    wr(fake_actor,0x359,123);wr(fake_actor,0x73,17);
    CHECK(SettingsWindow_Pad(1u<<4,1u<<4,1,1,1,0,0,0,10));
    CHECK(active && pauses==1 && model.role==50 && model.page==SETTINGS_PAGE_KEYMAP);
    char keymap_description[1024];int keymap_selection,keymap_icon;keymap_state=0;keymap_effect(14,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(strstr(keymap_description,"调查"));
    int old_interact=model.draft.values[CONFIG_WORLD_INTERACT];model.draft.values[CONFIG_WORLD_INTERACT]=1;
    keymap_effect(14,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strstr(keymap_description,"调查"));keymap_effect(15,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(strstr(keymap_description,"调查"));
    model.draft.values[CONFIG_WORLD_INTERACT]=old_interact;model.page=0;
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
    backend.menu_map_global=(uintptr_t)&fixture_map_pointer;backend.menu_map_vtable=(uintptr_t)fixture_map_table;backend.menu_map_show=(uintptr_t)fixture_map_show;
    fixture_map_table[7]=(uintptr_t)fixture_map_show;wr(fixture_map_root,0,(uint32_t)(uintptr_t)fixture_map_table);
    map_host_side_effect=1;
    for(unsigned shown=0;shown<2;++shown){
        wr(fixture_map_root,0x64,shown);CHECK(open_window());CHECK(rd(fixture_map_root,0x64)==shown);
        SettingsWindow_Close();CHECK(rd(fixture_map_root,0x64)==shown);
    }
    CHECK(fixture_map_calls==2);map_host_side_effect=0;

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
    CHECK(open_window());CHECK(!wheel_last_row_regression());wr(surface,0xC,640);wr(surface,0x10,480);
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
    /* 信息页鼠标/手柄可达；带说明状态进入不解引用不存在的配置项。 */
    model.page=2;model.help=1;editing=picker=footer=0;
    SettingsWindow_Pad(0,0,0,0,0,0,0,0,100);
    SettingsWindow_Pad(1u<<10,1u<<10,0,0,0,0,0,0,110);CHECK(model.page==SETTINGS_PAGE_ABOUT);
    SettingsWindow_Pad(0,0,0,0,0,0,0,0,120);
    wr(surface,0xC,640);wr(surface,0x10,480);
    RECT about_canvas={0,0,960,600};FillRect(fixture_dc,&about_canvas,(HBRUSH)GetStockObject(WHITE_BRUSH));
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    snapshot_path="settings_about_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));
    char about_body[1024];const char *about_info=about_text(1,about_body,sizeof about_body);CHECK(strstr(about_info,EDSLASH_VERSION) && strstr(about_info,EDSLASH_BUILD_ID));
    about_info=about_text(2,about_body,sizeof about_body);CHECK(strstr(about_info,EDSLASH_AUTHOR));
    /* 验证超过2048宽字符的长FAQ完整转换且末尾可滚到，检查后恢复正式文案。 */
    char build_info[1024];const char *build_text=about_text(1,build_info,sizeof build_info);
    CHECK(strlen(EDSLASH_BUILD_DATE)==19 && EDSLASH_BUILD_DATE[4]=='-' && EDSLASH_BUILD_DATE[10]==' ' && EDSLASH_BUILD_DATE[16]==':');
    CHECK(strstr(build_text,EDSLASH_BUILD_DATE) && strstr(build_text,"构建日期") && !strstr(build_text,"北京时间"));
    const char *saved_faq=about_faq;static char long_faq[32768];
    for(unsigned n=0;n<100;++n)strcat(long_faq,"问：追加的问题。\n答：追加说明应完整显示，不能被固定缓存和高度裁掉。\n\n");
    strcat(long_faq,"末尾验收标记");about_clear_layout();about_faq=long_faq;about_layout();
    CHECK(about_heights[3]>ABOUT_VIEW_HEIGHT && wcsstr(about_wide[3],L"末尾验收标记") && wcslen(about_wide[3])>2048);
    scroll_at(370);CHECK(model.scroll[SETTINGS_PAGE_ABOUT]==about_max_scroll());
    about_clear_layout();about_faq=saved_faq;model.scroll[SETTINGS_PAGE_ABOUT]=0;about_layout();
    /* 从实际字形像素检查28/30/36像素按钮，文字上下留白不能继续偏向顶部。 */
    for(int height=28;height<=36;height+=2) {
        FillRect(fixture_dc,&about_canvas,(HBRUSH)GetStockObject(WHITE_BRUSH));
        action_button(fixture_dc,20,20,192,height,"完成","A",0);int first=height,last=-1;
        for(int yy=0;yy<height;++yy)for(int xx=8;xx<184;++xx){COLORREF c=GetPixel(fixture_dc,20+xx,20+yy);
            if(GetRValue(c)>120 && GetGValue(c)>120 && GetBValue(c)>90){if(yy<first)first=yy;if(yy>last)last=yy;}}
        CHECK(last>=first && abs(first-(height-1-last))<=2);
    }
    ConfigSnapshot about_before=model.draft;ConfigBinding about_bindings[14];memcpy(about_bindings,model.draft_bindings,sizeof about_bindings);
    activate();SettingsWindow_Pad(1u<<4,1u<<4,0,0,0,0,0,0,130);CHECK(!editing && !picker && !confirm_reset);
    CHECK(!memcmp(&about_before,&model.draft,sizeof about_before) && !memcmp(about_bindings,model.draft_bindings,sizeof about_bindings));
    SettingsWindow_Wheel(-120);CHECK(model.scroll[SETTINGS_PAGE_ABOUT]>0);
    scroll_at(370);CHECK(model.scroll[SETTINGS_PAGE_ABOUT]==about_max_scroll());
    FillRect(fixture_dc,&about_canvas,(HBRUSH)GetStockObject(WHITE_BRUSH));
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    snapshot_path="settings_about_bottom_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));
    move(2);CHECK(footer==1);move(4);CHECK(footer==2);move(4);CHECK(footer==2);move(1);CHECK(!footer);
    SettingsWindow_Pad(0,0,0,0,0,0,0,0,140);SettingsWindow_Pad(1u<<10,1u<<10,0,0,0,0,0,0,150);CHECK(model.page==SETTINGS_PAGE_KEYMAP);
    mouse_click(520,50);CHECK(model.page==SETTINGS_PAGE_ABOUT);mouse_click(470,50);CHECK(model.page==SETTINGS_PAGE_ABOUT); /* 页签间隙不切页 */
    mouse_click(50,50);CHECK(model.page==SETTINGS_PAGE_KEYMAP);
    model.help=1;footer=0;ConfigSnapshot diagram_before=model.draft;activate();
    SettingsWindow_Pad(0,1u<<4,0,0,0,0,0,0,160);CHECK(!editing && !picker && !confirm_reset && !memcmp(&diagram_before,&model.draft,sizeof diagram_before));
    FillRect(fixture_dc,&about_canvas,(HBRUSH)GetStockObject(WHITE_BRUSH));
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    snapshot_path="settings_keymap_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));
    for(unsigned state=0;state<KEYMAP_STATE_COUNT;++state){keymap_state=state;
        for(unsigned key=0;key<18;++key){keymap_effect(key,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(keymap_description[0]);}
        FillRect(fixture_dc,&about_canvas,(HBRUSH)GetStockObject(WHITE_BRUSH));paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
        const char *paths[]={"keymap_normal.bmp","keymap_lt.bmp","keymap_rt.bmp","keymap_dual.bmp","keymap_rescue.bmp"};snapshot_path=paths[state];CHECK(snapshot(&info.bmiHeader,pixels));
    }
    /* 直接核对路由对应的文字，不把旧显示常量作为正确答案。 */
    keymap_state=0;
    const unsigned direction_cells[]={5,11,12,13};const char *menu_labels[]={"角色属性","技能页面","任务日志","背包"};
    for(unsigned i=0;i<4;++i){keymap_effect(direction_cells[i],keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,menu_labels[i]));}
    keymap_effect(1,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,"回复物品组合"));
    keymap_effect(7,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,"投掷物品组合"));
    keymap_effect(6,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,"技能快捷组合"));
    keymap_effect(3,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,"开始奔跑"));
    keymap_effect(4,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(strstr(keymap_description,"START") && strstr(keymap_description,"救援"));
    keymap_effect(15,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(strstr(keymap_description,"预览"));
    CHECK(strstr(keymap_hint(),"松开发动") && strstr(keymap_hint(),"BACK"));
    keymap_state=1;keymap_effect(2,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,"触发闪避"));
    model.draft.values[CONFIG_COMBO_SWITCH]=0;model.draft.values[CONFIG_LEGACY_ULTIMATE]=0;CHECK(strstr(keymap_hint(),"Y/B/A/X"));
    model.draft.values[CONFIG_COMBO_SWITCH]=1;CHECK(strstr(keymap_hint(),"此模式面键不切套"));
    model.draft.values[CONFIG_LEGACY_ULTIMATE]=1;CHECK(strstr(keymap_hint(),"旧必杀") && strstr(keymap_hint(),"再按同键释放"));
    keymap_state=2;keymap_effect(2,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,"移动／定向"));
    keymap_state=3;keymap_effect(4,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,"EDSlash设置"));
    CHECK(strstr(keymap_hint(),"未开菜单") && strstr(keymap_hint(),"ABXY无效") && strstr(keymap_hint(),"松扳机确认"));
    SettingsModel_Discard(&model);
    /* 第五页逐键对应真实救援映射；普通改绑、无角色、技能草稿不能污染鼠标说明。 */
    SettingsModel rescue_before=model;keymap_state=4;unsigned saved_role=model.role;model.role=0;
    for(unsigned key=0;key<KEYMAP_KEY_COUNT;++key){
        keymap_effect(key,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);
        const char *expected=key==0 ? RuntimeText_KeymapRescueRightClick:key==6 ? RuntimeText_KeymapRescueLeftClick:
            key==2 ? RuntimeText_KeymapRescueMove:key==8 ? RuntimeText_KeymapRescueFineMove:
            key==4 ? RuntimeText_KeymapRescueBack:key==10 ? RuntimeText_KeymapRescueStart:RuntimeText_KeymapNoEffect;
        CHECK(!strcmp(keymap_description,expected) && keymap_selection==-1 && keymap_icon==-1);
    }
    model.role=saved_role;CHECK(!memcmp(&model,&rescue_before,sizeof model));
    move(4);CHECK(keymap_state==0);move(3);CHECK(keymap_state==4);
    mouse_click(410,100);CHECK(keymap_state==0);mouse_click(195,100);CHECK(keymap_state==4);
    /* 键位图即时反映草稿的导航模式；非负责摇杆明确显示无效果。 */
    keymap_state=3;
    for(int mode=0;mode<3;++mode){
        model.draft.values[CONFIG_ACTION_MENU_NAV]=mode;
        keymap_effect(2,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);
        CHECK(!strcmp(keymap_description,mode==0 ? RuntimeText_KeymapBrowse:mode==1 ? RuntimeText_KeymapNoEffect:RuntimeText_KeymapLeftMenuBrowse));
        keymap_effect(8,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);
        CHECK(!strcmp(keymap_description,mode==0 ? RuntimeText_KeymapNoEffect:mode==1 ? RuntimeText_KeymapBrowse:RuntimeText_KeymapRightMenuBrowse));
        value_text(CONFIG_ACTION_MENU_NAV,keymap_description,sizeof keymap_description);
        CHECK(!strcmp(keymap_description,RuntimeText_SkillMenuNavigationValues[mode]));
    }
    SettingsModel_Discard(&model);
    keymap_state=2;model.draft_bindings[0]=(ConfigBinding){0,0,0};keymap_effect(14,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,RuntimeText_KeymapNoEffect));
    model.draft_bindings[0]=(ConfigBinding){1,123,1};skill_count=1;skills[0].selector=123;skills[0].icon=2;strcpy(skills[0].name,"测试技能");
    keymap_effect(14,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,"测试技能") && keymap_selection==123 && keymap_icon==2);SettingsModel_Discard(&model);
    keymap_state=1;keymap_effect(14,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(!strcmp(keymap_description,RuntimeText_KeymapNoEffect));
    static BYTE shown_combo_node[16];wr(fake_hud,(3+13)*16,1);wr(fake_hud,(3+13)*16+4,(uint32_t)(uintptr_t)shown_combo_node);
    keymap_effect(14,keymap_description,sizeof keymap_description,&keymap_selection,&keymap_icon);CHECK(strstr(keymap_description,"第3套"));
    wr(fake_hud,(3+13)*16,0);keymap_state=0;move(4);CHECK(keymap_state==1);move(3);CHECK(keymap_state==0);mouse_click(410,100);CHECK(keymap_state==1);mouse_click(195,100);CHECK(keymap_state==0);
    move(2);CHECK(footer==1);move(4);CHECK(footer==2);move(4);CHECK(footer==2);
    mouse_click(16+TAB_STEP+20,50);CHECK(model.page==0);model.help=0;footer=0;
    /* 数值编辑取消只撤销当前字段；技能候选取消不改绑定，选中才写草稿。 */
    model.page=1;model.focus[1]=0;activate();CHECK(editing);move(4);cancel();CHECK(!editing && !SettingsModel_Dirty(&model));
    model.page=2;skill_count=1;skills[0].selector=123;strcpy(skills[0].name,"测试技能");activate();
    move(2);cancel();CHECK(!model.draft_bindings[0].custom);
    activate();move(2);activate();CHECK(model.draft_bindings[0].custom && model.draft_bindings[0].selector==123);
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
    CHECK(skill_count==1 && strstr(skills[0].description,"技能原版描述句") && strstr(skills[0].description,"原版技能说明") && descriptions==1 && destroyed==1);
    skills[1]=skills[0];skills[1].selector=124;skills[1].kind=0;skill_count=2;picker_rebuild(123);CHECK(skill_view_count==3 && skill_view[0]==UINT32_MAX);
    skill_count=1;
    /* 大写键位只改显示，不改变保存枚举；三个页面都有真实滚动位置。 */
    char key_text[64];value_text(CONFIG_WORLD_INTERACT,key_text,sizeof key_text);CHECK(!strcmp(key_text,"A"));
    value_text(CONFIG_WORLD_SYSTEM,key_text,sizeof key_text);CHECK(!strcmp(key_text,"START"));
    picker=editing=0;model.page=0;model.scroll[0]=0;model.focus[0]=7;RECT h=help_rectangle();CHECK(h.left==16 && h.top==82);
    model.focus[0]=0;h=help_rectangle();CHECK(h.left==316 && h.top==194);
    RECT thumb=scroll_thumb();CHECK(thumb.bottom<370 && thumb.top==82);scroll_at(370);CHECK(model.scroll[0]>0 && model.focus[0]/2>=model.scroll[0]);
    model.page=1;model.scroll[1]=0;thumb=scroll_thumb();CHECK(thumb.top==82 && thumb.bottom<370);
    scroll_at(370);CHECK(model.scroll[1]>0 && model.focus[1]/2>=model.scroll[1]);
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
    wr(fake_hud,0xC18,(uint32_t)(uintptr_t)default_record);wr(default_record,0x14,123);wr(default_record,0x18,'Q');
    wr(fake_actor,0x193,(uint32_t)(uintptr_t)fixture_choices);CHECK(open_window());
    CHECK(!effective_binding(0).custom && !effective_binding(12).custom);
    CHECK(skill_count==1 && !strcmp(skills[0].name,"踢击") && strstr(skills[0].description,"技能原版描述句") && strstr(skills[0].description,"原版技能说明"));
    wr(surface,0xC,640);wr(surface,0x10,480);
    model.page=0;model.focus[0]=7;model.help=1;
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    snapshot_path="settings_help_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));
    model.page=1;model.focus[1]=0;model.help=0;activate();
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    snapshot_path="settings_edit_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));cancel();
    model.page=2;activate();move(2);model.help=1;
    backend.settings_icon_global=(uintptr_t)&icons_pointer;backend.icon_draw=(uintptr_t)fixture_icon_draw;
    backend.focus_frame_get=(uintptr_t)fixture_frame;backend.focus_image_get=(uintptr_t)fixture_image;
    wr(fixture_sprites+64,0,(uint32_t)(uintptr_t)icon_frame);wr(fixture_sprites+64,8,1);
    wr(icon_frame,8,(uint32_t)(uintptr_t)icon_bank);wr(icon_bank,4,(uint32_t)(uintptr_t)&icon_entry);
    wr(icon_image,0xC,43);wr(icon_image,0x10,47);
    wr(fixture_icons,0x44,1000);wr(fixture_icons,0x48,(uint32_t)(uintptr_t)fixture_sprites);
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    CHECK(icon_calls==1 && icon_abi_ok);
    RECT small=icon_rectangle(24,170,48,43,47,38);CHECK(small.bottom-small.top==38 && small.top==175);
    RECT compact=icon_rectangle(16,82,42,43,47,32);CHECK(compact.bottom-compact.top==32 && compact.top==87);
    CHECK(small.right-small.left<43 && compact.right-compact.left<43);
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);CHECK(icon_calls==1); /* 复用缓存 */
    /* 同一真实图标库在技能页能画，也必须在RT、双扳机及旧单LT键位页画；0x24是空值陷阱。 */
    SettingsModel icons_before=model;int picker_before=picker;unsigned state_before=keymap_state;
    picker=0;model.page=SETTINGS_PAGE_KEYMAP;model.role=4;skill_count=1;skills[0].selector=123;skills[0].icon=2;strcpy(skills[0].name,"测试技能");
    model.draft_bindings[0]=(ConfigBinding){1,123,1};keymap_finishers[0].selector=123;keymap_finishers[0].icon=2;strcpy(keymap_finishers[0].name,"测试必杀");
    unsigned native_icon_before=icon_calls;
    wr(fixture_icons,0x24,0);wr(fixture_icons,0x28,0);
    const unsigned icon_states[]={2,3,1};
    for(unsigned n=0;n<3;++n){keymap_state=icon_states[n];model.draft.values[CONFIG_LEGACY_ULTIMATE]=n==2;
        FillRect(fixture_dc,&about_canvas,(HBRUSH)GetStockObject(WHITE_BRUSH));paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
        RECT key_glyph=icon_rectangle(414,302,32,43,47,24);
        CHECK(GetPixel(fixture_dc,origin_x+(key_glyph.left+key_glyph.right)/2,origin_y+(key_glyph.top+key_glyph.bottom)/2)==RGB(38,66,92));
        const char *icon_paths[]={"keymap_icons_rt.bmp","keymap_icons_dual.bmp","keymap_icons_legacy.bmp"};
        snapshot_path=icon_paths[n];CHECK(snapshot(&info.bmiHeader,pixels));
    }
    CHECK(icon_calls>native_icon_before);
    model=icons_before;picker=picker_before;keymap_state=state_before;
    move(1);activate();CHECK(!model.draft_bindings[0].custom); /* 第一项未设置 */
    snapshot_path="settings_skill_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));
    CHECK(!picker);SettingsWindow_Close();
    clear_icon_cache();
    /* 全部数值用最小单位：左右1、上下10；百分比底层1即0.01%。 */
    CHECK(open_window());model.page=1;model.focus[1]=0;activate();int number=model.draft.values[CONFIG_DEADZONE];
    move(4);CHECK(model.draft.values[CONFIG_DEADZONE]==number+1);move(1);CHECK(model.draft.values[CONFIG_DEADZONE]==number+11);
    move(3);move(2);CHECK(model.draft.values[CONFIG_DEADZONE]==number);
    int old_dir=0;uint32_t started=0,next=0;repeat_direction(4,100,&old_dir,&started,&next);
    repeat_direction(4,2100,&old_dir,&started,&next);CHECK(next==2135);
    edit_id=CONFIG_GUARD_PERCENT;model.focus[1]=0; /* 改为该字段所在的真实页/焦点。 */
    model.page=0;for(unsigned i=0;i<SettingsModel_Count(0);++i)if(SettingsModel_Field(0,i)==CONFIG_GUARD_PERCENT)model.focus[0]=i;
    model.draft.values[CONFIG_GUARD_PERCENT]=0;old_dir=0;repeat_direction(4,100,&old_dir,&started,&next);
    CHECK(model.draft.values[CONFIG_GUARD_PERCENT]==1);repeat_direction(4,2100,&old_dir,&started,&next);CHECK(next==2104);
    model.draft.values[CONFIG_GUARD_PERCENT]=10000;move(1);CHECK(model.draft.values[CONFIG_GUARD_PERCENT]==10000);
    model.draft.values[CONFIG_GUARD_PERCENT]=0;move(2);CHECK(model.draft.values[CONFIG_GUARD_PERCENT]==0);
    editing=0;footer=picker=0;barrier=0;model.page=1;model.focus[1]=0;
    ConfigId current=SettingsModel_Field(1,0);const ConfigDescriptor *field=RuntimeConfig_Descriptor(current);
    model.draft.values[current]=field->default_value+1;activate();
    SettingsWindow_Pad(0,1u<<4,0,0,0,0,0,0,5000);
    CHECK(editing && !confirm_reset && model.draft.values[current]==field->default_value);
    editing=0;model.draft.values[current]=field->default_value+1;
    SettingsWindow_Pad(0,1u<<4,0,0,0,0,0,0,5020);CHECK(confirm_reset && model.draft.values[current]!=field->default_value);
    cancel();CHECK(!confirm_reset && model.draft.values[current]!=field->default_value);
    int other=model.draft.values[CONFIG_CENTER_HUD];model.page=1;confirm_reset=1;activate();
    CHECK(!confirm_reset && model.draft.values[current]==field->default_value && model.draft.values[CONFIG_CENTER_HUD]==other);
    model.page=2;model.draft_bindings[0]=(ConfigBinding){1,123,1};model.focus[2]=0;picker=1;
    SettingsWindow_Pad(0,1u<<4,0,0,0,0,0,0,5040);CHECK(!picker && !model.draft_bindings[0].custom);
    model.page=2;model.focus[2]=0;activate();CHECK(picker);pick_focus=1;
    SettingsWindow_Pad(0,1u<<6,0,0,0,0,0,0,5050);
    CHECK(!picker && model.saved_bindings[0].custom && model.saved_bindings[0].selector==123 && strstr(message,"已保存"));
    model.page=0;model.draft.values[CONFIG_CENTER_HUD]=!model.saved.values[CONFIG_CENTER_HUD];
    SettingsWindow_Pad(0,1u<<6,0,0,0,0,0,0,5060);
    CHECK(!SettingsModel_Dirty(&model) && model.help && strstr(message,"已保存"));
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    snapshot_path="settings_saved_fixture.bmp";CHECK(snapshot(&info.bmiHeader,pixels));
    confirm_reset=1;menu_swap=1;SettingsWindow_Pad(0,1u,0,0,0,0,0,0,5080);CHECK(!confirm_reset);
    menu_swap=0;editing=0;SettingsModel_Discard(&model);SettingsWindow_Close();
    backend.menu_settings_vtable=(uintptr_t)native_settings_table;backend.menu_settings_show=(uintptr_t)show_root;
    backend.menu_settings_primary=(uintptr_t)native_action;backend.menu_native_text_draw=(uintptr_t)native_caption;
    native_settings_table[0x24/4]=(uintptr_t)native_action;wr(native_settings_root,0,(uint32_t)(uintptr_t)native_settings_table);
    wr(native_settings_root,0x28,0xAA);wr(native_settings_root,0x64,1);wr(fake_world,0x58,0);wr(fake_ui,0x3C,(uint32_t)(uintptr_t)native_settings_root);
    RuntimeFocusRect entry;CHECK(SettingsWindow_NativeEntryRect(native_settings_root,&entry));
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);CHECK(native_captions==1 && GetPixel(fixture_dc,entry.left+5,entry.top+5)==RGB(0,0,0));
    SettingsWindow_NativeEntryFocus(native_settings_root,1);expected_caption_color=RGB(255,255,0);
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);CHECK(native_captions==2 && rd(native_settings_root,0x78)==0);
    SettingsWindow_NativeEntryFocus(NULL,0);expected_caption_color=0;
    unsigned pause_before=pauses;CHECK(SettingsWindow_OpenNative(native_settings_root) && !model.role && pauses==pause_before);
    CHECK(!wheel_last_row_regression());
    /* 验证无角色时拒绝编辑并给出提示，不将提示的具体措辞当成业务协议。 */
    model.page=2;ConfigSnapshot disabled_draft=model.draft;ConfigBinding disabled_bindings[14];
    memcpy(disabled_bindings,model.draft_bindings,sizeof disabled_bindings);
    activate();CHECK(!picker && !editing && model.help && message[0]);
    CHECK(!memcmp(&disabled_draft,&model.draft,sizeof disabled_draft) && !memcmp(disabled_bindings,model.draft_bindings,sizeof disabled_bindings));
    CHECK(!SettingsModel_SetBinding(&model,1,(ConfigBinding){1,123,1}));
    model.page=0;CHECK(SettingsModel_SetInt(&model,CONFIG_AIM_EXPAND_MS,700));save_settings();CHECK(!SettingsModel_Dirty(&model));
    model.page=SETTINGS_PAGE_ABOUT;model.help=1;activate();CHECK(!editing && !picker && !confirm_reset);
    paint(RUNTIME_EVENT_UI_DRAW_END,fake_root,(unsigned long)(uintptr_t)surface,0,NULL);
    CHECK(!model.role && !SettingsModel_Dirty(&model));
    SettingsWindow_Close();CHECK(!active && rd(native_settings_root,0x64) && rd(fake_ui,0x3C)==(uint32_t)(uintptr_t)native_settings_root);
    SelectObject(fixture_dc,previous_bitmap);DeleteObject(bitmap);DeleteDC(fixture_dc);
    if(font){DeleteObject(font);font=NULL;}
    if(help_font){DeleteObject(help_font);help_font=NULL;}
    if(keymap_font){DeleteObject(keymap_font);keymap_font=NULL;}
    for(unsigned i=0;i<brush_count;++i)DeleteObject(brushes[i].brush);
    CHECK(DeleteFileW(path));if(GetFileAttributesW(skill_file)!=INVALID_FILE_ATTRIBUTES)CHECK(DeleteFileW(skill_file));CHECK(RemoveDirectoryW(directory));printf("原生设置暂停/捕获/关闭链与模型入口回放通过：%u项\n",checks);return 0;
}
