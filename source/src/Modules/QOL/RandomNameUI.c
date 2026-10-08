#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "RandomNameUI.h"
#include "RandomName.h"
#include "../../Runtime/Win32Bridge.h"
#include "../../Runtime/SettingsWindow.h"
#include "../../Runtime/Log.h"

typedef struct NameBackend {
    uintptr_t ui,get_jm,menu_newgame_vtable,menu_name_vtable,menu_name_set;
    unsigned char signatures[2][12];
} NameBackend;
#include "RandomNameUIData.h"
typedef int (__attribute__((thiscall)) *GetJm)(void *,int);
typedef int (__attribute__((thiscall)) *SetText)(void *,const char *);
static NameBackend backend;static int ready;
static unsigned refusal;static int previous_f1;
static RandomNameSession session;
/* 只保留身份值作会话比较；每次使用都重新查原登记页、子对象和HWND。 */
static uintptr_t page_identity,window_identity;
static unsigned rd(void *p,unsigned offset)
{unsigned value=0;if(p)RuntimeWin32_Read((unsigned long)(uintptr_t)p+offset,&value,4);return value;}
static void *jm(int id){return (void *)(uintptr_t)((GetJm)backend.get_jm)((void *)backend.ui,id);}
static void *name_widget(void *page,HWND *window)
{
    refusal=0;
    if(!ready){refusal=1;return NULL;}
    if(!page || page!=jm(0x9C) || rd(page,0)!=backend.menu_newgame_vtable || rd(page,0x28)!=0x9C || !rd(page,0x64)){refusal=2;return NULL;}
    if(SettingsWindow_Active()){refusal=3;return NULL;}
    void *capture=(void *)(uintptr_t)rd((void *)backend.ui,0x3C);
    if(capture && capture!=page && rd(capture,0xA4)!=(uintptr_t)page){refusal=4;return NULL;}
    void *input=jm(0x9D);
    if(rd(input,0)!=backend.menu_name_vtable || rd(input,0x28)!=0x9D){refusal=5;return NULL;}
    if(rd(input,0xA4)!=(uintptr_t)page){refusal=6;return NULL;}
    if(!rd(input,0x64)){refusal=7;return NULL;}
    HWND handle=(HWND)(uintptr_t)rd(input,0xF4);DWORD process=0;
    DWORD thread=GetWindowThreadProcessId(handle,&process);
    if(!IsWindow(handle) || process!=GetCurrentProcessId()){refusal=8;return NULL;}
    if(thread!=GetCurrentThreadId()){refusal=9;return NULL;}
    *window=handle;return input;
}
void RandomNameUI_End(void)
{RandomName_End(&session);page_identity=window_identity=0;}
typedef struct AcceptContext {HDC dc;unsigned limit;char encoded[32];} AcceptContext;
static int accept_name(const char *utf8,void *user)
{
    AcceptContext *context=user;WCHAR wide[16];BOOL replaced=FALSE;
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,wide,16);if(n<3)return 0;
    int bytes=WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,wide,-1,context->encoded,sizeof context->encoded,NULL,&replaced);
    if(!bytes || replaced || (unsigned)(bytes-1)>context->limit)return 0;
    /* 用原名称编辑框当前字体验证字形，不把“GBK可编码”直接当成字体可显示。 */
    WORD glyphs[16];if(GetGlyphIndicesW(context->dc,wide,n-1,glyphs,GGI_MARK_NONEXISTING_GLYPHS)==GDI_ERROR)return 0;
    for(int i=0;i<n-1;++i)if(glyphs[i]==0xFFFF)return 0;
    return 1;
}
int RandomNameUI_Request(void *page)
{
    HWND window;void *input=name_widget(page,&window);if(!input){
        RuntimeLog_Write("[随机名称][拒绝] 原因=%u 页面=%08lX；1未就绪/2页/3模组窗口/4模态/5名称类/6父页/7隐藏/8句柄/9线程。",refusal,(unsigned long)(uintptr_t)page);
        RandomNameUI_End();return 0;}
    /* 原程序的ANSI编辑/CString链按GBK运行；未确认的系统代码页不写乱码。 */
    if(GetACP()!=936){RuntimeLog_Line("[随机名称] 当前名称编辑编码不是GBK，未覆盖原文本。");return 0;}
    if(!session.active || page_identity!=(uintptr_t)page || window_identity!=(uintptr_t)window) {
        RandomName_Begin(&session,GetTickCount()^(uint32_t)(uintptr_t)window);page_identity=(uintptr_t)page;window_identity=(uintptr_t)window;
    }
    WCHAR current_wide[64];char current[192],generated[RANDOM_NAME_CAPACITY];
    if(!GetWindowTextW(window,current_wide,64))current_wide[0]=0;
    if(!WideCharToMultiByte(CP_UTF8,0,current_wide,-1,current,sizeof current,NULL,NULL))return 0;
    LRESULT limit=SendMessageW(window,EM_GETLIMITTEXT,0,0);if(limit<4){RuntimeLog_Write("[随机名称][拒绝] 编辑容量=%ld，不足两个汉字。",(long)limit);return 0;}
    HDC dc=GetDC(window);if(!dc)return 0;
    HFONT font=(HFONT)SendMessageW(window,WM_GETFONT,0,0);HGDIOBJ previous=font ? SelectObject(dc,font):NULL;
    if(font && (!previous || previous==HGDI_ERROR)){ReleaseDC(window,dc);return 0;}
    AcceptContext context={dc,(unsigned)limit,{0}};
    unsigned characters=context.limit/2;if(characters>4)characters=4;
    /* 尚未证明原角色selector的性别对应，按生成核心约定使用中性池。 */
    int result=RandomName_Generate(&session,RANDOM_NAME_NEUTRAL,characters,current,accept_name,&context,generated,sizeof generated);
    if(previous)SelectObject(dc,previous);
    ReleaseDC(window,dc);
    if(!result){RuntimeLog_Line("[随机名称][拒绝] 候选未通过编码、容量或当前编辑字体字形检查。");return 0;}
    /* 原setter同步CString、编辑框和外传光标位置；不能自行写D4或模拟确认。 */
    ((SetText)backend.menu_name_set)(input,context.encoded);
    RuntimeLog_Line("[随机名称] 已填入名称，等待玩家确认创建。");return 1;
}
void RandomNameUI_AfterInputFrame(void)
{
    if(!ready)return;
    void *page=jm(0x9C);HWND window;void *input=name_widget(page,&window);
    if(!input){RandomNameUI_End();previous_f1=(GetAsyncKeyState(VK_F1)&0x8000)!=0;return;}
    if(!session.active || page_identity!=(uintptr_t)page || window_identity!=(uintptr_t)window) {
        RandomName_Begin(&session,GetTickCount()^(uint32_t)(uintptr_t)window);page_identity=(uintptr_t)page;window_identity=(uintptr_t)window;
        previous_f1=(GetAsyncKeyState(VK_F1)&0x8000)!=0;
        /* 仅新流程的空名称自动填写；已有手工名称保留，Y/F1可主动换一个。 */
        if(GetWindowTextLengthW(window)==0)RandomNameUI_Request(page);
    }
    int f1=(GetAsyncKeyState(VK_F1)&0x8000)!=0;
    if(f1 && !previous_f1)RandomNameUI_Request(page);
    previous_f1=f1;
}
int RandomNameUI_Initialize(const RuntimeContext *runtime)
{
    if(ready)return 1;
    if(!runtime || !runtime->profile || runtime->profile->game_id<1 || runtime->profile->game_id>2)return 0;
    backend=name_profiles[runtime->profile->game_id-1];uintptr_t targets[2]={backend.get_jm,backend.menu_name_set};
    for(unsigned i=0;i<2;++i){unsigned char actual[12];if(!RuntimeWin32_Read((unsigned long)targets[i],actual,12) || memcmp(actual,backend.signatures[i],12))return 0;}
    ready=1;return 1;
}
