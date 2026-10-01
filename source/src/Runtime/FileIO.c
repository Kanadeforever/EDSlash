#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "FileIO.h"

int RuntimeFile_Sibling(void *module,const wchar_t *name,wchar_t *path,size_t capacity)
{
    if (!module || !name || !path || !capacity || capacity>32768) return 0;
    DWORD length=GetModuleFileNameW((HMODULE)module,path,(DWORD)capacity);
    if (!length || length>=capacity) return 0;
    wchar_t *slash=wcsrchr(path,L'\\');
    if (!slash || (size_t)(slash+1-path)+wcslen(name)>=capacity) return 0;
    /* 只替换文件名，始终从ASI所在目录找配置，不依赖游戏当前工作目录。 */
    wcscpy(slash+1,name);return 1;
}
int RuntimeFile_Read(const wchar_t *path,char *bytes,size_t capacity,size_t *size)
{
    if (!path || !bytes || !capacity || !size || capacity>0xFFFFFFFFu) return 0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER length;
    if (!GetFileSizeEx(file,&length) || length.QuadPart<0 || (unsigned long long)length.QuadPart>=capacity) {
        CloseHandle(file);SetLastError(ERROR_FILE_TOO_LARGE);return 0;
    }
    DWORD read=0;
    int ok=ReadFile(file,bytes,(DWORD)length.QuadPart,&read,NULL) && read==(DWORD)length.QuadPart;
    CloseHandle(file);
    if (!ok) return 0;
    *size=read;bytes[read]=0;return 1;
}
int RuntimeFile_WriteAtomic(const wchar_t *path,const char *bytes,size_t size,int create_only)
{
    /* 保存流程改编自Castle Reforge的runtime_file.c。
     * 先完成临时文件，原文件直到最终替换成功才变化；每一步都检查实际返回值。
     */
    static LONG serial;
    wchar_t temp[1200];
    if (!path || !bytes || size>0xFFFFFFFFu || wcslen(path)>1100) return 0;
    int length=swprintf(temp,1200,L"%ls.%lu.%ld.tmp",path,GetCurrentProcessId(),InterlockedIncrement(&serial));
    if (length<0 || length>=1200) return 0;
    HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) return 0;
    DWORD written=0;
    int ok=WriteFile(file,bytes,(DWORD)size,&written,NULL) && written==size && FlushFileBuffers(file);
    if (!CloseHandle(file)) ok=0;
    if (ok) ok=MoveFileExW(temp,path,MOVEFILE_WRITE_THROUGH|(create_only ? 0:MOVEFILE_REPLACE_EXISTING))!=0;
    if (!ok) {
        DWORD error=GetLastError();DeleteFileW(temp);SetLastError(error);return 0;
    }
    return 1;
}
