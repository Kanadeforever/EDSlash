/* 在自有宿主直接喂入异常上下文，测试文本记录；不制造游戏崩溃、不附加进程。 */
#include "../../src/Modules/Controller/Crash.c"
#include <stdlib.h>
#include <wchar.h>

Intent g_intent;PadInput g_input;
const char *RuntimeFocus_Stage(void){return NULL;}
static const RuntimeContext runtime={.self_module=(void *)1};
const RuntimeContext *Runtime_GetContext(void){return &runtime;}
static WCHAR output_path[1024];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"异常诊断检查失败 行%d：%s\n",__LINE__,#x);exit(1);}}while(0)
int RuntimeFile_Sibling(void *module,const wchar_t *name,wchar_t *path,size_t capacity)
{
    CHECK(module==(void *)1 && !wcscmp(name,L"EDSlash崩溃记录.txt"));
    CHECK(wcslen(output_path)<capacity);wcscpy(path,output_path);return 1;
}
static DWORD read_report(char *bytes)
{
    HANDLE file=CreateFileW(output_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    CHECK(file!=INVALID_HANDLE_VALUE);DWORD n=0;
    CHECK(ReadFile(file,bytes,4095,&n,NULL));CloseHandle(file);bytes[n]=0;return n;
}
int main(void)
{
    WCHAR root[900];CHECK(GetCurrentDirectoryW(900,root));
    swprintf(output_path,1024,L"%ls\\异常记录回归_%lu.txt",root,GetCurrentProcessId());
    CHECK(GetFileAttributesW(output_path)==INVALID_FILE_ATTRIBUTES);
    CHECK(Crash_Initialize());CHECK(Crash_Initialize());
    EXCEPTION_RECORD exception={0};CONTEXT context={0};EXCEPTION_POINTERS details={&exception,&context};
    exception.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;
    exception.ExceptionAddress=(void *)(uintptr_t)record_exception;
    exception.NumberParameters=2;exception.ExceptionInformation[0]=1;exception.ExceptionInformation[1]=0xDEADBEEF;
    context.Eip=(DWORD)(uintptr_t)record_exception;context.Esp=0x12345678;context.Eax=0xABCD1234;
    Crash_Stage("源图边界回归");g_intent.layer=LAYER_MENU;g_input.buttons=KEY(PAD_X);
    CHECK(record_exception(&details)==EXCEPTION_CONTINUE_SEARCH);
    char bytes[4096];DWORD before=read_report(bytes);
    CHECK(before>0 && strstr(bytes,"源图边界回归") && strstr(bytes,"DEADBEEF") && strstr(bytes,"ABCD1234"));
    CHECK(strstr(bytes,"\r\n") && (unsigned char)bytes[0]!=0xEF);
    CHECK(record_exception(NULL)==EXCEPTION_CONTINUE_SEARCH);
    exception.ExceptionCode=0xE06D7363;CHECK(record_exception(&details)==EXCEPTION_CONTINUE_SEARCH);
    CHECK(read_report(bytes)==before);
    exception.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;writing=1;
    CHECK(record_exception(&details)==EXCEPTION_CONTINUE_SEARCH);writing=0;CHECK(read_report(bytes)==before);
    Crash_Shutdown();CHECK(!exception_hook);CHECK(DeleteFileW(output_path));
    printf("异常地址/寄存器/中文文本、递归保护及原异常链保留通过：%u项\n",checks);return 0;
}
