#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "SettingsWindow.h"
#include "BuildInfo.h"
#include "SettingsModel.h"
#include "Win32Bridge.h"
#include "Log.h"
#include "Focus.h"

typedef int (__attribute__((thiscall)) *This0)(void *);
typedef int (__attribute__((thiscall)) *This1)(void *,int);
typedef int (__attribute__((thiscall)) *This2)(void *,int,int);
typedef int (__attribute__((thiscall)) *This3)(void *,int,int,void *);
typedef int (__attribute__((thiscall)) *IconDraw)(void *,int,int,int,int,int,int,int,int,int);
typedef struct {
    uintptr_t world_global,ui,skill_global,inventory_root,inventory_get,get_jm;
    uintptr_t menu_system_vtable,menu_system_show,menu_system_primary;
    uintptr_t settings_actor_get,settings_string_get,settings_icon_global;
    uintptr_t ui_property,skill_groups,methods,lookup,skill_eligibility,icon_resolve,icon_draw;
    uintptr_t settings_skill_name,settings_skill_description,settings_string_destroy,settings_query_skill,settings_empty_string,settings_text_get,settings_text_table,focus_frame_get,focus_image_get;
    unsigned active_offset,invalid_offset;uintptr_t menu_settings_vtable,menu_settings_show,menu_settings_primary,menu_native_text_draw;
    BYTE signatures[21][12];
} SettingsBackend;
#include "SettingsData.h"
static SettingsBackend backend;
static SettingsModel model;
static unsigned game;
static int ready,active,editing,footer,confirm_discard,picker,pick_focus,pick_scroll,barrier;
static int draw_subscribed,input_subscribed,menu_swap,error_modal,picker_footer,scroll_drag;
static ConfigId edit_id;static int edit_before;static char edit_text_before[40];
static void *root;static uintptr_t old_primary,old_draw,host_table,host_show;
static int native_host;
static void paint(RuntimeEventId,void *,unsigned long,unsigned long,void *);
static void clear_icon_cache(void);
static HFONT font,help_font;static int origin_x,origin_y,logical_width,logical_height;
static struct {COLORREF color;HBRUSH brush;} brushes[32];static unsigned brush_count;
static uint32_t repeat_at,hold_since;static int previous_direction;
static int pointer_mode;static POINT previous_pointer;
static int previous_open_key,previous_left,previous_right,previous_escape;
static char message[160];
static int confirm_reset;
/* 候选只保存整数选择和已转换名字；不缓存角色/技能资源裸指针。 */
static struct {int selector,icon,kind;char name[128],description[2048];} skills[128];static unsigned skill_count;
static unsigned skill_view[128],skill_view_count;
/* 只在构建候选的同步调用期间借用学习记录，构建结束全部清空。 */
static void *learned_records[16],*skill_player;

static int copy_game_text(const char *source,char *out,unsigned capacity)
{
    if(!source || !out || !capacity)return 0;
    char bytes[1536];unsigned n=0;
    while(n<sizeof bytes-1 && RuntimeWin32_IsReadable((unsigned long)(uintptr_t)(source+n),1) && source[n]){bytes[n]=source[n];++n;}
    bytes[n]=0;out[0]=0;if(!n)return 0;
    WCHAR wide[1536];if(!MultiByteToWideChar(936,0,bytes,-1,wide,1536))return 0;
    return WideCharToMultiByte(CP_UTF8,0,wide,-1,out,(int)capacity,NULL,NULL)>0;
}
static unsigned rd(const void *p,unsigned offset)
{unsigned v=0;if(p)RuntimeWin32_Read((unsigned long)(uintptr_t)p+offset,&v,4);return v;}
static void *ptr(const void *p,unsigned offset){return (void *)(uintptr_t)rd(p,offset);}
static int readable(const void *p,unsigned bytes)
{return p && RuntimeWin32_IsReadable((unsigned long)(uintptr_t)p,bytes);}
static void *actor(void)
{
    void *world=ptr((void *)backend.world_global,0),*manager=ptr(world,0x30);
    return readable(manager,0x10) ? (void *)(uintptr_t)((This0)backend.settings_actor_get)(manager):NULL;
}
static int foreground(void)
{
    DWORD pid=0;HWND w=GetForegroundWindow();if(w)GetWindowThreadProcessId(w,&pid);
    return pid==GetCurrentProcessId();
}
static int __attribute__((fastcall)) capture_primary(void *self,void *unused,int e,int x,void *y)
{
    (void)unused;
    /* 自建窗口打开时原系统按钮不可被透明穿透点击；关闭恢复安装前的同一调用链。 */
    if(active && self==root)return 1;
    return old_primary ? ((This3)old_primary)(self,e,x,y):1;
}
static int __attribute__((fastcall)) fallback_draw(void *self,void *unused,unsigned long surface)
{
    (void)unused;int result=old_draw ? ((This1)old_draw)(self,(int)surface):1;
    if(active && !RuntimeConfig_GetInt(CONFIG_DISPLAY_ENABLED))paint(RUNTIME_EVENT_UI_DRAW_END,self,surface,0,NULL);
    return result;
}
void SettingsWindow_Close(void)
{
    if(!active)return;
    /* 撤回失败时保留窗口和旧调用链，不能清空回调后把原系统菜单永久吞掉。
     * 先还原Draw，随后还原输入；后者失败则补回窗口Draw，供用户再次关闭。 */
    if(old_draw && rd((void *)host_table,8)==(unsigned)(uintptr_t)fallback_draw) {
        int result=RuntimeWin32_WriteCode((unsigned long)(host_table+8),&old_draw,4);
        if(result!=1 && rd((void *)host_table,8)==(unsigned)(uintptr_t)fallback_draw) {
            strcpy(message,"原绘制入口恢复失败，请再次关闭。");return;
        }
    }
    if(readable((void *)(host_table+0x24),4) &&
        rd((void *)host_table,0x24)==(unsigned)(uintptr_t)capture_primary) {
        int result=RuntimeWin32_WriteCode((unsigned long)(host_table+0x24),&old_primary,4);
        if(result!=1 && rd((void *)host_table,0x24)==(unsigned)(uintptr_t)capture_primary) {
            if(old_draw){uintptr_t hook=(uintptr_t)fallback_draw;RuntimeWin32_WriteCode((unsigned long)(host_table+8),&hook,4);}
            strcpy(message,"原输入入口恢复失败，请再次关闭。");return;
        }
    }
    old_draw=0;
    if(!native_host && readable(root,0x68))((This2)host_show)(root,0,0);
    active=editing=picker=confirm_discard=confirm_reset=0;root=NULL;old_primary=0;message[0]=0;
    RuntimeLog_Write("[模组设置] 关闭并释放原菜单捕获，原游戏恢复。");
}
int SettingsWindow_Active(void){return active;}
int SettingsWindow_ShowPointer(void){return active && pointer_mode;}
static void add_skill(void *role,int id,int direct)
{
    if(id<0 || id==0xFFFF || skill_count>=128)return;
    /* 技能组编号与可施放selector不是同一字段。原动作菜单取组+24的16位编号，
     * 资格仍按组编号查询；保存时绝不能把用于查表的组编号误作玩家动作。 */
    void *g;int selector;
    if(direct) {
        return; /* 原绑定不是学习记录；可设置的普通技能由已学组表提供，不补投掷/空动作。 */
    } else {
        g=(void *)(uintptr_t)((This1)backend.lookup)((void *)backend.skill_groups,id);
        if(!readable(g,0x36) || rd(g,0x32)>=2 || ((This1)backend.skill_eligibility)(role,id)==-1)return;
        selector=(int)(rd(g,0x24)&0xFFFFu);
        if(selector==0xFFFF)return;
        for(unsigned i=0;i<skill_count;++i)if(skills[i].selector==selector)return;
    }
    /* 原技能名是技能组自身的游戏文本，不是普通字符串表GetString(0)。
     * 名称或图标无效就不生成可选条目，避免一排没有身份的“可用动作”。 */
    int icon=(int)(rd(g,0x22)&0xFFFFu);
    const char *name=(const char *)(uintptr_t)((This0)backend.settings_skill_name)(g);
    if(icon==0xFFFF || !copy_game_text(name,skills[skill_count].name,sizeof skills[skill_count].name))return;
    skills[skill_count].selector=selector;skills[skill_count].icon=icon;skills[skill_count].kind=(int)rd(g,0x32);
    strcpy(skills[skill_count].description,skills[skill_count].name);
    for(unsigned i=0;i<16;++i)if(learned_records[i] && ((This1)backend.ui_property)(learned_records[i],2)==id) {
        /* 学习页先拼技能名与记录第15项指向的描述句，再追加原数值说明。
         * 仅调用数值函数会漏掉描述句；detail=0避免重复标题和连招页专用文字。 */
        char narrative[1024]={0};int text_id=((This1)backend.ui_property)(learned_records[i],15);
        if(text_id>=0 && text_id!=0xFFFF) {
            const char *body=(const char *)(uintptr_t)((This2)backend.settings_text_get)((void *)backend.settings_text_table,text_id,1);
            copy_game_text(body,narrative,sizeof narrative);
        }
        unsigned game_string=rd((void *)backend.settings_empty_string,0);
        if(game_string) {
            typedef void (__cdecl *Description)(void *,void *,unsigned *,int);
            char statistics[1024]={0};
            ((Description)backend.settings_skill_description)(skill_player,learned_records[i],&game_string,0);
            copy_game_text((const char *)(uintptr_t)game_string,statistics,sizeof statistics);
            ((This0)backend.settings_string_destroy)(&game_string);
            char heading[128];memcpy(heading,skills[skill_count].name,sizeof heading);
            snprintf(skills[skill_count].description,sizeof skills[skill_count].description,"%s\n%s%s%s",
                heading,narrative,narrative[0] ? "\n":"",statistics);
        }
        break;
    }
    ++skill_count;
}
static void build_skills(void)
{
    memset(skills,0,sizeof skills);skill_count=0;memset(learned_records,0,sizeof learned_records);
    skill_player=(void *)(uintptr_t)((This0)backend.inventory_get)((void *)backend.inventory_root);
    if(readable(skill_player,0x34C))for(unsigned i=0;i<16;++i) {
        typedef void (__attribute__((thiscall)) *Query)(void *,void **,void **,int);
        void *learned=NULL,*next=NULL;((Query)backend.settings_query_skill)(skill_player,&learned,&next,(int)i);
        if(readable(learned,16))learned_records[i]=learned;
    }void *role=actor(),*choices=ptr(role,0x193);
    if(readable(choices,12)) {
        int n=((This1)backend.ui_property)(choices,1);
        if(n>=0 && n<=1024)for(int i=0;i<n && skill_count<128;++i)add_skill(role,((This1)backend.ui_property)(choices,i+2),0);
    }
    memset(learned_records,0,sizeof learned_records);skill_player=NULL;
}
static int open_on(void *native_page)
{
    /* 窗口不能在Loader锁内打开；这里只由游戏输入线程的组合键/反引号边沿调用。
     * 原Show负责暂停和捕获，本函数不写角色动作状态，也不创建自己的暂停计数。 */
    if(!ready || active || !foreground())return 0;
    /* 上一次失败回滚若仍留着自己的包装，不能再把自己当旧入口形成递归链。 */
    if(rd((void *)host_table,0x24)==(unsigned)(uintptr_t)capture_primary ||
        rd((void *)host_table,8)==(unsigned)(uintptr_t)fallback_draw)return 0;
    void *world=ptr((void *)backend.world_global,0),*role=actor();
    if(!native_page && (!rd(world,0x58) || !readable(role,backend.invalid_offset+4) || rd(role,backend.invalid_offset)))return 0;
    /* 原捕获中的其它菜单不被自建窗口替换；战斗/走路不作为拒绝理由，由原暂停保护。 */
    void *capture=ptr((void *)backend.ui,0x3C);
    if(capture && (!native_page || (capture!=native_page && ptr(capture,0xA4)!=native_page)))return 0;
    /* 普通菜单模块只包装Tick/Show/hover，主操作+24保留原版是正常状态。
     * 当前是否可开由场景和捕获判断，不能要求这个槽先被另一个模块改写。 */
    native_host=native_page!=NULL;host_table=native_host ? backend.menu_settings_vtable:backend.menu_system_vtable;
    host_show=native_host ? backend.menu_settings_show:backend.menu_system_show;
    root=native_page ? native_page:(void *)(uintptr_t)((This1)backend.get_jm)((void *)backend.ui,0x2D);
    void *data=rd(world,0x58) ? (void *)(uintptr_t)((This0)backend.inventory_get)((void *)backend.inventory_root):NULL;
    unsigned selector=rd(world,0x58) && readable(role,backend.invalid_offset+4) && !rd(role,backend.invalid_offset) && readable(data,0x34C) ? rd(data,0x348):0;
    if(!readable(root,0xC0) || rd(root,0)!=host_table || !SettingsModel_Open(&model,game,selector)){root=NULL;return 0;}
    old_primary=rd((void *)host_table,0x24);uintptr_t replacement=(uintptr_t)capture_primary;
    if(!old_primary)return 0;
    int installed=RuntimeWin32_WriteCode((unsigned long)(host_table+0x24),&replacement,4);
    if(installed!=1) {
        /* 2代表内存已写入但系统收尾失败，必须还原；不能将其冒充完全成功。 */
        if(installed==2)RuntimeWin32_WriteCode((unsigned long)(host_table+0x24),&old_primary,4);
        root=NULL;return 0;
    }
    if(!RuntimeConfig_GetInt(CONFIG_DISPLAY_ENABLED)) {
        old_draw=rd((void *)host_table,8);uintptr_t hook=(uintptr_t)fallback_draw;
        int draw_result=old_draw ? RuntimeWin32_WriteCode((unsigned long)(host_table+8),&hook,4):0;
        if(draw_result!=1) {
            if(draw_result==2)RuntimeWin32_WriteCode((unsigned long)(host_table+8),&old_draw,4);
            RuntimeWin32_WriteCode((unsigned long)(host_table+0x24),&old_primary,4);root=NULL;return 0;
        }
    }
    /* 本次窗口固定使用打开时的确认布局，保存交换AB后不在半次编辑中改变含义。 */
    menu_swap=RuntimeConfig_GetInt(CONFIG_MENU_SWAP_AB);
    active=1;barrier=1;editing=footer=picker=confirm_discard=confirm_reset=error_modal=picker_footer=scroll_drag=0;previous_direction=0;message[0]=0;
    clear_icon_cache();
    if(!native_host)((This2)host_show)(root,1,0);
    skill_count=skill_view_count=0;if(selector)build_skills();
    RuntimeLog_Write("[模组设置] 打开并取得原系统菜单暂停/捕获，角色selector=%u。",selector);return 1;
}
static int open_window(void) {return open_on(NULL);}
static void *native_entry_focus;static int native_entry_hover;
void SettingsWindow_NativeEntryFocus(void *page,int selected){native_entry_focus=selected ? page:NULL;}
int SettingsWindow_NativeEntryRect(void *page,RuntimeFocusRect *rectangle)
{
    if(!ready || active || !rectangle || !readable(page,0xC0) || rd(page,0)!=backend.menu_settings_vtable || rd(page,0x28)!=0xAA || !rd(page,0x64))return 0;
    int x=(int)rd(page,0x14),y=(int)rd(page,0x18);
    *rectangle=(RuntimeFocusRect){x+400,y+306,x+576,y+342};return 1;
}
int SettingsWindow_OpenNative(void *page)
{
    RuntimeFocusRect r;if(!SettingsWindow_NativeEntryRect(page,&r))return 0;
    return open_on(page);
}
static ConfigBinding effective_binding(unsigned slot)
{return model.draft_bindings[slot];}
static void picker_rebuild(int selector)
{
    /* 快捷技能独立于左右手槽位，列出全部可设置的已学普通技能。 */
    skill_view_count=1;skill_view[0]=UINT32_MAX;pick_focus=pick_scroll=picker_footer=0;
    for(unsigned i=0;i<skill_count;++i){if(skills[i].selector==selector)pick_focus=(int)skill_view_count;skill_view[skill_view_count++]=i;}
    if(pick_focus>=5)pick_scroll=pick_focus-4;
}
typedef HRESULT (__stdcall *SurfaceGetDC)(void *,HDC *);
typedef HRESULT (__stdcall *SurfaceReleaseDC)(void *,HDC);
static struct {uintptr_t image;COLORREF color;int width,height;HBITMAP bitmap;} icon_cache[64];
static unsigned cache_next;
static void clear_icon_cache(void)
{
    for(unsigned i=0;i<64;++i){if(icon_cache[i].bitmap)DeleteObject(icon_cache[i].bitmap);memset(&icon_cache[i],0,sizeof icon_cache[i]);}
    cache_next=0;
}
static RECT icon_rectangle(int cell_x,int cell_y,int row_height,int sw,int sh,int maximum)
{
    /* 按比例缩小，实际图像高度参与居中；不会把原裁取API误当缩放API。 */
    int w=sw,h=sh;
    if(w>maximum || h>maximum){if(w>=h){h=MulDiv(h,maximum,w);w=maximum;}else {w=MulDiv(w,maximum,h);h=maximum;}}
    if(w<1)w=1;
    if(h<1)h=1;
    int x=cell_x+8+(40-w)/2,y=cell_y+(row_height-h)/2;
    return (RECT){x,y,x+w,y+h};
}
static void scaled_icon(unsigned long context,void *icons,void *animation,int icon,int selector,int cx,int cy,int row_height,int maximum,COLORREF color)
{
    /* 先核对当前动画、登记图像和尺寸。只存图像的整数身份，不跨帧解引用旧对象。 */
    unsigned n=rd(animation,8),index=rd(animation,4);void *frames=ptr(animation,0);
    if(!n || n>4096 || index>=n || !readable((BYTE *)frames+index*22u,22))return;
    void *frame=(void *)(uintptr_t)((This0)backend.focus_frame_get)(animation);
    if(!readable(frame,22))return;
    void *bank=ptr(frame,8),*entries=ptr(bank,4);unsigned resource=rd(frame,4)&0xFFFFFu;
    if(!readable(bank,8) || !readable((BYTE *)entries+resource*4u,4) || !readable(ptr(entries,resource*4u),0x28))return;
    void *image=(void *)(uintptr_t)((This0)backend.focus_image_get)(frame);
    if(!readable(image,0x14))return;
    int sw=(int)rd(image,0xC),sh=(int)rd(image,0x10);if(sw<1 || sh<1 || sw>128 || sh>128)return;
    void *surface=(void *)(uintptr_t)context,*dd=ptr(surface,0x2D),*table=ptr(dd,0);
    if(!readable(table,0x6C))return;
    SurfaceGetDC get=(SurfaceGetDC)(uintptr_t)rd(table,0x44);
    SurfaceReleaseDC release=(SurfaceReleaseDC)(uintptr_t)rd(table,0x68);
    RECT target=icon_rectangle(cx,cy,row_height,sw,sh,maximum);HDC dc=NULL;
    if(get(dd,&dc)!=S_OK || !dc)return;
    HDC work=CreateCompatibleDC(dc),backup=CreateCompatibleDC(dc);HBITMAP glyph=NULL,saved=NULL;
    if(!work || !backup){if(work)DeleteDC(work);if(backup)DeleteDC(backup);release(dd,dc);return;}
    unsigned slot=64;
    for(unsigned i=0;i<64;++i)if(icon_cache[i].bitmap && icon_cache[i].image==(uintptr_t)image && icon_cache[i].color==color && icon_cache[i].width==sw && icon_cache[i].height==sh){slot=i;break;}
    if(slot<64)glyph=icon_cache[slot].bitmap;
    else {
        glyph=CreateCompatibleBitmap(dc,sw,sh);saved=CreateCompatibleBitmap(dc,sw,sh);
        if(!glyph || !saved){if(glyph)DeleteObject(glyph);if(saved)DeleteObject(saved);DeleteDC(work);DeleteDC(backup);release(dd,dc);return;}
        HGDIOBJ previous=SelectObject(backup,saved);int sx=origin_x+24,sy=origin_y+90;
        /* 临时绘制区域先保存；填上同色底，再让原接口绘出原素材，随后完整还原。
         * 缓存只在首次出现时生成，后续每帧仅StretchBlt缩小，不修改游戏素材。 */
        BOOL copied=BitBlt(backup,0,0,sw,sh,dc,sx,sy,SRCCOPY);
        HBRUSH brush=CreateSolidBrush(color);RECT r={sx,sy,sx+sw,sy+sh};if(copied && brush)FillRect(dc,&r,brush);if(brush)DeleteObject(brush);
        release(dd,dc);dc=NULL;
        int16_t offsets[4]={0};RuntimeWin32_Read((unsigned long)(uintptr_t)frame+0xE,offsets,sizeof offsets);
        if(copied)((IconDraw)backend.icon_draw)(icons,(int)context,icon,selector,sx-offsets[0]+offsets[2],sy-offsets[1]+offsets[3],0,-1,0,0);
        if(get(dd,&dc)!=S_OK || !dc){SelectObject(backup,previous);DeleteObject(saved);DeleteObject(glyph);DeleteDC(work);DeleteDC(backup);return;}
        HGDIOBJ old=SelectObject(work,glyph);BOOL ready_image=copied && BitBlt(work,0,0,sw,sh,dc,sx,sy,SRCCOPY);
        if(copied)BitBlt(dc,sx,sy,sw,sh,backup,0,0,SRCCOPY);
        SelectObject(work,old);SelectObject(backup,previous);DeleteObject(saved);
        if(!ready_image){DeleteObject(glyph);DeleteDC(work);DeleteDC(backup);release(dd,dc);return;}
        slot=cache_next++%64;if(icon_cache[slot].bitmap)DeleteObject(icon_cache[slot].bitmap);
        icon_cache[slot].image=(uintptr_t)image;icon_cache[slot].color=color;icon_cache[slot].width=sw;icon_cache[slot].height=sh;icon_cache[slot].bitmap=glyph;
    }
    HGDIOBJ old=SelectObject(work,glyph);int old_mode=SetStretchBltMode(dc,COLORONCOLOR);
    StretchBlt(dc,origin_x+target.left,origin_y+target.top,target.right-target.left,target.bottom-target.top,work,0,0,sw,sh,SRCCOPY);
    SetStretchBltMode(dc,old_mode);SelectObject(work,old);DeleteDC(work);DeleteDC(backup);release(dd,dc);
}
/* 关于页按完整信息段滚动，没有可修改设置，不能将正文当成配置条目。 */
enum {ABOUT_SECTION_COUNT=7,ABOUT_VISIBLE=4,TAB_STEP=144,TAB_WIDTH=136};
static const char *about_titles[ABOUT_SECTION_COUNT]={"插件说明","版本与构建","作者","使用与保存","技能快捷配置","调试与反馈","第三方组件"};
static void about_text(unsigned section,char *output,size_t capacity)
{
    const char *body="";
    switch(section) {
    case 0:body="为《刀剑封魔录》及外传提供手柄操作、画面适配和便利功能。";break;
    case 1:snprintf(output,capacity,"版本：%s\n构建：%s",EDSLASH_VERSION,EDSLASH_BUILD_ID);return;
    case 2:body=EDSLASH_AUTHOR;break;
    case 3:snprintf(output,capacity,"LB/RB切页，方向键浏览；%s确认、%s取消。\n修改后按START保存，重启项在下次启动时生效。",menu_swap ? "B":"A",menu_swap ? "A":"B");return;
    case 4:body="本体与外传分别保存，每个职业共用一组14个快捷位置；同职业的不同存档共用这组设置。";break;
    case 5:body="遇到问题时开启插件日志。提供EDSlash.log和本页的构建编号，便于确认正在使用的版本。";break;
    case 6:body="使用官方SDL 3.4.16。随发行提供第三方许可说明；插件不包含游戏文件。";break;
    }
    snprintf(output,capacity,"%s",body);
}
static void cancel(void)
{
    if(error_modal){error_modal=0;return;}
    if(confirm_reset){confirm_reset=0;return;}
    if(confirm_discard){confirm_discard=0;return;}
    if(picker){picker=0;return;}
    if(editing){model.draft.values[edit_id]=edit_before;strcpy(model.draft.aspect_ratio,edit_text_before);editing=0;return;}
    if(SettingsModel_Dirty(&model)){confirm_discard=1;return;}
    SettingsWindow_Close();
}
/* 所有保存入口共享反馈；成功后显示在说明区，不只藏在底部操作行。 */
static void save_settings(void)
{
    if(SettingsModel_Save(&model)) {
        strcpy(message,RuntimeConfig_NeedsRestart() ? "设置已保存。标有待重启的项目会在下次启动游戏时生效。":"设置已保存。修改会在对应操作安全结束后生效。");
        editing=0;model.help=1;
    } else {snprintf(message,sizeof message,"%s",RuntimeConfig_Error());error_modal=1;}
}
static void reset_item(void)
{
    SettingsModel_ResetItem(&model,model.focus[model.page]);
    strcpy(message,"当前项目已恢复默认值。按START保存后生效。");
}
static void activate(void)
{
    /* 确认由内到外处理：关闭询问、技能候选、底部按钮、当前设置项。
     * 所有编辑都先改草稿，只有“保存并应用”才能写配置文件。 */
    if(confirm_reset){SettingsModel_ResetPage(&model);confirm_reset=0;strcpy(message,"本页已恢复默认设置。按START保存后生效。");model.help=1;return;}
    if(confirm_discard){SettingsModel_Discard(&model);SettingsWindow_Close();return;}
    if(error_modal){error_modal=0;return;}
    if(picker) {
        unsigned slot=model.focus[2]+1;
        if(!pick_focus)SettingsModel_SetBinding(&model,slot,(ConfigBinding){0,0,0});
        else if(pick_focus>0 && (unsigned)pick_focus<skill_view_count)
            SettingsModel_SetBinding(&model,slot,(ConfigBinding){1,skills[skill_view[pick_focus]].selector,1});
        else return;
        picker=0;return;
    }
    if(footer) {
        if(footer==2){cancel();return;}
        if(footer==3){if(model.page!=SETTINGS_PAGE_ABOUT)confirm_reset=1;return;}
        save_settings();
        return;
    }
    if(model.page==SETTINGS_PAGE_ABOUT)return;
    message[0]=0;
    if(model.page==2){if(!model.role){strcpy(message,"请载入角色后设置技能快捷键。");model.help=1;return;}picker=1;pick_focus=pick_scroll=picker_footer=0;model.help=0;
        ConfigBinding selected=effective_binding(model.focus[2]);picker_rebuild(selected.custom ? selected.selector:-1);
        return;}
    ConfigId id=SettingsModel_Field(model.page,model.focus[model.page]);const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);
    if(!f)return;
    if(f->type==CONFIG_BOOL)SettingsModel_SetInt(&model,id,!model.draft.values[id]);
    else if(editing)editing=0;
    else {edit_id=id;edit_before=model.draft.values[id];strcpy(edit_text_before,model.draft.aspect_ratio);editing=1;}
}
static void move(int dir)
{
    /* 相同方向在不同层有明确含义：技能候选逐项，数值编辑调值，普通列表按双列走。
     * 列表到最底部再向下才进入保存按钮，不在视觉上下边界斜跳到另一列。 */
    if(confirm_discard || confirm_reset || error_modal)return;
    if(dir)message[0]=0;
    if(picker) {
        if(dir==1 && pick_focus)--pick_focus;
        if(dir==2){if((unsigned)(pick_focus+1)<skill_view_count)++pick_focus;}
        if(pick_focus<pick_scroll)pick_scroll=pick_focus;
        if(pick_focus-pick_scroll>=5)pick_scroll=pick_focus-4;
        return;
    }
    if(editing) {
        ConfigId id=SettingsModel_Field(model.page,model.focus[model.page]);const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);
        if(!f)return;
        if(f->type==CONFIG_TEXT) {
            const char *ratios[]={"auto","4:3","16:9","16:10","21:9"};unsigned index=0;
            for(unsigned i=0;i<5;++i)if(!strcmp(model.draft.aspect_ratio,ratios[i]))index=i;
            SettingsModel_SetText(&model,ratios[(index+(dir==1 || dir==3 ? 4u:1u))%5]);
        } else {
            int step=f->type==CONFIG_INT || f->type==CONFIG_PERCENT ? (edit_id==CONFIG_PICKUP_MODE ? 1:dir<=2 ? 10:1):1;int64_t value=model.draft.values[id];
            value+=(dir==1 || dir==4 ? step:-step);
            if(value<f->minimum)value=f->minimum;
            if(value>f->maximum)value=f->maximum;
            SettingsModel_SetInt(&model,id,(int)value);
        }
        return;
    }
    if(footer) {
        if(dir==1){footer=0;return;}
        if(dir==3 && footer>1)--footer;
        if(dir==4 && footer<(model.page==SETTINGS_PAGE_ABOUT ? 2:3))++footer;
        return;
    }
    if(model.page==SETTINGS_PAGE_ABOUT) {
        unsigned *top=&model.scroll[SETTINGS_PAGE_ABOUT];
        if(dir==1 && *top)--*top;
        if(dir==2){if(*top<ABOUT_SECTION_COUNT-ABOUT_VISIBLE)++*top;else footer=1;}
        return;
    }
    unsigned before=model.focus[model.page];SettingsModel_Move(&model,dir,6);
    if(dir==2 && before==model.focus[model.page])footer=1;
}
static void repeat_direction(int dir,uint32_t now,int *old_dir,uint32_t *started,uint32_t *next)
{
    int fresh=dir!=*old_dir;
    if(fresh)*started=now;
    if(dir && (fresh || (int32_t)(now-(*next))>=0)) {
        int numeric=editing && edit_id!=CONFIG_PICKUP_MODE && (RuntimeConfig_Descriptor(edit_id)->type==CONFIG_INT || RuntimeConfig_Descriptor(edit_id)->type==CONFIG_PERCENT);
        int percent=numeric && RuntimeConfig_Descriptor(edit_id)->type==CONFIG_PERCENT;
        unsigned elapsed=now-(*started),interval=110;
        if(numeric && elapsed>=2000)interval=percent ? 4u:35u;
        else if(percent)interval=60;
        /* 慢帧最多补8次，不提高步长；百分比因此可比普通数值更快，且不会无限追赶。 */
        unsigned repeats=1;
        if(!fresh && numeric && elapsed>=2000)repeats=1+(now-(*next))/interval;
        if(repeats>8)repeats=8;
        for(unsigned i=0;i<repeats;++i)move(dir);
        *next=now+(fresh ? 350u:interval);
    }
    *old_dir=dir;
}
int SettingsWindow_Pad(uint32_t held,uint32_t pressed,int lt,int rt,float lx,float ly,float rx,float ry,uint32_t now)
{
    /* 开窗组合只消费Back的新按下；持续按住不能反复开关。
     * barrier等待打开时的输入全部松开，避免同一组合同时修改第一项。 */
    /* 切到其它程序时保留原暂停，不让后台手柄输入修改设置或关闭窗口。 */
    if(active && !foreground()){barrier=1;return 1;}
    if(!active) {
        if(lt && rt && (pressed&(1u<<4)) && !(held&(1u<<6)))open_window();
        return active;
    }
    if(pressed || fabsf(lx)>0.55f || fabsf(ly)>0.55f)pointer_mode=0;
    if(barrier) {if(!held && !lt && !rt && lx==0 && ly==0 && rx==0 && ry==0)barrier=0;return 1;}
    if(error_modal){if(pressed&3u)error_modal=0;return 1;}
    if(confirm_discard || confirm_reset){if(pressed&(1u<<(menu_swap ? 1:0)))activate();
        else if(pressed&(1u<<(menu_swap ? 0:1)))cancel();
        return 1;}
    /* 已有模态确认优先，普通设置页BACK重置、START保存；不占用战斗键。 */
    if(pressed&(1u<<6)){if(picker)activate();save_settings();return 1;}
    if(pressed&(1u<<4)) {
        if(model.page==SETTINGS_PAGE_ABOUT)return 1;
        if(editing || picker){reset_item();if(picker){picker=0;model.help=1;}}
        else confirm_reset=1;
        return 1;
    }
    if(pressed&(1u<<9)){editing=picker=footer=0;message[0]=0;SettingsModel_Page(&model,-1);}
    if(pressed&(1u<<10)){editing=picker=footer=0;message[0]=0;SettingsModel_Page(&model,1);}
    if(!picker && model.page!=SETTINGS_PAGE_ABOUT && (pressed&(1u<<3)))model.help=!model.help;
    if(pressed&(1u<<(menu_swap ? 1:0)))activate();
    if(pressed&(1u<<(menu_swap ? 0:1)))cancel();
    int dir=held&(1u<<11) ? 1:held&(1u<<12) ? 2:held&(1u<<13) ? 3:held&(1u<<14) ? 4:0;
    if(!dir && (fabsf(lx)>0.55f || fabsf(ly)>0.55f))dir=fabsf(ly)>=fabsf(lx) ? (ly<0 ? 1:2):(lx<0 ? 3:4);
    repeat_direction(dir,now,&previous_direction,&hold_since,&repeat_at);
    return 1;
}
static void text(HDC dc,int x,int y,int width,int height,const char *s,COLORREF color)
{
    WCHAR wide[2048];if(!MultiByteToWideChar(CP_UTF8,0,s,-1,wide,2048))return;
    RECT r={x,y,x+width,y+height};SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,wide,-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
}
static void box(HDC dc,int x,int y,int w,int h,COLORREF fill,COLORREF edge)
{
    /* 窗口只有固定的少量颜色，缓存画刷，避免每一帧反复创建几十个GDI对象。 */
    COLORREF colors[2]={fill,edge};HBRUSH selected[2]={0};
    for(unsigned n=0;n<2;++n) {
        for(unsigned i=0;i<brush_count;++i)if(brushes[i].color==colors[n])selected[n]=brushes[i].brush;
        if(!selected[n] && brush_count<32) {
            HBRUSH made=CreateSolidBrush(colors[n]);
            if(made){brushes[brush_count].color=colors[n];brushes[brush_count++].brush=made;selected[n]=made;}
        }
        if(!selected[n])selected[n]=(HBRUSH)GetStockObject(BLACK_BRUSH);
    }
    RECT r={x,y,x+w,y+h};FillRect(dc,&r,selected[0]);FrameRect(dc,&r,selected[1]);
}
/* 弹窗和底部操作共用按钮外观；文字靠左，键位靠右，两部分互不抢空间。 */
static void action_button(HDC dc,int x,int y,int w,int h,const char *label,const char *key,int focused)
{
    box(dc,x,y,w,h,RGB(45,36,24),focused ? RGB(204,69,36):RGB(164,124,59));
    text(dc,x+8,y+4,w-64,h-8,label,RGB(236,218,178));
    WCHAR wide[40];if(!MultiByteToWideChar(CP_UTF8,0,key,-1,wide,40))return;
    RECT r={x+w-60,y+4,x+w-8,y+h-4};SetTextColor(dc,RGB(236,218,178));SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,wide,-1,&r,DT_RIGHT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
}
static void value_text(ConfigId id,char *out,size_t cap)
{
    const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);int v=model.draft.values[id];
    if(id==CONFIG_GUARD_MODE)snprintf(out,cap,"%s",v ? "按最大体力比例":"原版数值");
    else if(id==CONFIG_RECOVERY_MODE)snprintf(out,cap,"%s",v ? "按最大体力比例":"与防御消耗一致");
    else if(id==CONFIG_COMBO_SWITCH)snprintf(out,cap,"%s",v ? "LT＋方向键":"LT＋Y/B/A/X");
    else if(id==CONFIG_PICKUP_MODE){const char *names[]={"关闭自动拾取","只拾取钱","钱和恢复道具","再加宝石护身石","全部物品"};snprintf(out,cap,"%s",names[v]);}
    else if(f->type==CONFIG_BOOL)snprintf(out,cap,"%s",v ? "开启":"关闭");
    else if(f->type==CONFIG_TEXT)snprintf(out,cap,"%s",model.draft.aspect_ratio);
    else if(f->type==CONFIG_PERCENT)snprintf(out,cap,"%d.%02d%%",v/100,v%100);
    else if(f->type==CONFIG_CHOICE) {
        const char *p=f->choices;for(int i=0;i<v && p;++i){p=strchr(p,'|');if(p)++p;}
        const char *end=p ? strchr(p,'|'):NULL;snprintf(out,cap,"%.*s",(end ? (int)(end-p):p ? (int)strlen(p):0),p ? p:"");
        /* 文件中的枚举仍保留小写，界面按实际手柄键位大写显示，不改变保存格式。 */
        if(id>=CONFIG_WORLD_INTERACT && id<=CONFIG_WORLD_SYSTEM)
            for(unsigned i=0;out[i];++i)if(out[i]>='a' && out[i]<='z')out[i]=(char)(out[i]-'a'+'A');
    } else snprintf(out,cap,"%d",v);
}
static RECT help_rectangle(void)
{
    /* 主列表说明放在右下；当前项目落在这片区域时移动到对角的左上。
     * 说明宽度不超过一列，左上说明不会再次盖住右列的当前项目。 */
    if(picker)return (RECT){328,122,584,362};
    RECT result={316,194,592,370};unsigned i=model.focus[model.page],start=model.scroll[model.page]*2;
    RECT target={16+(int)(i%2)*288,82+(int)((i-start)/2)*48,0,0};target.right=target.left+280;target.bottom=target.top+42;
    RECT overlap;if(IntersectRect(&overlap,&result,&target))result=(RECT){16,82,292,258};
    return result;
}
static void scroll_metrics(unsigned *total,unsigned *visible,unsigned *top,RECT *track)
{
    if(picker){*total=skill_view_count;*visible=5;*top=(unsigned)pick_scroll;*track=(RECT){308,122,316,362};}
    else if(model.page==SETTINGS_PAGE_ABOUT){*total=ABOUT_SECTION_COUNT;*visible=ABOUT_VISIBLE;*top=model.scroll[model.page];*track=(RECT){596,82,604,370};}
    else {*total=(SettingsModel_Count(model.page)+1)/2;*visible=6;*top=model.scroll[model.page];*track=(RECT){596,82,604,370};}
}
static RECT scroll_thumb(void)
{
    unsigned total,visible,top;RECT track;scroll_metrics(&total,&visible,&top,&track);
    int height=track.bottom-track.top,thumb=total>visible ? (int)(height*visible/total):height;
    if(thumb<18)thumb=18;
    int offset=total>visible ? (int)(top*(unsigned)(height-thumb)/(total-visible)):0;
    return (RECT){track.left,track.top+offset,track.right,track.top+offset+thumb};
}
static void scroll_at(int y)
{
    unsigned total,visible,top;RECT track;scroll_metrics(&total,&visible,&top,&track);
    if(total<=visible)return;
    RECT thumb=scroll_thumb();int length=(track.bottom-track.top)-(thumb.bottom-thumb.top);
    int position=y-track.top-(thumb.bottom-thumb.top)/2;
    if(position<0)position=0;
    if(position>length)position=length;
    top=(unsigned)MulDiv(position,(int)(total-visible),length);
    /* 拖动时焦点保持在可见列表内，避免按确认修改已经滚出画面的项目。 */
    if(picker){pick_scroll=(int)top;picker_footer=0;if(pick_focus<(int)top || pick_focus>=(int)(top+visible))pick_focus=(int)top;}
    else if(model.page==SETTINGS_PAGE_ABOUT){model.scroll[model.page]=top;footer=0;}
    else {model.scroll[model.page]=top;unsigned *focus=&model.focus[model.page];
        if(*focus/2<top || *focus/2>=top+visible)*focus=top*2+(*focus%2);
        footer=0;}
}
static void paint_scrollbar(HDC dc,int x,int y)
{
    unsigned total,visible,top;RECT track;scroll_metrics(&total,&visible,&top,&track);RECT thumb=scroll_thumb();
    box(dc,x+track.left,y+track.top,track.right-track.left,track.bottom-track.top,RGB(45,36,24),RGB(96,75,43));
    box(dc,x+thumb.left,y+thumb.top,thumb.right-thumb.left,thumb.bottom-thumb.top,RGB(194,146,65),RGB(218,168,80));
}
static void paint_native_entry(unsigned long context)
{
    void *page=(void *)(uintptr_t)((This1)backend.get_jm)((void *)backend.ui,0xAA);RuntimeFocusRect r;
    if(!SettingsWindow_NativeEntryRect(page,&r))return;
    void *surface=(void *)(uintptr_t)context,*dd=ptr(surface,0x2D),*table=ptr(dd,0);
    if(!readable(table,0x6C))return;
    logical_width=(int)rd(surface,0xC);logical_height=(int)rd(surface,0x10);
    HDC dc=NULL;SurfaceGetDC get=(SurfaceGetDC)(uintptr_t)rd(table,0x44);SurfaceReleaseDC release=(SurfaceReleaseDC)(uintptr_t)rd(table,0x68);
    if(get(dd,&dc)!=S_OK || !dc)return;
    box(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,RGB(12,12,12),RGB(70,59,40));release(dd,dc);
    static char caption[48];if(!caption[0])WideCharToMultiByte(936,0,L"EDSlash设置",-1,caption,sizeof caption,NULL,NULL);
    typedef int (__attribute__((thiscall)) *NativeText)(void *,unsigned long,const char *,int,int,int);
    /* 原文字接口以传入x为文字中心；底板和文字必须使用相同中心。
     * 复制只读绘制属性，不暂时改原AA页的字体颜色，避免影响其它文字。 */
    BYTE style[0xC0];memcpy(style,page,sizeof style);
    unsigned color=(native_entry_hover || native_entry_focus==page) ? RGB(255,255,0):rd(page,0x60);
    memcpy(style+0x60,&color,4);
    /* 当前原字体单行占12个逻辑像素；从按钮高度减去文字高度，两边平分留白。 */
    const int text_height=12;
    int text_y=r.top+(r.bottom-r.top-text_height)/2;
    ((NativeText)backend.menu_native_text_draw)(style,context,caption,(r.left+r.right)/2,text_y,0);
}
static void paint(RuntimeEventId event,void *subject,unsigned long context,unsigned long value,void *user)
{
    /* 借用原游戏绘制上下文的DirectDraw表面，DC只在本次绘制中持有。
     * 面板保持608×448，较大的表面只改变居中原点，不改变逻辑控件大小。 */
    (void)subject;(void)value;(void)user;
    if(event!=RUNTIME_EVENT_UI_DRAW_END || !context)return;
    if(!active){paint_native_entry(context);return;}
    if((!native_host && !rd(ptr((void *)backend.world_global,0),0x58)) || !rd(root,0x64)){SettingsWindow_Close();return;}
    void *surface=(void *)(uintptr_t)context,*dd=ptr(surface,0x2D),*table=ptr(dd,0);
    if(!readable(surface,0x31) || !readable(table,0x6C))return;
    typedef HRESULT (__stdcall *GetDCFn)(void *,HDC *);typedef HRESULT (__stdcall *ReleaseDCFn)(void *,HDC);
    HDC dc=NULL;if(((GetDCFn)(uintptr_t)rd(table,0x44))(dd,&dc)!=S_OK || !dc)return;
    int width=(int)rd(surface,0xC),height=(int)rd(surface,0x10);
    if(width<640 || height<480){((ReleaseDCFn)(uintptr_t)rd(table,0x68))(dd,dc);return;}
    logical_width=width;logical_height=height;origin_x=(width-608)/2;origin_y=(height-448)/2;
    int saved_dc=SaveDC(dc); /* 字体、文字色和背景模式都还给原游戏，不污染后续绘制。 */
    if(!font)font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"宋体");
    HGDIOBJ previous=font ? SelectObject(dc,font):NULL;
    int x=origin_x,y=origin_y;box(dc,x,y,608,448,RGB(20,18,15),RGB(164,124,59));
    text(dc,x+16,y+10,450,24,"EDSlash 模组设置",RGB(231,206,154));
    const char *pages[]={"模组设置","按键设置","技能快捷","关于"};
    for(unsigned i=0;i<SETTINGS_PAGE_COUNT;++i){box(dc,x+16+(int)i*TAB_STEP,y+38,TAB_WIDTH,30,RGB(40,32,23),i==model.page ? RGB(218,168,80):RGB(104,78,38));text(dc,x+24+(int)i*TAB_STEP,y+44,TAB_WIDTH-16,20,pages[i],RGB(226,210,174));}
    unsigned start=model.scroll[model.page]*2,count=SettingsModel_Count(model.page);
    for(unsigned i=start;i<count && i<start+12;++i) {
        int cx=x+16+(int)(i%2)*288,cy=y+82+(int)((i-start)/2)*48;int focused=!footer && model.focus[model.page]==i && (model.page!=2 || model.role);
        box(dc,cx,cy,280,42,RGB(31,27,21),focused ? RGB(204,69,36):RGB(96,75,43));
        char label[96],value_text_buffer[96];int dirty=0;
        if(model.page==2) {
            static const char *names[]={"A","B","X","Y","上","下","左","右","LB","RB","Back","Start","L3","R3"};
            snprintf(label,sizeof label,"RT + %s",names[i]);ConfigBinding b=model.draft_bindings[i];
            strcpy(value_text_buffer,model.role ? "未设置":"载入角色后设置");ConfigBinding shown=effective_binding(i);
            if(shown.custom)for(unsigned n=0;n<skill_count;++n)if(skills[n].selector==shown.selector)snprintf(value_text_buffer,sizeof value_text_buffer,"%s",skills[n].name);
            dirty=memcmp(&b,&model.saved_bindings[i],sizeof b)!=0;
        } else {
            ConfigId id=SettingsModel_Field(model.page,i);const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);
            const ConfigSnapshot *actual=RuntimeConfig_Current();
            int pending=f->type==CONFIG_TEXT ? strcmp(model.saved.aspect_ratio,actual->aspect_ratio)!=0:model.saved.values[id]!=actual->values[id];
            const char *state=pending ? (f->apply==CONFIG_APPLY_RESTART ? " [待重启]":f->apply==CONFIG_APPLY_IDLE ? " [待释放]":" [待应用]"):
                f->apply==CONFIG_APPLY_RESTART ? " [重启]":"";
            snprintf(label,sizeof label,"%s%s",f->label,state);value_text(id,value_text_buffer,sizeof value_text_buffer);
            dirty=f->type==CONFIG_TEXT ? strcmp(model.draft.aspect_ratio,model.saved.aspect_ratio)!=0:model.draft.values[id]!=model.saved.values[id];
        }
        text(dc,cx+(model.page==2 ? 54:8),cy+3,model.page==2 ? 218:264,18,label,RGB(222,205,172));text(dc,cx+(model.page==2 ? 54:8),cy+21,model.page==2 ? 218:264,18,value_text_buffer,model.page==2 && !model.role ? RGB(112,112,112):dirty ? RGB(255,178,76):RGB(154,198,149));
    }
    if(model.page==SETTINGS_PAGE_ABOUT) {
        if(!help_font)help_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"宋体");
        HGDIOBJ prior=help_font ? SelectObject(dc,help_font):NULL;
        unsigned top=model.scroll[SETTINGS_PAGE_ABOUT];
        for(unsigned i=top;i<ABOUT_SECTION_COUNT && i<top+ABOUT_VISIBLE;++i) {
            int row_y=y+82+(int)(i-top)*68;char body[1024];about_text(i,body,sizeof body);
            box(dc,x+16,row_y,576,64,RGB(28,25,19),RGB(104,78,38));
            text(dc,x+24,row_y+4,560,18,about_titles[i],RGB(231,206,154));
            text(dc,x+24,row_y+23,560,39,body,RGB(222,205,172));
        }
        if(prior)SelectObject(dc,prior);
        action_button(dc,x+16,y+378,280,30,"保存","START",footer==1);
        action_button(dc,x+304,y+378,288,30,"关闭",menu_swap ? "A":"B",footer==2);
    } else {
        action_button(dc,x+16,y+378,136,30,"保存","START",footer==1);
        action_button(dc,x+160,y+378,136,30,"关闭",menu_swap ? "A":"B",footer==2);
        action_button(dc,x+304,y+378,136,30,editing ? "当前默认":"本页默认","BACK",footer==3);
        if(!picker)action_button(dc,x+448,y+378,144,30,model.help ? "隐藏说明":"显示说明","Y",0);
    }
    const char *hint=model.page==SETTINGS_PAGE_ABOUT ? "LB/RB切页，上下或滚轮浏览；START保存，取消键关闭":editing ? "左右×1，上下×10，长按加速；BACK默认，START保存":"LB/RB分类，BACK本页默认，START保存，Y说明";
    text(dc,x+16,y+416,576,26,message[0] ? message:hint,RGB(207,188,154));
    paint_scrollbar(dc,x,y);
    if(picker) {
        box(dc,x+16,y+82,576,322,RGB(24,22,18),RGB(194,146,65));
        text(dc,x+24,y+90,280,24,"选择已学会的技能",RGB(228,206,167));
        text(dc,x+328,y+90,256,24,"技能说明",RGB(228,206,167));
        for(unsigned n=(unsigned)pick_scroll;n<skill_view_count && n<(unsigned)pick_scroll+5;++n) {
            int sy=y+122+(int)(n-(unsigned)pick_scroll)*48;
            box(dc,x+24,sy,280,48,RGB(37,32,25),!picker_footer && n==(unsigned)pick_focus ? RGB(204,69,36):RGB(88,71,42));
            text(dc,x+80,sy+14,216,28,n ? skills[skill_view[n]].name:"未设置",RGB(225,207,177));
        }
        paint_scrollbar(dc,x,y);
    }
    if(editing) {
        const ConfigDescriptor *f=RuntimeConfig_Descriptor(edit_id);char current[128];value_text(edit_id,current,sizeof current);
        box(dc,x+16,y+82,576,288,RGB(24,22,18),RGB(194,146,65));
        text(dc,x+24,y+90,552,26,f->label,RGB(236,218,178));
        text(dc,x+24,y+132,280,24,"当前选择／数值：",RGB(222,205,172));
        text(dc,x+24,y+160,280,30,current,RGB(255,178,76));
        int options=f->type==CONFIG_CHOICE || f->type==CONFIG_TEXT || edit_id==CONFIG_PICKUP_MODE;
        action_button(dc,x+24,y+202,128,36,options ? "上一项":"减小数值","←",0);
        action_button(dc,x+164,y+202,140,36,options ? "下一项":"增大数值","→",0);
        action_button(dc,x+24,y+306,128,36,"完成",menu_swap ? "B":"A",0);
        action_button(dc,x+164,y+306,140,36,"取消",menu_swap ? "A":"B",0);
        if(!model.help)text(dc,x+328,y+138,256,190,"左右调整最小单位，上下调整十倍。按住方向两秒后加快，百分比加速更快。\n\n完成只保留这次修改；返回主列表后，选择“保存并应用”才会保存。取消会恢复打开这个调整窗口前的值。\n\n按Y可查看本项的详细说明。",RGB(207,188,154));
    }
    if((picker || (model.help && model.page!=SETTINGS_PAGE_ABOUT)) && !confirm_discard && !confirm_reset && !error_modal) {
        const char *description;
        if(picker)description=pick_focus>0 && (unsigned)pick_focus<skill_view_count ? skills[skill_view[pick_focus]].description:"未设置任何技能。选择并保存后，按这个RT组合键不会发动技能。";
        else description=model.page==2 ? "先选择一个RT组合键位置并确认，再从已学会的技能中选择。保存后，按住RT并按这个组合键就会直接发动技能，不需要再按鼠标右键。每个角色分别保存。":RuntimeConfig_Descriptor(SettingsModel_Field(model.page,model.focus[model.page]))->description;
        if(message[0] && !picker)description=message;
        RECT h=editing ? (RECT){328,122,584,362}:help_rectangle();
        box(dc,x+h.left,y+h.top,h.right-h.left,h.bottom-h.top,RGB(28,25,19),RGB(191,145,68));
        if(!help_font)help_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"宋体");
        HGDIOBJ prior=help_font ? SelectObject(dc,help_font):NULL;
        text(dc,x+h.left+8,y+h.top+8,h.right-h.left-16,h.bottom-h.top-16,description,RGB(236,218,178));
        if(prior)SelectObject(dc,prior);
    }
    if(confirm_discard || confirm_reset) {
        box(dc,x+96,y+160,416,116,RGB(26,23,18),RGB(208,151,67));
        text(dc,x+112,y+176,384,40,confirm_reset ? "将本页恢复默认？其它页面不变，保存后生效。":"有未保存的修改。丢弃并关闭，或继续编辑。",RGB(238,218,177));
        action_button(dc,x+112,y+232,176,28,confirm_reset ? "恢复默认":"丢弃并关闭",menu_swap ? "B":"A",0);
        action_button(dc,x+304,y+232,192,28,"继续编辑",menu_swap ? "A":"B",0);
    }
    if(error_modal) {
        box(dc,x+96,y+138,416,190,RGB(26,23,18),RGB(208,151,67));
        text(dc,x+112,y+152,384,24,"无法保存设置",RGB(255,178,76));
        text(dc,x+112,y+188,384,84,message,RGB(238,218,177));
        box(dc,x+208,y+286,192,28,RGB(45,36,24),RGB(164,124,59));
        text(dc,x+216,y+290,176,24,"返回修改 (A/B)",RGB(238,218,177));
    }
    if(previous)SelectObject(dc,previous);
    if(saved_dc)RestoreDC(dc,saved_dc);
    ((ReleaseDCFn)(uintptr_t)rd(table,0x68))(dd,dc);
    if(!confirm_discard && !confirm_reset && !error_modal && !editing && model.page==2 && model.role) {
        /* DC释放后补原图标；避开说明框，不能把后画的图标盖到说明文字上。 */
        void *icons=ptr((void *)backend.settings_icon_global,0),*hud=ptr((void *)backend.skill_global,0);
        unsigned icon_count=rd(icons,0x44);void *sprites=ptr(icons,0x48);
        if(readable(icons,0x4C) && icon_count && icon_count<=4096 && sprites) {
            unsigned begin=picker ? (unsigned)pick_scroll:start,limit=picker ? skill_view_count:count;
            unsigned end=begin+(picker ? 5u:12u);if(end>limit)end=limit;
            for(unsigned i=begin;i<end;++i) {
                if(picker && i==0)continue;
                ConfigBinding shown=picker ? (ConfigBinding){0,0,0}:effective_binding(i);
                if(!picker && !shown.custom)continue;
                int selection=picker ? skills[skill_view[i]].selector:shown.selector;
                int icon=picker ? skills[skill_view[i]].icon:((This1)backend.icon_resolve)(hud,selection);
                int dx=picker ? 26:16+(int)(i%2)*288,dy=picker ? 122+(int)(i-begin)*48:82+(int)((i-start)/2)*48;
                RECT glyph={dx,dy,dx+48,dy+48},overlap,h=help_rectangle();
                if((picker || model.help) && IntersectRect(&overlap,&glyph,&h))continue;
                if(icon>=0 && (unsigned)icon<icon_count && readable((BYTE *)sprites+(unsigned)icon*32u,32))
                    scaled_icon(context,icons,(BYTE *)sprites+(unsigned)icon*32u,icon,selection,picker ? 24:16+(int)(i%2)*288,dy,picker ? 48:42,picker ? 38:32,picker ? RGB(37,32,25):RGB(31,27,21));
            }
        }
    }
}
int SettingsWindow_Wheel(int delta)
{if(!active)return 0;pointer_mode=1;move(delta>0 ? 1:2);return 1;}
static void mouse_click(int x,int y)
{
    /* 坐标已换算为面板坐标。先处理最上层，任何模态都不把点击漏给背后的设置。 */
    if(error_modal){if(x>=208 && x<400 && y>=286 && y<314)error_modal=0;return;}
    if(confirm_discard || confirm_reset){if(y>=232 && y<260){if(x>=112 && x<288)activate();else if(x>=304 && x<496)cancel();}return;}
    if(editing) {
        if(y>=202 && y<238){if(x>=24 && x<152)move(3);else if(x>=164 && x<304)move(4);}
        if(y>=306 && y<342){if(x>=24 && x<152)activate();else if(x>=164 && x<304)cancel();}
        if(y>=378 && y<408){if(x>=448 && x<592)model.help=!model.help;else if(x>=304 && x<440)reset_item();else if(x>=16 && x<152)save_settings();}
        return;
    }
    unsigned total,visible,top;RECT track;scroll_metrics(&total,&visible,&top,&track);
    if(x>=track.left && x<track.right && y>=track.top && y<track.bottom){scroll_at(y);scroll_drag=1;return;}
    if(picker) {
        if(x>=24 && x<304 && y>=122 && y<362){unsigned row=(unsigned)(y-122)/48+(unsigned)pick_scroll;
            if(row<skill_view_count){picker_footer=0;pick_focus=(int)row;activate();}}
                return;
    }
    if(y>=38 && y<68 && x>=16 && x<592){unsigned tab=(unsigned)((x-16)/TAB_STEP);if((x-16)%TAB_STEP<TAB_WIDTH){model.page=tab;footer=0;}return;}
    if(model.page==SETTINGS_PAGE_ABOUT) {
        if(y>=378 && y<408){if(x>=16 && x<296)footer=1;else if(x>=304 && x<592)footer=2;else return;activate();}
        return;
    }
    if(y>=82 && y<370 && x>=16 && x<592) {
        unsigned item=model.scroll[model.page]*2+(unsigned)((y-82)/48)*2+(unsigned)((x-16)/288);
        if(item<SettingsModel_Count(model.page)){model.focus[model.page]=item;footer=0;activate();}return;
    }
    if(y>=378 && y<408) {
        if(x>=448 && x<592)model.help=!model.help;
        else {if(x>=16 && x<152)footer=1;else if(x>=160 && x<296)footer=2;else if(x>=304 && x<440)footer=3;else return;activate();}
    }
}
static void mouse_hover(int x,int y)
{
    /* 只在物理指针移动时更新焦点，静止的鼠标不能抢走手柄正在浏览的项目。 */
    if(editing || confirm_discard || confirm_reset || error_modal || scroll_drag)return;
    if(model.page==SETTINGS_PAGE_ABOUT)return;
    if(picker) {
        if(x>=24 && x<304 && y>=122 && y<362) {
            unsigned row=(unsigned)(y-122)/48+(unsigned)pick_scroll;
            if(row<skill_view_count){picker_footer=0;pick_focus=(int)row;}
        }
        return;
    }
    if(x>=16 && x<592 && y>=82 && y<370) {
        unsigned item=model.scroll[model.page]*2+(unsigned)((y-82)/48)*2+(unsigned)((x-16)/288);
        if(item<SettingsModel_Count(model.page)){model.focus[model.page]=item;footer=0;}
    }
}
static void keyboard(RuntimeEventId event,void *subject,unsigned long result,unsigned long value,void *user)
{
    /* 这里只读真实Windows输入，不读取被Controller包装后用于驱动角色的虚拟键状态。
     * 鼠标坐标先从屏幕转到客户区，再换算到游戏表面，和绘制用同一个面板原点。 */
    (void)subject;(void)result;(void)value;(void)user;if(event!=RUNTIME_EVENT_INPUT_FRAME_END || !ready)return;
    if(!foreground()){if(active)barrier=1;return;}
    static unsigned old_keys;
    int keys[]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_RETURN,'Y'};unsigned now_keys=0;
    for(unsigned i=0;i<6;++i)if(GetAsyncKeyState(keys[i])&0x8000)now_keys|=1u<<i;
    static int keyboard_direction;static uint32_t keyboard_started,keyboard_next;
    int direction=now_keys&1 ? 1:now_keys&2 ? 2:now_keys&4 ? 3:now_keys&8 ? 4:0;
    if(active){if(direction)pointer_mode=1;repeat_direction(direction,GetTickCount(),&keyboard_direction,&keyboard_started,&keyboard_next);}
    else keyboard_direction=0;
    if(active)for(unsigned i=4;i<6;++i)if((now_keys&~old_keys)&(1u<<i)){pointer_mode=1;if(i==4)activate();else if(!picker && model.page!=SETTINGS_PAGE_ABOUT)model.help=!model.help;}
    old_keys=now_keys;
    int open_key=(GetAsyncKeyState(VK_OEM_3)&0x8000)!=0,escape=(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
    if(open_key && !previous_open_key){if(active)cancel();else if(open_window())pointer_mode=1;}
    previous_open_key=open_key;
    if(active && escape && !previous_escape)cancel();
    previous_escape=escape;
    int left=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0,right=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
    if(active && right && !previous_right)cancel();
    native_entry_hover=0;
    if(!active && logical_width && logical_height) {
        POINT p;RECT client;HWND w=GetForegroundWindow();void *page=(void *)(uintptr_t)((This1)backend.get_jm)((void *)backend.ui,0xAA);RuntimeFocusRect r;
        if(SettingsWindow_NativeEntryRect(page,&r) && GetCursorPos(&p) && ScreenToClient(w,&p) && GetClientRect(w,&client) && client.right>0 && client.bottom>0) {
            p.x=MulDiv(p.x,logical_width,client.right);p.y=MulDiv(p.y,logical_height,client.bottom);
            native_entry_hover=p.x>=r.left && p.x<r.right && p.y>=r.top && p.y<r.bottom;
            if(native_entry_hover && left && !previous_left && SettingsWindow_OpenNative(page)){pointer_mode=1;previous_left=left;}
        }
    }
    if(active && left && !previous_left) {
        POINT p;GetCursorPos(&p);HWND w=GetForegroundWindow();ScreenToClient(w,&p);RECT client;
        if(!GetClientRect(w,&client) || client.right<=0 || client.bottom<=0 || !logical_width || !logical_height)return;
        p.x=MulDiv(p.x,logical_width,client.right)-origin_x;p.y=MulDiv(p.y,logical_height,client.bottom)-origin_y;
        mouse_click((int)p.x,(int)p.y);
    }
    if(active && scroll_drag && left) {
        POINT p;RECT client;HWND w=GetForegroundWindow();
        if(GetCursorPos(&p) && ScreenToClient(w,&p) && GetClientRect(w,&client) && client.bottom>0)
            scroll_at(MulDiv(p.y,logical_height,client.bottom)-origin_y);
    }
    if(!left)scroll_drag=0;
    POINT location;if(GetCursorPos(&location)) {
        int moved=location.x!=previous_pointer.x || location.y!=previous_pointer.y;
        if(moved || (left && !previous_left) || (right && !previous_right))pointer_mode=1;
        if(active && moved && logical_width && logical_height) {
            POINT p=location;RECT client;HWND w=GetForegroundWindow();
            if(ScreenToClient(w,&p) && GetClientRect(w,&client) && client.right>0 && client.bottom>0)
                mouse_hover(MulDiv(p.x,logical_width,client.right)-origin_x,MulDiv(p.y,logical_height,client.bottom)-origin_y);
        }
        previous_pointer=location;
    }
    previous_left=left;previous_right=right;
}
int SettingsWindow_Initialize(const RuntimeContext *runtime)
{
    if(ready)return 1;
    if(!runtime || !runtime->profile || runtime->profile->game_id<1 || runtime->profile->game_id>2)return 0;
    backend=settings_profiles[runtime->profile->game_id-1];game=runtime->profile->game_id;
    uintptr_t functions[]={backend.inventory_get,backend.get_jm,backend.menu_system_show,backend.menu_system_primary,backend.settings_actor_get,backend.settings_string_get,backend.ui_property,backend.lookup,backend.skill_eligibility,backend.icon_resolve,backend.icon_draw,backend.settings_skill_name,backend.settings_skill_description,backend.settings_string_destroy,backend.settings_query_skill,backend.settings_text_get,backend.focus_frame_get,backend.focus_image_get,backend.menu_settings_show,backend.menu_settings_primary,backend.menu_native_text_draw};
    for(unsigned i=0;i<21;++i){BYTE bytes[12];if(!RuntimeWin32_Read((unsigned long)functions[i],bytes,12) || memcmp(bytes,backend.signatures[i],12)) {
        RuntimeLog_Write("[模组设置] 原接口%u地址%08lX未通过签名校验，设置窗口停用。",i,(unsigned long)functions[i]);return 0;}}
    /* 订阅分别记账；后一项失败时重试也不会重复注册前一项。 */
    if(!draw_subscribed)draw_subscribed=Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_END,paint,NULL);
    if(!input_subscribed)input_subscribed=Runtime_Subscribe(RUNTIME_EVENT_INPUT_FRAME_END,keyboard,NULL);
    if(!draw_subscribed || !input_subscribed)return 0;
    ready=1;RuntimeLog_Write("[模组设置] 原接口已校验；场景中LT+RT+Back或主键盘1左侧按键打开并使用原暂停。");return 1;
}
