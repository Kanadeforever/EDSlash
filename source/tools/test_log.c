#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/Runtime/Log.h"
#include "../src/Runtime/FileIO.h"
/* 真实并发日志测试，只写自有构建目录的日志，不接触游戏。 */
static unsigned checks;
#define CHECK(test) do {++checks;if(!(test)){printf("后台日志检查失败，第%d行\n",__LINE__);return 1;}}while(0)
static DWORD WINAPI producer(void *arg)
{
    unsigned id=(unsigned)(uintptr_t)arg;
    for (unsigned n=0;n<128;++n) RuntimeLog_Write("[日志回归] 生产者=%u 序号=%u 中文保持",id,n);
    return 0;
}
int main(void)
{
    CHECK(RuntimeLog_Initialize(GetModuleHandleW(NULL)));
    CHECK(RuntimeLog_Flush(0));
    RuntimeLogStats before;RuntimeLog_GetStats(&before);CHECK(before.file_opens==1);
    CHECK(RuntimeLog_Start());
    HANDLE threads[4];
    LARGE_INTEGER begin,end,frequency;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);
    for (unsigned i=0;i<4;++i) {
        threads[i]=CreateThread(NULL,0,producer,(void *)(uintptr_t)i,0,NULL);
        CHECK(threads[i]!=NULL);
    }
    CHECK(WaitForMultipleObjects(4,threads,TRUE,5000)==WAIT_OBJECT_0);
    QueryPerformanceCounter(&end);
    for (unsigned i=0;i<4;++i) CloseHandle(threads[i]);
    CHECK(RuntimeLog_Flush(5000));
    RuntimeLogStats stats;RuntimeLog_GetStats(&stats);
    CHECK(stats.file_opens==1 && stats.queued_bytes==0 && stats.dropped_messages==0);
    CHECK(stats.written_bytes>before.written_bytes && stats.file_writes<513);
    wchar_t path[1100];static char bytes[65536];size_t size;
    CHECK(RuntimeFile_Sibling(GetModuleHandleW(NULL),L"EDSlash.log",path,1100));
    CHECK(RuntimeFile_Read(path,bytes,sizeof bytes,&size));
    /* 本次日志必须包含实际编译摘要，不能沿用旧性能候选的实机标签。 */
    CHECK(strstr(bytes,EDSLASH_BUILD_ID)!=NULL);
    CHECK(strstr(bytes,"当前产物实机待验收")!=NULL);
    CHECK(strstr(bytes,"性能候选1")==NULL);
    /* 每条记录应恰好出现一次，即使四个生产线程交错也不能串行内容。 */
    for (unsigned id=0;id<4;++id) for(unsigned n=0;n<128;++n) {
        char expected[160];snprintf(expected,sizeof expected,"[日志回归] 生产者=%u 序号=%u 中文保持\r\n",id,n);
        char *first=strstr(bytes,expected);CHECK(first && !strstr(first+strlen(expected),expected));
    }
    for(size_t i=0;i<size;++i)if(bytes[i]=='\n')CHECK(i>0 && bytes[i-1]=='\r');
    RuntimeLog_Shutdown();CHECK(DeleteFileW(path));
    printf("后台日志并发、单次打开、批量写入、中文和完整收尾通过：%u项，512条生产耗时%.3f毫秒\n",
        checks,(double)(end.QuadPart-begin.QuadPart)*1000.0/frequency.QuadPart);
    return 0;
}
