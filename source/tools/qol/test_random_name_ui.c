#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/Runtime/Win32Bridge.h"
static UINT fixture_acp(void){return 936;}
#define GetACP fixture_acp
static int f1_down;
static SHORT fixture_async(int key){return key==VK_F1 && f1_down ? (SHORT)-32768:0;}
#define GetAsyncKeyState fixture_async
#include "../../src/Modules/QOL/RandomNameUI.c"
#undef GetACP
#undef GetAsyncKeyState
static BYTE fake_ui[0x80],fake_page[0xC0],fake_input[0x100];
static BYTE fake_month[0x100],fake_day[0x100];static char birth_month[4],birth_day[4];static unsigned birthday_writes;
static HWND edit;static unsigned checks,writes;static char native_text[32];
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"随机名称适配失败 行%d：%s\n",__LINE__,#x);exit(1);}}while(0)
static void wr(void *p,unsigned off,uintptr_t value){DWORD n=(DWORD)value;memcpy((BYTE *)p+off,&n,4);}
int RuntimeWin32_Read(unsigned long address,void *out,unsigned long bytes)
{MEMORY_BASIC_INFORMATION i;if(!address || !VirtualQuery((void *)address,&i,sizeof i) || i.State!=MEM_COMMIT || (i.Protect&(PAGE_NOACCESS|PAGE_GUARD)) || address+bytes>(uintptr_t)i.BaseAddress+i.RegionSize)return 0;memcpy(out,(void *)address,bytes);return 1;}
void RuntimeLog_Line(const char *text){(void)text;}
void RuntimeLog_Write(const char *format,...){(void)format;}
int SettingsWindow_Active(void){return 0;}
static int __fastcall get_jm(void *self, void *unused_edx,int id)
{ (void)unused_edx;CHECK(self==fake_ui);return (int)(uintptr_t)(id==0x9C ? fake_page:id==0x9D ? fake_input:id==0xDF ? fake_month:id==0xE0 ? fake_day:NULL);}
static int __fastcall set_text(void *self, void *unused_edx,const char *text)
{ (void)unused_edx;if(self==fake_month || self==fake_day){CHECK(strlen(text)<=2);strcpy(self==fake_month ? birth_month:birth_day,text);++birthday_writes;return 1;}CHECK(self==fake_input && strlen(text)<sizeof native_text);strcpy(native_text,text);++writes;WCHAR wide[16];CHECK(MultiByteToWideChar(936,MB_ERR_INVALID_CHARS,text,-1,wide,16));CHECK(SetWindowTextW(edit,wide));return 1;}
int main(void)
{
    edit=CreateWindowExW(0,L"EDIT",L"",WS_POPUP,0,0,200,30,NULL,NULL,GetModuleHandleW(NULL),NULL);CHECK(edit);
    HFONT font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"宋体");CHECK(font);
    /* 原名称输入没有WM_SETFONT，文字另由游戏绘制；默认EDIT也必须能接受随机中文名。 */
    CHECK(!SendMessageW(edit,WM_GETFONT,0,0));SendMessageW(edit,EM_LIMITTEXT,12,0);
    backend=(NameBackend){.ui=(uintptr_t)fake_ui,.get_jm=(uintptr_t)get_jm,.menu_newgame_vtable=111,.menu_name_vtable=222,.menu_name_set=(uintptr_t)set_text};ready=1;
    wr(fake_page,0,111);wr(fake_page,0x28,0x9C);wr(fake_page,0x64,1);wr(fake_input,0,222);wr(fake_input,0x28,0x9D);wr(fake_input,0x64,1);wr(fake_input,0xA4,(uintptr_t)fake_page);wr(fake_input,0xF4,(uintptr_t)edit);
    RandomNameUI_AfterInputFrame();CHECK(writes==1 && strlen(native_text)>=4 && strlen(native_text)<=8);
    char first[32];strcpy(first,native_text);RandomNameUI_AfterInputFrame();CHECK(writes==1);
    CHECK(RandomNameUI_Request(fake_page) && writes==2 && strcmp(first,native_text));
    SendMessageW(edit,EM_LIMITTEXT,2,0);CHECK(!RandomNameUI_Request(fake_page) && writes==2);SendMessageW(edit,EM_LIMITTEXT,12,0);
    wr(fake_page,0x64,0);RandomNameUI_AfterInputFrame();CHECK(!session.active && !RandomNameUI_Request(fake_page) && writes==2);
    wr(fake_page,0x64,1);CHECK(SetWindowTextW(edit,L"自定"));RandomNameUI_AfterInputFrame();CHECK(writes==2);
    wr(fake_ui,0x3C,1);CHECK(!RandomNameUI_Request(fake_page) && writes==2);wr(fake_ui,0x3C,0);
    CHECK(RandomNameUI_Request(fake_page) && writes==3);RandomNameUI_End();
    /* F1只按新边沿换名，长按不连发；字母仍由原编辑框处理。 */
    RandomNameUI_AfterInputFrame();unsigned baseline=writes;
    f1_down=1;RandomNameUI_AfterInputFrame();CHECK(writes==baseline+1);
    RandomNameUI_AfterInputFrame();CHECK(writes==baseline+1);
    f1_down=0;RandomNameUI_AfterInputFrame();f1_down=1;RandomNameUI_AfterInputFrame();CHECK(writes==baseline+2);
    wr(fake_page,0x64,0);RandomNameUI_AfterInputFrame();CHECK(writes==baseline+2);
    wr(fake_page,0x64,1);RandomNameUI_AfterInputFrame();CHECK(writes==baseline+2);
    f1_down=0;RandomNameUI_AfterInputFrame();f1_down=1;RandomNameUI_AfterInputFrame();CHECK(writes==baseline+3);
    /* 外传生日共366个合法日期，包含原规则允许的二月29日；隐藏生日EDIT也可同步原文本。 */
    backend.birthday=1;
    wr(fake_month,0,222);wr(fake_month,0x28,0xDF);wr(fake_month,0xA4,(uintptr_t)fake_page);
    wr(fake_day,0,222);wr(fake_day,0x28,0xE0);wr(fake_day,0xA4,(uintptr_t)fake_page);
    /* 用固定种子复现同一组合法日期；测试覆盖不能依赖系统时间或随机抽中某一天。 */
    birthday_state=1;
    unsigned seen_months=0;int leap_seen=0;
    for(unsigned i=0;i<1000;++i){CHECK(RandomNameUI_Request(fake_page));unsigned m=(unsigned)atoi(birth_month),d=(unsigned)atoi(birth_day);
        CHECK(m>=1 && m<=12 && d>=1 && d<=month_days[m-1]);seen_months|=1u<<(m-1);if(m==2 && d==29)leap_seen=1;}
    CHECK(seen_months==0xFFF && leap_seen && birthday_writes==2000);
    wr(fake_day,0xA4,0);unsigned previous=writes;CHECK(!RandomNameUI_Request(fake_page) && writes==previous && birthday_writes==2000);
    DestroyWindow(edit);DeleteObject(font);printf("原名称框编码/容量/无EDIT字体/生日/会话与空名自动填写通过：%u项\n",checks);return 0;
}
