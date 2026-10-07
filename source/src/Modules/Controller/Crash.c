#include "Plugin.h"
#include "Crash.h"
#include "../../Runtime/Focus.h"
#include "../../Runtime/FileIO.h"
#include <stdio.h>
#include <string.h>

static PVOID exception_hook;
static WCHAR report_path[1024];
static volatile LONG writing;
static const char *current_stage="手柄普通运行";
static char report[2048];
void Crash_Stage(const char *stage) {current_stage=stage ? stage:"未知阶段";}
static LONG CALLBACK record_exception(EXCEPTION_POINTERS *details)
{
    if (!details || !details->ExceptionRecord || !details->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code=details->ExceptionRecord->ExceptionCode;
    /* 普通C++异常、调试断点等不记录。只记录内存/指令/除零类异常，原处理链继续负责退出。 */
    if (code!=EXCEPTION_ACCESS_VIOLATION && code!=EXCEPTION_IN_PAGE_ERROR &&
        code!=EXCEPTION_ILLEGAL_INSTRUCTION && code!=EXCEPTION_INT_DIVIDE_BY_ZERO) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedCompareExchange(&writing,1,0)) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c=details->ContextRecord;EXCEPTION_RECORD *e=details->ExceptionRecord;
    HMODULE module=NULL;WCHAR name[MAX_PATH]={0};char utf8[MAX_PATH*3]={0};
    /* 从异常地址取得模块及偏移，只读取系统提供的异常上下文，不尝试读坏地址。 */
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCWSTR)e->ExceptionAddress,&module)) GetModuleFileNameW(module,name,MAX_PATH);
    WideCharToMultiByte(CP_UTF8,0,name,-1,utf8,sizeof utf8,NULL,NULL);
    SYSTEMTIME time;GetLocalTime(&time);
    ULONG_PTR access=e->NumberParameters>0 ? e->ExceptionInformation[0]:0;
    ULONG_PTR address=e->NumberParameters>1 ? e->ExceptionInformation[1]:0;
    int length=snprintf(report,sizeof report,
        "异常记录 %04u-%02u-%02u %02u:%02u:%02u\r\n最近手柄阶段=%s\r\n异常=%08lX 地址=%08lX 模块=%s 偏移=%08lX\r\n"
        "访问类型=%lu 故障地址=%08lX 线程=%lu 输入层=%d 按键=%08lX\r\n"
        "EIP=%08lX ESP=%08lX EBP=%08lX EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX\r\n"
        "这是首次异常记录，不吞异常；最终退出由游戏/系统原处理链决定。\r\n\r\n",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,
        RuntimeFocus_Stage() ? RuntimeFocus_Stage():current_stage,
        code,(DWORD)(uintptr_t)e->ExceptionAddress,utf8,(DWORD)((uintptr_t)e->ExceptionAddress-(uintptr_t)module),
        (DWORD)access,(DWORD)address,GetCurrentThreadId(),g_intent.layer,(DWORD)g_input.buttons,
        c->Eip,c->Esp,c->Ebp,c->Eax,c->Ebx,c->Ecx,c->Edx,c->Esi,c->Edi);
    /* 直接追加独立小文本，不依赖后台日志队列或堆分配，尽量保留进程退出前的证据。 */
    if(length>0) {
        DWORD bytes=(DWORD)(length<(int)sizeof report ? length:(int)sizeof report-1),written;
        HANDLE file=CreateFileW(report_path,FILE_APPEND_DATA,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(file!=INVALID_HANDLE_VALUE) {WriteFile(file,report,bytes,&written,NULL);CloseHandle(file);}
    }
    InterlockedExchange(&writing,0);return EXCEPTION_CONTINUE_SEARCH;
}
int Crash_Initialize(void)
{
    if(exception_hook)return 1;
    const RuntimeContext *runtime=Runtime_GetContext();
    /* 离线宿主没有真实ASI模块时不注册；正常游戏报告放在当前ASI旁。 */
    if(!runtime || !runtime->self_module)return 1;
    if(!RuntimeFile_Sibling(runtime->self_module,L"EDSlash崩溃记录.txt",report_path,1024))return 0;
    exception_hook=AddVectoredExceptionHandler(1,record_exception);
    return exception_hook!=NULL;
}
void Crash_Shutdown(void)
{
    if(exception_hook)RemoveVectoredExceptionHandler(exception_hook);
    exception_hook=NULL;
}
