#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
/* 未知EXE加载统一插件时安全返回；预占默认ImageBase还可检查实际重定位加载。 */
int wmain(int argc,wchar_t **argv)
{
    if (argc!=2) return 2;
    HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if (file==INVALID_HANDLE_VALUE) return 1;
    BYTE header[4096];DWORD read;
    if (!ReadFile(file,header,sizeof header,&read,NULL) || read<512) return 1;
    CloseHandle(file);
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)header;
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew+sizeof(IMAGE_NT_HEADERS32)>sizeof header) return 1;
    IMAGE_NT_HEADERS32 *pe=(IMAGE_NT_HEADERS32 *)(header+dos->e_lfanew);
    void *reserved=VirtualAlloc((void *)(uintptr_t)pe->OptionalHeader.ImageBase,pe->OptionalHeader.SizeOfImage,MEM_RESERVE,PAGE_NOACCESS);
    HMODULE asi=LoadLibraryW(argv[1]);
    if (!asi) {printf("统一ASI加载失败：%lu\n",GetLastError());return 1;}
    FARPROC entry=GetProcAddress(asi,"InitializeASI");
    if (!entry) return 1;
    ((void (__cdecl *)(void))entry)();
    if (reserved && (uintptr_t)asi==pe->OptionalHeader.ImageBase) return 1;
    FreeLibrary(asi);if(reserved)VirtualFree(reserved,0,MEM_RELEASE);
    puts("统一ASI在无外置SDL DLL的非游戏进程加载、入口和重定位检查通过");
    return 0;
}
