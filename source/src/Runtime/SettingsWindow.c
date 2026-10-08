#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "SettingsWindow.h"
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
    uintptr_t settings_skill_name,settings_skill_description,settings_string_destroy,settings_query_skill,settings_empty_string;
    unsigned active_offset,invalid_offset;BYTE signatures[15][12];
} SettingsBackend;
#include "SettingsData.h"
static SettingsBackend backend;
static SettingsModel model;
static unsigned game;
static int ready,active,editing,footer,confirm_discard,picker,pick_focus,pick_scroll,picker_style,barrier;
static int draw_subscribed,input_subscribed,menu_swap,error_modal,picker_footer,scroll_drag;
static ConfigId edit_id;static int edit_before;static char edit_text_before[40];
static void *root;static uintptr_t old_primary,old_draw;
static void paint(RuntimeEventId,void *,unsigned long,unsigned long,void *);
static HFONT font,help_font;static int origin_x,origin_y,logical_width,logical_height;
static struct {COLORREF color;HBRUSH brush;} brushes[32];static unsigned brush_count;
static uint32_t repeat_at;static int previous_direction;
static int pointer_mode;static POINT previous_pointer;
static int previous_open_key,previous_left,previous_right,previous_escape;
static char message[160];
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
    if(old_draw && rd((void *)backend.menu_system_vtable,8)==(unsigned)(uintptr_t)fallback_draw) {
        int result=RuntimeWin32_WriteCode((unsigned long)(backend.menu_system_vtable+8),&old_draw,4);
        if(result!=1 && rd((void *)backend.menu_system_vtable,8)==(unsigned)(uintptr_t)fallback_draw) {
            strcpy(message,"原绘制入口恢复失败，请再次关闭。");return;
        }
    }
    if(readable((void *)(backend.menu_system_vtable+0x24),4) &&
        rd((void *)backend.menu_system_vtable,0x24)==(unsigned)(uintptr_t)capture_primary) {
        int result=RuntimeWin32_WriteCode((unsigned long)(backend.menu_system_vtable+0x24),&old_primary,4);
        if(result!=1 && rd((void *)backend.menu_system_vtable,0x24)==(unsigned)(uintptr_t)capture_primary) {
            if(old_draw){uintptr_t hook=(uintptr_t)fallback_draw;RuntimeWin32_WriteCode((unsigned long)(backend.menu_system_vtable+8),&hook,4);}
            strcpy(message,"原输入入口恢复失败，请再次关闭。");return;
        }
    }
    old_draw=0;
    if(readable(root,0x68))((This2)backend.menu_system_show)(root,0,0);
    active=editing=picker=confirm_discard=0;root=NULL;old_primary=0;message[0]=0;
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
        /* 与原技能页悬停相同的完整说明生成函数；游戏CString由游戏自己的析构释放。
         * 复制为UTF-8以后只保留文字，不持有游戏字符串或学习记录。 */
        unsigned game_string=rd((void *)backend.settings_empty_string,0);
        if(game_string) {
            typedef void (__cdecl *Description)(void *,void *,unsigned *,int);
            ((Description)backend.settings_skill_description)(skill_player,learned_records[i],&game_string,1);
            if(!copy_game_text((const char *)(uintptr_t)game_string,skills[skill_count].description,sizeof skills[skill_count].description))
                strcpy(skills[skill_count].description,skills[skill_count].name);
            ((This0)backend.settings_string_destroy)(&game_string);
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
    /* 原绑定中的基础动作可能是特殊选择，和已学技能合并去重。 */
    void *node=ptr(ptr((void *)backend.skill_global,0),0xC18);
    for(unsigned i=0;node && i<128;++i) {
        if(!readable(node,0x20))break;
        add_skill(role,(int)rd(node,0x14),1);
        void *next=ptr(node,8);if(next==node)break;node=next;
    }
    memset(learned_records,0,sizeof learned_records);skill_player=NULL;
}
static int open_window(void)
{
    /* 窗口不能在Loader锁内打开；这里只由游戏输入线程的组合键/反引号边沿调用。
     * 原Show负责暂停和捕获，本函数不写角色动作状态，也不创建自己的暂停计数。 */
    if(!ready || active || !foreground())return 0;
    /* 上一次失败回滚若仍留着自己的包装，不能再把自己当旧入口形成递归链。 */
    if(rd((void *)backend.menu_system_vtable,0x24)==(unsigned)(uintptr_t)capture_primary ||
        rd((void *)backend.menu_system_vtable,8)==(unsigned)(uintptr_t)fallback_draw)return 0;
    void *world=ptr((void *)backend.world_global,0),*role=actor();
    if(!rd(world,0x58) || !readable(role,backend.invalid_offset+4) || rd(role,backend.invalid_offset))return 0;
    /* 原捕获中的其它菜单不被自建窗口替换；战斗/走路不作为拒绝理由，由原暂停保护。 */
    if(ptr((void *)backend.ui,0x3C))return 0;
    /* 普通菜单模块只包装Tick/Show/hover，主操作+24保留原版是正常状态。
     * 当前是否可开由场景和捕获判断，不能要求这个槽先被另一个模块改写。 */
    root=(void *)(uintptr_t)((This1)backend.get_jm)((void *)backend.ui,0x2D);
    void *data=(void *)(uintptr_t)((This0)backend.inventory_get)((void *)backend.inventory_root);
    unsigned selector=readable(data,0x34C) ? rd(data,0x348):0;
    if(!readable(root,0xC0) || rd(root,0)!=backend.menu_system_vtable || !SettingsModel_Open(&model,game,selector)){root=NULL;return 0;}
    old_primary=rd((void *)backend.menu_system_vtable,0x24);uintptr_t replacement=(uintptr_t)capture_primary;
    if(!old_primary)return 0;
    int installed=RuntimeWin32_WriteCode((unsigned long)(backend.menu_system_vtable+0x24),&replacement,4);
    if(installed!=1) {
        /* 2代表内存已写入但系统收尾失败，必须还原；不能将其冒充完全成功。 */
        if(installed==2)RuntimeWin32_WriteCode((unsigned long)(backend.menu_system_vtable+0x24),&old_primary,4);
        root=NULL;return 0;
    }
    if(!RuntimeConfig_GetInt(CONFIG_DISPLAY_ENABLED)) {
        old_draw=rd((void *)backend.menu_system_vtable,8);uintptr_t hook=(uintptr_t)fallback_draw;
        int draw_result=old_draw ? RuntimeWin32_WriteCode((unsigned long)(backend.menu_system_vtable+8),&hook,4):0;
        if(draw_result!=1) {
            if(draw_result==2)RuntimeWin32_WriteCode((unsigned long)(backend.menu_system_vtable+8),&old_draw,4);
            RuntimeWin32_WriteCode((unsigned long)(backend.menu_system_vtable+0x24),&old_primary,4);root=NULL;return 0;
        }
    }
    /* 本次窗口固定使用打开时的确认布局，保存交换AB后不在半次编辑中改变含义。 */
    menu_swap=RuntimeConfig_GetInt(CONFIG_MENU_SWAP_AB);
    active=1;barrier=1;editing=footer=picker=confirm_discard=error_modal=picker_footer=scroll_drag=0;previous_direction=0;message[0]=0;
    ((This2)backend.menu_system_show)(root,1,0);build_skills();
    RuntimeLog_Write("[模组设置] 打开并取得原系统菜单暂停/捕获，角色selector=%u。",selector);return 1;
}
static void picker_rebuild(int selector)
{
    /* 原动作菜单左手只接受类别1；右手接受普通类别0/1。
     * 切换施放侧时重建“可设置”的视图，不把不可用条目留在列表里。 */
    skill_view_count=0;pick_focus=pick_scroll=picker_footer=0;
    for(unsigned i=0;i<skill_count;++i)if(picker_style || skills[i].kind==1) {
        if(skills[i].selector==selector)pick_focus=(int)skill_view_count;
        skill_view[skill_view_count++]=i;
    }
    if(pick_focus>=5)pick_scroll=pick_focus-4;
}
static void picker_switch_style(void)
{
    int selector=skill_view_count && (unsigned)pick_focus<skill_view_count ? skills[skill_view[pick_focus]].selector:-1;
    picker_style=!picker_style;picker_rebuild(selector);
}
static void cancel(void)
{
    if(error_modal){error_modal=0;return;}
    if(confirm_discard){confirm_discard=0;return;}
    if(picker){picker=0;return;}
    if(editing){model.draft.values[edit_id]=edit_before;strcpy(model.draft.aspect_ratio,edit_text_before);editing=0;return;}
    if(SettingsModel_Dirty(&model)){confirm_discard=1;return;}
    SettingsWindow_Close();
}
static void activate(void)
{
    /* 确认由内到外处理：关闭询问、技能候选、底部按钮、当前设置项。
     * 所有编辑都先改草稿，只有“保存并应用”才能写配置文件。 */
    if(confirm_discard){SettingsModel_Discard(&model);SettingsWindow_Close();return;}
    if(error_modal){error_modal=0;return;}
    if(picker) {
        unsigned slot=model.focus[2]+1;
        if(picker_footer)SettingsModel_SetBinding(&model,slot,(ConfigBinding){0,0,0});
        else if(skill_view_count && (unsigned)pick_focus<skill_view_count)
            SettingsModel_SetBinding(&model,slot,(ConfigBinding){1,skills[skill_view[pick_focus]].selector,picker_style});
        else return;
        picker=0;return;
    }
    if(footer) {
        if(footer==2){cancel();return;}
        if(SettingsModel_Save(&model))strcpy(message,RuntimeConfig_NeedsRestart() ? "已保存；部分选项需要重启":"已保存；按各项安全边界应用");
        else {snprintf(message,sizeof message,"%s",RuntimeConfig_Error());error_modal=1;}
        return;
    }
    if(model.page==2){picker=1;pick_focus=pick_scroll=picker_footer=0;model.help=0;
        picker_style=model.draft_bindings[model.focus[2]].custom ? model.draft_bindings[model.focus[2]].right:1;
        picker_rebuild(model.draft_bindings[model.focus[2]].custom ? model.draft_bindings[model.focus[2]].selector:-1);
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
    if(confirm_discard || error_modal)return;
    if(picker) {
        if(picker_footer){if(dir==1)picker_footer=0;return;}
        if(dir==1 && pick_focus)--pick_focus;
        if(dir==2){if((unsigned)(pick_focus+1)<skill_view_count)++pick_focus;else picker_footer=1;}
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
            int step=f->step ? f->step:1;int64_t value=model.draft.values[id];
            value+=(dir==1 || dir==4 ? step:-step);
            if(value<f->minimum)value=f->minimum;
            if(value>f->maximum)value=f->maximum;
            SettingsModel_SetInt(&model,id,(int)value);
        }
        return;
    }
    if(footer) {
        if(dir==1){footer=0;return;}
        if(dir==3)footer=1;
        if(dir==4)footer=2;
        return;
    }
    unsigned before=model.focus[model.page];SettingsModel_Move(&model,dir,6);
    if(dir==2 && before==model.focus[model.page])footer=1;
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
    if(confirm_discard){if(pressed&(1u<<(menu_swap ? 1:0)))activate();
        else if(pressed&(1u<<(menu_swap ? 0:1)))cancel();
        return 1;}
    if(pressed&(1u<<9)){editing=picker=footer=0;SettingsModel_Page(&model,-1);}
    if(pressed&(1u<<10)){editing=picker=footer=0;SettingsModel_Page(&model,1);}
    if(pressed&(1u<<3))model.help=!model.help;
    if(pressed&(1u<<(menu_swap ? 1:0)))activate();
    if(pressed&(1u<<(menu_swap ? 0:1)))cancel();
    if(picker && (pressed&(1u<<2)))picker_switch_style();
    int dir=held&(1u<<11) ? 1:held&(1u<<12) ? 2:held&(1u<<13) ? 3:held&(1u<<14) ? 4:0;
    if(!dir && (fabsf(lx)>0.55f || fabsf(ly)>0.55f))dir=fabsf(ly)>=fabsf(lx) ? (ly<0 ? 1:2):(lx<0 ? 3:4);
    if(dir && (dir!=previous_direction || (int32_t)(now-repeat_at)>=0)) {move(dir);repeat_at=now+(dir==previous_direction ? 110u:350u);}
    previous_direction=dir;return 1;
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
static void value_text(ConfigId id,char *out,size_t cap)
{
    const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);int v=model.draft.values[id];
    if(id==CONFIG_GUARD_MODE)snprintf(out,cap,"%s",v ? "最大体力百分比":"游戏原有消耗");
    else if(id==CONFIG_RECOVERY_MODE)snprintf(out,cap,"%s",v ? "最大体力百分比":"与格挡消耗相同");
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
static void paint(RuntimeEventId event,void *subject,unsigned long context,unsigned long value,void *user)
{
    /* 借用原游戏绘制上下文的DirectDraw表面，DC只在本次绘制中持有。
     * 面板保持608×448，较大的表面只改变居中原点，不改变逻辑控件大小。 */
    (void)subject;(void)value;(void)user;
    if(event!=RUNTIME_EVENT_UI_DRAW_END || !active || !context)return;
    if(!rd(ptr((void *)backend.world_global,0),0x58) || !rd(root,0x64)){SettingsWindow_Close();return;}
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
    unsigned total_rows=(SettingsModel_Count(model.page)+1)/2,first_row=model.scroll[model.page]+1,last_row=first_row+5;
    if(last_row>total_rows)last_row=total_rows;
    char row_status[80];snprintf(row_status,sizeof row_status,"第%u–%u行 / 共%u行",first_row,last_row,total_rows);
    text(dc,x+408,y+10,184,24,row_status,RGB(207,188,154));
    const char *pages[]={"模组设置","按键设置","技能快捷"};
    for(unsigned i=0;i<3;++i){box(dc,x+16+(int)i*192,y+38,184,30,RGB(40,32,23),i==model.page ? RGB(218,168,80):RGB(104,78,38));text(dc,x+24+(int)i*192,y+44,160,20,pages[i],RGB(226,210,174));}
    unsigned start=model.scroll[model.page]*2,count=SettingsModel_Count(model.page);
    for(unsigned i=start;i<count && i<start+12;++i) {
        int cx=x+16+(int)(i%2)*288,cy=y+82+(int)((i-start)/2)*48;int focused=!footer && model.focus[model.page]==i;
        box(dc,cx,cy,280,42,RGB(31,27,21),focused ? RGB(204,69,36):RGB(96,75,43));
        char label[96],value_text_buffer[96];int dirty=0;
        if(model.page==2) {
            static const char *names[]={"A","B","X","Y","上","下","左","右","LB","RB","Back","Start","L3","R3"};
            snprintf(label,sizeof label,"RT + %s",names[i]);ConfigBinding b=model.draft_bindings[i];
            strcpy(value_text_buffer,"跟随原游戏");
            if(b.custom){strcpy(value_text_buffer,"自定义技能");for(unsigned n=0;n<skill_count;++n)if(skills[n].selector==b.selector)snprintf(value_text_buffer,sizeof value_text_buffer,"%s",skills[n].name);}
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
        text(dc,cx+(model.page==2 ? 54:8),cy+3,model.page==2 ? 218:264,18,label,RGB(222,205,172));text(dc,cx+(model.page==2 ? 54:8),cy+21,model.page==2 ? 218:264,18,value_text_buffer,dirty ? RGB(255,178,76):RGB(154,198,149));
    }
    box(dc,x+16,y+378,184,30,RGB(45,36,24),footer==1 ? RGB(204,69,36):RGB(138,103,48));text(dc,x+24,y+384,160,22,"保存并应用",RGB(236,218,178));
    box(dc,x+216,y+378,184,30,RGB(45,36,24),footer==2 ? RGB(204,69,36):RGB(138,103,48));text(dc,x+224,y+384,160,22,"关闭",RGB(236,218,178));
    box(dc,x+416,y+378,176,30,RGB(45,36,24),RGB(138,103,48));text(dc,x+424,y+384,160,22,model.help ? "隐藏说明 (Y)":"显示说明 (Y)",RGB(236,218,178));
    text(dc,x+16,y+416,576,26,message[0] ? message:(editing ? "方向键调整，确认结束编辑；Y说明":"LB/RB分类，确认编辑，Y说明；橙色表示未保存"),RGB(207,188,154));
    paint_scrollbar(dc,x,y);
    if(picker) {
        box(dc,x+16,y+82,576,322,RGB(24,22,18),RGB(194,146,65));
        text(dc,x+24,y+90,280,24,"选择已学会的技能",RGB(228,206,167));
        text(dc,x+328,y+90,256,24,picker_style ? "右手施放；X切换":"左手施放；X切换",RGB(228,206,167));
        for(unsigned n=(unsigned)pick_scroll;n<skill_view_count && n<(unsigned)pick_scroll+5;++n) {
            int sy=y+122+(int)(n-(unsigned)pick_scroll)*48;
            box(dc,x+24,sy,280,48,RGB(37,32,25),!picker_footer && n==(unsigned)pick_focus ? RGB(204,69,36):RGB(88,71,42));
            text(dc,x+80,sy+14,216,28,skills[skill_view[n]].name,RGB(225,207,177));
        }
        if(!skill_view_count)text(dc,x+32,y+138,264,100,"当前这一侧没有可分配的已学技能。按X切换左右手，或使用原游戏设置。",RGB(225,207,177));
        box(dc,x+24,y+366,280,30,RGB(45,36,24),picker_footer ? RGB(204,69,36):RGB(138,103,48));
        text(dc,x+32,y+371,264,22,"使用原游戏设置",RGB(236,218,178));
        box(dc,x+328,y+366,256,30,RGB(45,36,24),RGB(138,103,48));
        text(dc,x+336,y+371,240,22,model.help ? "隐藏技能说明 (Y)":"显示技能说明 (Y)",RGB(207,188,154));
        paint_scrollbar(dc,x,y);
    }
    if(editing) {
        const ConfigDescriptor *f=RuntimeConfig_Descriptor(edit_id);char current[128];value_text(edit_id,current,sizeof current);
        box(dc,x+16,y+82,576,288,RGB(24,22,18),RGB(194,146,65));
        text(dc,x+24,y+90,552,26,f->label,RGB(236,218,178));
        text(dc,x+24,y+132,280,24,"当前选择／数值：",RGB(222,205,172));
        text(dc,x+24,y+160,280,30,current,RGB(255,178,76));
        box(dc,x+24,y+202,128,36,RGB(45,36,24),RGB(164,124,59));box(dc,x+164,y+202,140,36,RGB(45,36,24),RGB(164,124,59));
        int options=f->type==CONFIG_CHOICE || f->type==CONFIG_TEXT || edit_id==CONFIG_PICKUP_MODE;
        text(dc,x+32,y+210,112,24,options ? "上一项 ←":"减小数值 ←",RGB(236,218,178));
        text(dc,x+172,y+210,124,24,options ? "下一项 →":"增大数值 →",RGB(236,218,178));
        box(dc,x+24,y+306,128,36,RGB(45,36,24),RGB(164,124,59));box(dc,x+164,y+306,140,36,RGB(45,36,24),RGB(164,124,59));
        text(dc,x+32,y+314,112,24,menu_swap ? "完成 (B)":"完成 (A)",RGB(236,218,178));
        text(dc,x+172,y+314,124,24,menu_swap ? "取消 (A)":"取消 (B)",RGB(236,218,178));
        if(!model.help)text(dc,x+328,y+138,256,190,"按左右方向键，或点左边的按钮调整。\n\n完成只保留这次修改；返回主列表后，选择“保存并应用”才会保存。取消会恢复打开这个调整窗口前的值。\n\n按Y可查看本项的详细说明。",RGB(207,188,154));
    }
    if(model.help && !confirm_discard && !error_modal) {
        const char *description;
        if(picker)description=!picker_footer && skill_view_count && (unsigned)pick_focus<skill_view_count ? skills[skill_view[pick_focus]].description:"使用原游戏设置会恢复这个快捷位置原来的功能，不再使用插件为它指定的技能。";
        else description=model.page==2 ? "先选择一个RT组合键位置并确认，再从已学会的技能中选择。保存后，按住RT并按这个组合键就会直接发动技能，不需要再按鼠标右键。每个角色分别保存。":RuntimeConfig_Descriptor(SettingsModel_Field(model.page,model.focus[model.page]))->description;
        RECT h=editing ? (RECT){328,122,584,362}:help_rectangle();
        box(dc,x+h.left,y+h.top,h.right-h.left,h.bottom-h.top,RGB(28,25,19),RGB(191,145,68));
        if(!help_font)help_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"宋体");
        HGDIOBJ prior=help_font ? SelectObject(dc,help_font):NULL;
        text(dc,x+h.left+8,y+h.top+8,h.right-h.left-16,h.bottom-h.top-16,description,RGB(236,218,178));
        if(prior)SelectObject(dc,prior);
    }
    if(confirm_discard){box(dc,x+96,y+160,416,116,RGB(26,23,18),RGB(208,151,67));text(dc,x+112,y+176,384,40,"有未保存修改。确认丢弃并关闭，取消继续编辑。",RGB(238,218,177));
        box(dc,x+112,y+232,176,28,RGB(45,36,24),RGB(164,124,59));text(dc,x+120,y+236,160,22,"丢弃并关闭",RGB(238,218,177));
        box(dc,x+304,y+232,192,28,RGB(45,36,24),RGB(164,124,59));text(dc,x+312,y+236,176,22,"继续编辑",RGB(238,218,177));}
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
    if(!confirm_discard && !error_modal && !editing && model.page==2) {
        /* DC释放后补原图标；避开说明框，不能把后画的图标盖到说明文字上。 */
        void *icons=ptr((void *)backend.settings_icon_global,0),*hud=ptr((void *)backend.skill_global,0);
        unsigned icon_count=rd(icons,0x44);void *sprites=ptr(icons,0x48);
        if(readable(icons,0x4C) && icon_count && icon_count<=4096 && readable(sprites,icon_count*32u)) {
            unsigned begin=picker ? (unsigned)pick_scroll:start,limit=picker ? skill_view_count:count;
            unsigned end=begin+(picker ? 5u:12u);if(end>limit)end=limit;
            for(unsigned i=begin;i<end;++i) {
                if(!picker && !model.draft_bindings[i].custom)continue;
                int selection=picker ? skills[skill_view[i]].selector:model.draft_bindings[i].selector;
                int icon=picker ? skills[skill_view[i]].icon:((This1)backend.icon_resolve)(hud,selection);
                int dx=picker ? 26:16+(int)(i%2)*288,dy=picker ? 122+(int)(i-begin)*48:82+(int)((i-start)/2)*48;
                RECT glyph={dx,dy,dx+48,dy+48},overlap,h=help_rectangle();
                if(model.help && IntersectRect(&overlap,&glyph,&h))continue;
                if(icon>=0 && (unsigned)icon<icon_count)
                    ((IconDraw)backend.icon_draw)(icons,(int)context,icon,selection,x+dx,y+dy,2,-1,0,0);
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
    if(confirm_discard){if(y>=232 && y<260){if(x>=112 && x<288)activate();else if(x>=304 && x<496)cancel();}return;}
    if(editing) {
        if(y>=202 && y<238){if(x>=24 && x<152)move(3);else if(x>=164 && x<304)move(4);}
        if(y>=306 && y<342){if(x>=24 && x<152)activate();else if(x>=164 && x<304)cancel();}
        if(y>=378 && y<408 && x>=416 && x<592)model.help=!model.help;
        return;
    }
    unsigned total,visible,top;RECT track;scroll_metrics(&total,&visible,&top,&track);
    if(x>=track.left && x<track.right && y>=track.top && y<track.bottom){scroll_at(y);scroll_drag=1;return;}
    if(picker) {
        if(x>=328 && x<584 && y>=90 && y<114)picker_switch_style();
        else if(x>=24 && x<304 && y>=122 && y<362){unsigned row=(unsigned)(y-122)/48+(unsigned)pick_scroll;
            if(row<skill_view_count){picker_footer=0;pick_focus=(int)row;activate();}}
        else if(x>=24 && x<304 && y>=366 && y<396){picker_footer=1;activate();}
        else if(x>=328 && x<584 && y>=366 && y<396)model.help=!model.help;
        return;
    }
    if(y>=38 && y<68 && x>=16 && x<592){model.page=(unsigned)((x-16)/192);footer=0;return;}
    if(y>=82 && y<370 && x>=16 && x<592) {
        unsigned item=model.scroll[model.page]*2+(unsigned)((y-82)/48)*2+(unsigned)((x-16)/288);
        if(item<SettingsModel_Count(model.page)){model.focus[model.page]=item;footer=0;activate();}return;
    }
    if(y>=378 && y<408) {
        if(x>=416 && x<592)model.help=!model.help;
        else {if(x>=16 && x<200)footer=1;else if(x>=216 && x<400)footer=2;else return;activate();}
    }
}
static void mouse_hover(int x,int y)
{
    /* 只在物理指针移动时更新焦点，静止的鼠标不能抢走手柄正在浏览的项目。 */
    if(editing || confirm_discard || error_modal || scroll_drag)return;
    if(picker) {
        if(x>=24 && x<304 && y>=122 && y<362) {
            unsigned row=(unsigned)(y-122)/48+(unsigned)pick_scroll;
            if(row<skill_view_count){picker_footer=0;pick_focus=(int)row;}
        } else if(x>=24 && x<304 && y>=366 && y<396)picker_footer=1;
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
    if(active)for(unsigned i=0;i<6;++i)if((now_keys&~old_keys)&(1u<<i)){pointer_mode=1;if(i<4)move((int)i+1);else if(i==4)activate();else model.help=!model.help;}
    old_keys=now_keys;
    int open_key=(GetAsyncKeyState(VK_OEM_3)&0x8000)!=0,escape=(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
    if(open_key && !previous_open_key){if(active)cancel();else if(open_window())pointer_mode=1;}
    previous_open_key=open_key;
    if(active && escape && !previous_escape)cancel();
    previous_escape=escape;
    int left=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0,right=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
    if(active && right && !previous_right)cancel();
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
    uintptr_t functions[]={backend.inventory_get,backend.get_jm,backend.menu_system_show,backend.menu_system_primary,backend.settings_actor_get,backend.settings_string_get,backend.ui_property,backend.lookup,backend.skill_eligibility,backend.icon_resolve,backend.icon_draw,backend.settings_skill_name,backend.settings_skill_description,backend.settings_string_destroy,backend.settings_query_skill};
    for(unsigned i=0;i<15;++i){BYTE bytes[12];if(!RuntimeWin32_Read((unsigned long)functions[i],bytes,12) || memcmp(bytes,backend.signatures[i],12)) {
        RuntimeLog_Write("[模组设置] 原接口%u地址%08lX未通过签名校验，设置窗口停用。",i,(unsigned long)functions[i]);return 0;}}
    /* 订阅分别记账；后一项失败时重试也不会重复注册前一项。 */
    if(!draw_subscribed)draw_subscribed=Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_END,paint,NULL);
    if(!input_subscribed)input_subscribed=Runtime_Subscribe(RUNTIME_EVENT_INPUT_FRAME_END,keyboard,NULL);
    if(!draw_subscribed || !input_subscribed)return 0;
    ready=1;RuntimeLog_Write("[模组设置] 原接口已校验；场景中LT+RT+Back或主键盘1左侧按键打开并使用原暂停。");return 1;
}
