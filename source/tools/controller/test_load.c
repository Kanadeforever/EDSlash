#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* 独立测试进程不是受支持游戏：加载 ASI 应安全返回，不安装游戏补丁。
   同时实际加载 32 位 SDL，核实 Windows 依赖和所用公开导出是否完整。 */
int wmain(int argc, wchar_t **argv)
{
    if (argc!=3) return 2;
    HMODULE asi=LoadLibraryW(argv[1]);
    if (!asi) { printf("ASI 加载失败：%lu\n",GetLastError());return 1; }
    FARPROC entry=GetProcAddress(asi,"InitializeASI");
    if (!entry) return 1;
    ((void (__cdecl *)(void))entry)();
    FreeLibrary(asi);
    HMODULE sdl=LoadLibraryW(argv[2]);
    if (!sdl) { printf("SDL 加载失败：%lu\n",GetLastError());return 1; }
    const char *names[]={"SDL_SetMainReady","SDL_InitSubSystem","SDL_UpdateGamepads","SDL_GetGamepads",
        "SDL_OpenGamepad","SDL_GamepadConnected","SDL_GetGamepadButton","SDL_GetGamepadAxis",
        "SDL_CloseGamepad","SDL_RumbleGamepad","SDL_GetError","SDL_free"};
    for (unsigned i=0;i<sizeof names/sizeof names[0];++i) {
        if (!GetProcAddress(sdl,names[i])) { printf("SDL 缺少导出：%s\n",names[i]);return 1; }
    }
    FreeLibrary(sdl);
    puts("32 位 ASI 非游戏进程加载与 SDL 公开导出检查通过");
    return 0;
}
