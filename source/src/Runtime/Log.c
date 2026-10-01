#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Log.h"
#include "FileIO.h"

#define LOG_CAPACITY 65536u
static SRWLOCK queue_lock=SRWLOCK_INIT;
static char queue[LOG_CAPACITY];
static unsigned head,count;
static HANDLE file=INVALID_HANDLE_VALUE,worker,wake,drained;
static volatile LONG worker_state,stopping,in_flight;
static LONG written_bytes,dropped_messages,file_writes;
static int initialized;
/* 一个持久句柄、一份有界队列。游戏线程只复制文本，磁盘写入由低优先级线程完成。 */
int RuntimeLog_Initialize(void *module)
{
    if (initialized) return 1;
    wchar_t path[1100];
    if (!RuntimeFile_Sibling(module,L"EDSlash.log",path,1100)) return 0;
    file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,
        CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) return 0;
    initialized=1;RuntimeLog_Line("EDSlash：统一迁移性能候选1，TOML与静态SDL、后台日志版");return 1;
}
static unsigned take_chunk(char *output,unsigned capacity)
{
    AcquireSRWLockExclusive(&queue_lock);
    unsigned size=count<capacity ? count:capacity;
    for (unsigned i=0;i<size;++i) output[i]=queue[(head+i)%LOG_CAPACITY];
    head=(head+size)%LOG_CAPACITY;count-=size;
    if (size) InterlockedExchange(&in_flight,1);
    ReleaseSRWLockExclusive(&queue_lock);
    return size;
}
static void write_chunk(const char *text,unsigned size)
{
    unsigned position=0;
    /* 即使系统只完成部分写入，也从实际已写的位置继续，不能假定WriteFile一定写完。 */
    while (position<size) {
        DWORD actual=0;
        if (!WriteFile(file,text+position,size-position,&actual,NULL) || !actual) {
            InterlockedIncrement(&dropped_messages);break;
        }
        position+=actual;InterlockedExchangeAdd(&written_bytes,(LONG)actual);
        InterlockedIncrement(&file_writes);
    }
    InterlockedExchange(&in_flight,0);
}
static void drain_queue(void)
{
    char chunk[8192];unsigned size;
    while ((size=take_chunk(chunk,sizeof chunk))!=0) write_chunk(chunk,size);
    if (drained) SetEvent(drained);
}
static DWORD WINAPI log_worker(void *unused)
{
    (void)unused;
    /* 首轮直接处理DllMain阶段积累的日志，之后由事件或250ms超时唤醒。 */
    for (;;) {
        drain_queue();
        if (InterlockedCompareExchange(&stopping,0,0)) break;
        WaitForSingleObject(wake,250);
    }
    return 0;
}
int RuntimeLog_Start(void)
{
    if (!initialized) return 0;
    LONG state=InterlockedCompareExchange(&worker_state,1,0);
    if (state) return state>0;
    wake=CreateEventW(NULL,FALSE,FALSE,NULL);
    drained=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (wake && drained) worker=CreateThread(NULL,0,log_worker,NULL,0,NULL);
    if (!worker) {
        /* 线程创建失败时仍用同一持久句柄，恢复同步写入，不能让日志故障禁用游戏功能。 */
        if (wake) CloseHandle(wake);
        if (drained) CloseHandle(drained);
        wake=drained=NULL;InterlockedExchange(&worker_state,-1);drain_queue();return 0;
    }
    SetThreadPriority(worker,THREAD_PRIORITY_BELOW_NORMAL);
    InterlockedExchange(&worker_state,2);SetEvent(wake);return 1;
}
void RuntimeLog_Text(const char *text)
{
    if (!text || !initialized) return;
    size_t size=strlen(text);
    AcquireSRWLockExclusive(&queue_lock);
    /* 队列满时不阻塞游戏。计数会出现在性能摘要，不能静默把证据完整性说成已保证。 */
    if (size>LOG_CAPACITY-count) {
        InterlockedIncrement(&dropped_messages);
        ReleaseSRWLockExclusive(&queue_lock);return;
    }
    if (drained) ResetEvent(drained);
    for (size_t i=0;i<size;++i) queue[(head+count+(unsigned)i)%LOG_CAPACITY]=text[i];
    count+=(unsigned)size;
    ReleaseSRWLockExclusive(&queue_lock);
    if (wake) SetEvent(wake);
    if (InterlockedCompareExchange(&worker_state,0,0)==-1) drain_queue();
}
void RuntimeLog_Line(const char *text)
{
    if (!text) return;
    char line[4096];int n=snprintf(line,sizeof line,"%s\r\n",text);
    if (n>=0 && (size_t)n<sizeof line) RuntimeLog_Text(line);
}
void RuntimeLog_Write(const char *format,...)
{
    va_list args;va_start(args,format);RuntimeLog_VWrite(format,args);va_end(args);
}
void RuntimeLog_VWrite(const char *format,va_list args)
{
    char line[2048];int n=vsnprintf(line,sizeof line,format,args);
    if (n>=0 && (size_t)n<sizeof line) RuntimeLog_Line(line);
}
void RuntimeLog_GetStats(RuntimeLogStats *stats)
{
    if (!stats) return;
    AcquireSRWLockShared(&queue_lock);stats->queued_bytes=count;ReleaseSRWLockShared(&queue_lock);
    stats->written_bytes=(unsigned long)InterlockedCompareExchange(&written_bytes,0,0);
    stats->dropped_messages=(unsigned long)InterlockedCompareExchange(&dropped_messages,0,0);
    stats->file_opens=initialized ? 1u:0u;
    stats->file_writes=(unsigned long)InterlockedCompareExchange(&file_writes,0,0);
}
int RuntimeLog_Flush(unsigned long timeout_ms)
{
    if (!initialized) return 0;
    if (InterlockedCompareExchange(&worker_state,0,0)<=0) {drain_queue();return FlushFileBuffers(file)!=0;}
    DWORD begin=GetTickCount();
    for (;;) {
        RuntimeLogStats stats;RuntimeLog_GetStats(&stats);
        if (!stats.queued_bytes && !InterlockedCompareExchange(&in_flight,0,0)) return FlushFileBuffers(file)!=0;
        DWORD elapsed=GetTickCount()-begin;
        if (elapsed>=timeout_ms) return 0;
        SetEvent(wake);WaitForSingleObject(drained,timeout_ms-elapsed);
    }
}
void RuntimeLog_Shutdown(void)
{
    if (!initialized) return;
    RuntimeLog_Flush(2000);
    InterlockedExchange(&stopping,1);
    if (wake) SetEvent(wake);
    if (worker) {
        /* 此接口要求调用者先停止日志生产；工作线程收尾后才能关句柄。 */
        WaitForSingleObject(worker,INFINITE);CloseHandle(worker);
    }
    if (wake) CloseHandle(wake);
    if (drained) CloseHandle(drained);
    CloseHandle(file);file=INVALID_HANDLE_VALUE;worker=wake=drained=NULL;initialized=0;
}
