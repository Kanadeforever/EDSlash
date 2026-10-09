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
    wchar_t existing_path[1100];CHECK(RuntimeFile_Sibling(GetModuleHandleW(NULL),L"EDSlash.log",existing_path,1100));
    HANDLE preserved=CreateFileW(existing_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);CHECK(preserved!=INVALID_HANDLE_VALUE);
    DWORD copied;CHECK(WriteFile(preserved,"保留旧日志",15,&copied,NULL) && copied==15);CloseHandle(preserved);
    RuntimeLog_SetEnabled(0);CHECK(!RuntimeLog_Enabled());
    CHECK(RuntimeLog_Initialize(GetModuleHandleW(NULL)));
    RuntimeLogStats disabled;RuntimeLog_GetStats(&disabled);CHECK(!disabled.file_opens);
    preserved=CreateFileW(existing_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(preserved!=INVALID_HANDLE_VALUE);
    char kept[32]={0};CHECK(ReadFile(preserved,kept,sizeof kept,&copied,NULL) && copied==15 && !memcmp(kept,"保留旧日志",15));CloseHandle(preserved);
    RuntimeLog_Line("关闭时不写入");RuntimeLog_SetEnabled(1);CHECK(RuntimeLog_Enabled());
    CHECK(RuntimeLog_Initialize(GetModuleHandleW(NULL)));
    CHECK(RuntimeLog_Flush(0));
    RuntimeLogStats before;RuntimeLog_GetStats(&before);CHECK(before.file_opens==1);
    /* 模拟A版接口返回的路径：系统代码页可能无法表示汉字，原始字节会先变成替代字符。
     * 日志转换只能保留接口实际返回的内容，不能从替代字符恢复已经丢失的汉字。 */
    char native[256],mixed[512]="[路径] ";
    CHECK(WideCharToMultiByte(CP_ACP,0,L"C:\\游戏目录\\EDSlash.toml",-1,native,sizeof native,NULL,NULL)>0);
    /* 用Windows真实API读取ANSI片段的含义，建立当前系统下应得到的UTF-8结果。
     * 1252检查替代字符被如实保留，936和UTF-8仍严格检查中文路径完整转换。 */
    wchar_t decoded[256];char expected[256];
    CHECK(MultiByteToWideChar(CP_ACP,0,native,-1,decoded,256)>0);
    CHECK(WideCharToMultiByte(CP_UTF8,0,decoded,-1,expected,sizeof expected,NULL,NULL)>0);
    CHECK(RuntimeLog_AppendAnsi(mixed,sizeof mixed,native));
    CHECK(!memcmp(mixed,"[路径] ",strlen("[路径] ")));
    CHECK(!strcmp(mixed+strlen("[路径] "),expected));
    CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,mixed,-1,NULL,0)>0);
    char tiny[8]="保留";CHECK(!RuntimeLog_AppendAnsi(tiny,sizeof tiny,native) && !strcmp(tiny,"保留"));
    RuntimeLog_Line(mixed);
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
    /* 日志首行必须标识实际编译输入，便于将用户日志与当前产物对应。 */
    CHECK(strstr(bytes,EDSLASH_BUILD_ID)!=NULL);
    CHECK(strstr(bytes,"EDSlash：构建身份=")==bytes);
    /* 每条记录应恰好出现一次，即使四个生产线程交错也不能串行内容。 */
    for (unsigned id=0;id<4;++id) for(unsigned n=0;n<128;++n) {
        char expected[160];snprintf(expected,sizeof expected,"[日志回归] 生产者=%u 序号=%u 中文保持\r\n",id,n);
        char *first=strstr(bytes,expected);CHECK(first && !strstr(first+strlen(expected),expected));
    }
    for(size_t i=0;i<size;++i)if(bytes[i]=='\n')CHECK(i>0 && bytes[i-1]=='\r');
    CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,(int)size,NULL,0)>0);
    RuntimeLog_GetStats(&before);RuntimeLog_SetEnabled(0);
    RuntimeLog_Write("不应写入%d",123);RuntimeLog_Line("不应写入");CHECK(RuntimeLog_Flush(1000));
    RuntimeLog_GetStats(&disabled);CHECK(disabled.written_bytes==before.written_bytes && !disabled.queued_bytes);
    RuntimeLog_SetEnabled(1);RuntimeLog_Line("重新开启日志");CHECK(RuntimeLog_Flush(1000));
    RuntimeLog_GetStats(&disabled);CHECK(disabled.written_bytes>before.written_bytes);
    RuntimeLog_Shutdown();CHECK(DeleteFileW(path));
    printf("后台日志并发、单次打开、批量写入、中文和完整收尾通过：%u项，512条生产耗时%.3f毫秒\n",
        checks,(double)(end.QuadPart-begin.QuadPart)*1000.0/frequency.QuadPart);
    return 0;
}
