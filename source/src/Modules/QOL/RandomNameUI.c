#include "QOLText.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "RandomNameUI.h"
#include "RandomName.h"
#include "../../Runtime/Win32Bridge.h"
#include "../../Runtime/SettingsWindow.h"
#include "../../Runtime/Log.h"

typedef struct NameBackend {
    uintptr_t ui,get_jm,menu_newgame_vtable,menu_name_vtable,menu_name_set;
    unsigned birthday;unsigned char signatures[2][12];
} NameBackend;
#include "RandomNameUIData.h"
typedef int (__fastcall *GetJm)(void *, void *,int);
typedef int (__fastcall *SetText)(void *, void *,const char *);
static NameBackend backend;static int ready;
static unsigned refusal;static int previous_f1;static uint32_t birthday_state;
static RandomNameSession session;
/* 只保留身份值作会话比较；每次使用都重新查原登记页、子对象和HWND。 */
static uintptr_t page_identity,window_identity;
static unsigned rd(void *p,unsigned offset)
{unsigned value=0;if(p)RuntimeWin32_Read((unsigned long)(uintptr_t)p+offset,&value,4);return value;}
static void *jm(int id){return (void *)(uintptr_t)((GetJm)backend.get_jm)((void *)backend.ui, NULL,id);}
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
typedef struct AcceptContext {unsigned limit;char encoded[32];} AcceptContext;
static int accept_name(const char *utf8,void *user)
{
    AcceptContext *context=user;WCHAR wide[16];BOOL replaced=FALSE;
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,wide,16);if(n<3)return 0;
    int bytes=WideCharToMultiByte(936,WC_NO_BEST_FIT_CHARS,wide,-1,context->encoded,sizeof context->encoded,NULL,&replaced);
    if(!bytes || replaced || (unsigned)(bytes-1)>context->limit)return 0;
    /* 原EDIT收集输入，名称由游戏CJmName的文字接口绘制。
     * EDIT默认字体不是屏幕上的游戏字体，不能以它缺少汉字拒绝整个候选池。 */
    return 1;
}
/* 外传原日期校验允许1..12月；二月29天，四/六/九/十一月30天，其余31天。
 * 生日控件可以隐藏其EDIT，CString仍是原创建校验的输入；不直接改创建参数。 */
static const unsigned month_days[12]={31,29,31,30,31,30,31,31,30,31,30,31};
static void birthday_pick(unsigned *month,unsigned *day)
{
    if(!birthday_state)birthday_state=GetTickCount()^0x6D2B79F5u;
    birthday_state^=birthday_state<<13;birthday_state^=birthday_state>>17;birthday_state^=birthday_state<<5;
    unsigned index=birthday_state%366u;*month=1;
    while(index>=month_days[*month-1]){index-=month_days[*month-1];++*month;}
    *day=index+1;
}
int RandomNameUI_Request(void *page)
{
    HWND window;void *input=name_widget(page,&window);if(!input){
        RuntimeLog_Write(QOLText_RandomName_PageValidationRejectedLog,refusal,(unsigned long)(uintptr_t)page);
        RandomNameUI_End();return 0;}
    void *month_input=NULL,*day_input=NULL;
    if(backend.birthday) {
        month_input=jm(0xDF);day_input=jm(0xE0);
        if(rd(month_input,0)!=backend.menu_name_vtable || rd(day_input,0)!=backend.menu_name_vtable ||
           rd(month_input,0x28)!=0xDF || rd(day_input,0x28)!=0xE0 ||
           rd(month_input,0xA4)!=(uintptr_t)page || rd(day_input,0xA4)!=(uintptr_t)page) {
            RuntimeLog_Line(QOLText_RandomName_BirthdayControlsRejectedLog);return 0;
        }
    }
    /* 原程序的ANSI编辑/CString链按GBK运行；未确认的系统代码页不写乱码。 */
    if(GetACP()!=936){RuntimeLog_Line(QOLText_RandomName_NonGbkEncodingRejectedLog);return 0;}
    if(!session.active || page_identity!=(uintptr_t)page || window_identity!=(uintptr_t)window) {
        RandomName_Begin(&session,GetTickCount()^(uint32_t)(uintptr_t)window);page_identity=(uintptr_t)page;window_identity=(uintptr_t)window;
    }
    WCHAR current_wide[64];char current[192],generated[RANDOM_NAME_CAPACITY];
    if(!GetWindowTextW(window,current_wide,64))current_wide[0]=0;
    if(!WideCharToMultiByte(CP_UTF8,0,current_wide,-1,current,sizeof current,NULL,NULL))return 0;
    LRESULT limit=SendMessageW(window,EM_GETLIMITTEXT,0,0);if(limit<4){RuntimeLog_Write(QOLText_RandomName_EditCapacityRejectedLog,(long)limit);return 0;}
    AcceptContext context={(unsigned)limit,{0}};
    unsigned characters=context.limit/2;if(characters>4)characters=4;
    /* 尚未证明原角色selector的性别对应，按生成核心约定使用中性池。 */
    int result=RandomName_Generate(&session,RANDOM_NAME_NEUTRAL,characters,current,accept_name,&context,generated,sizeof generated);
    if(!result){RuntimeLog_Line(QOLText_RandomName_CandidateEncodingRejectedLog);return 0;}
    /* 原setter同步CString、编辑框和外传光标位置；不能自行写D4或模拟确认。 */
    ((SetText)backend.menu_name_set)(input, NULL,context.encoded);
    if(backend.birthday) {
        unsigned month,day;char month_text[4],day_text[4];birthday_pick(&month,&day);
        snprintf(month_text,sizeof month_text,"%u",month);snprintf(day_text,sizeof day_text,"%u",day);
        ((SetText)backend.menu_name_set)(month_input, NULL,month_text);((SetText)backend.menu_name_set)(day_input, NULL,day_text);
        RuntimeLog_Write(QOLText_RandomName_NameAndBirthdayFilledLog,month,day);
    } else RuntimeLog_Line(QOLText_RandomName_NameFilledLog);return 1;
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
