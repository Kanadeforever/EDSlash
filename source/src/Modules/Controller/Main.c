#include "Plugin.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

WCHAR g_directory[MAX_PATH];
HWND g_window;
PadInput g_input;
Intent g_intent;
static ControlState control;
static FILE *log_file;
static LONG initialized;
static BOOL (WINAPI *original_keyboard)(PBYTE);
static SHORT (WINAPI *original_async)(int);
static BYTE saved_call[5];
static bool installed;
static DWORD game_thread;
static HINSTANCE self_module;
static int runtime_state;
static void initialize_runtime(void);
static WNDPROC previous_window_proc;
static HWND hooked_window;

static LRESULT CALLBACK window_hook(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if ((message == WM_ACTIVATEAPP && !wp) || message == WM_KILLFOCUS) {
        /* 游戏失焦后可能不再轮询键盘，不能等下一帧才松开插件按下的鼠标按钮。 */
        g_input.focused = false;
        control.ready = false; control.running = false; control.start_pending = false;
        control.previous_layer = LAYER_NONE;
        memset(&g_intent, 0, sizeof g_intent);
        Game_Release(); Input_ReleaseMouse(); Input_Rumble(0);
    }
    LRESULT result = CallWindowProcW(previous_window_proc, window, message, wp, lp);
    if (message == WM_NCDESTROY) {
        /* 返回标题或切换窗口模式若重建 HWND，下一帧允许为新主窗口重新安装。 */
        hooked_window = NULL; previous_window_proc = NULL;
    }
    return result;
}

void Log_Write(const char *format, ...)
{
    if (!log_file) return;
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    fputs("\n", log_file);
    fflush(log_file);
}

bool Memory_Readable(const void *pointer, size_t bytes)
{
    /* 游戏切图会销毁对象。读前检查整个范围，拒绝空指针、保护页和跨出已提交区的访问。 */
    MEMORY_BASIC_INFORMATION info;
    uintptr_t start = (uintptr_t)pointer;
    if (!pointer || !bytes || start + bytes < start) return false;
    if (!VirtualQuery(pointer, &info, sizeof info) || info.State != MEM_COMMIT) return false;
    if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return start + bytes <= (uintptr_t)info.BaseAddress + info.RegionSize;
}
uint32_t Read32(const void *p, unsigned offset)
{
    uint32_t result = 0;
    const BYTE *address = p ? (const BYTE *)p + offset : NULL;
    if (Memory_Readable(address, 4)) memcpy(&result, address, 4);
    return result;
}
void *ReadPtr(const void *p, unsigned offset) { return (void *)(uintptr_t)Read32(p, offset); }
void Write32(void *p, unsigned offset, uint32_t value)
{
    /* 只对已确认的可写游戏对象使用；代码补丁走另外的 VirtualProtect 流程。 */
    memcpy((BYTE *)p + offset, &value, 4);
}

static bool patch(void *where, const void *data, size_t count)
{
    DWORD old, ignored;
    if (!VirtualProtect(where, count, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(where, data, count);
    FlushInstructionCache(GetCurrentProcess(), where, count);
    VirtualProtect(where, count, old, &ignored);
    return true;
}

static BOOL WINAPI keyboard_hook(PBYTE keys)
{
    BOOL result = original_keyboard(keys);
    if (!result || (uintptr_t)keys != g_profile->keyboard_buffer) return result;
    /* 仅游戏自身的键盘帧调用推进插件。SDL 内部若读取键盘，不会形成递归采样。 */
    if (!game_thread) game_thread = GetCurrentThreadId();
    if (game_thread != GetCurrentThreadId()) return result;
    if (!runtime_state) initialize_runtime();
    if (runtime_state != 1) return result;
    memset(&g_input, 0, sizeof g_input);
    g_input.now = GetTickCount();
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    g_input.focused = pid == GetCurrentProcessId();
    if (g_input.focused) {
        g_window = foreground;
        if (!hooked_window && GetWindowThreadProcessId(foreground, NULL) == game_thread) {
            SetLastError(0);
            WNDPROC old = (WNDPROC)SetWindowLongPtrW(foreground,GWLP_WNDPROC,(LONG_PTR)window_hook);
            if (old) { previous_window_proc = old; hooked_window = foreground; }
            else Log_Write("[窗口] 未安装失焦清理入口，错误码=%lu。",GetLastError());
        }
    }
    g_input.menu = Game_Menu();
    g_input.connected = Input_Poll(&g_input);
    g_intent = Control_Step(&control, &g_input);
    Game_Diagnose();
    if (g_intent.reset) { Game_Release(); Input_ReleaseMouse(); }
    if (g_intent.mode_changed) {
        Input_Rumble(g_intent.rumble_ms);
        Log_Write("[模式] 已切换为%s，震动 %u 毫秒。", control.mouse ? "鼠标模式" : "手柄模式", g_intent.rumble_ms);
    }
    Input_Mouse(g_intent.layer == LAYER_MOUSE && !g_intent.mode_changed);
    Game_Keyboard(keys);
    return result;
}

static SHORT WINAPI async_hook(int key)
{
    SHORT native = original_async(key);
    if (game_thread != GetCurrentThreadId()) return native;
    return Game_Async(key, native);
}

static void __attribute__((fastcall)) resolver_hook(void *mouse, void *unused)
{
    (void)unused;
    /* 先让原版解析新一帧场景和玩家，再提交控制器动作，避免拿上一张地图的 Role。 */
    ((This0)g_profile->resolver)(mouse);
    Game_Update();
}

static void initialize_runtime(void)
{
    /* 此时已经离开 DllMain 的 Loader 锁，允许文件散列、配置与 SDL 动态加载。
       先置失败状态；只有所有验证和安装成功才改成可运行，递归也不会重复初始化。 */
    runtime_state = -1;
    DWORD length = GetModuleFileNameW(self_module, g_directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    WCHAR *slash = wcsrchr(g_directory, L'\\');
    if (!slash) return;
    slash[1] = 0;
    WCHAR path[MAX_PATH];
    if (wcslen(g_directory) + 24 >= MAX_PATH) return;
    swprintf(path, MAX_PATH, L"%lsEDSlashController.log", g_directory);
    /* 每次启动覆盖旧日志，符合实机比较需要；不把历史记录追加成一大份混合日志。 */
    log_file = _wfopen(path, L"w");
    Log_Write("EDSlashController v0.1-dev2：调查与原生操作修正版");
    const Profile *candidate = g_profile;
    if (!Profile_Select() || candidate != g_profile || !Profile_Verify()) {
        Log_Write("[停止] 基线或机器码检查失败，撤回采样入口，未启用游戏动作。");
        patch((void *)candidate->keyboard_iat, &original_keyboard, 4);
        return;
    }
    original_async = *(void **)g_profile->async_iat;
    memcpy(saved_call, (void *)g_profile->resolver_call, 5);
    BYTE replacement[5] = {0xE8};
    intptr_t relative = (intptr_t)resolver_hook - (intptr_t)g_profile->resolver_call - 5;
    memcpy(replacement + 1, &relative, 4);
    void *async = async_hook;
    /* 任一步失败都撤回前面已经写入的槽，不能留下半套输入桥。 */
    if (!patch((void *)g_profile->resolver_call, replacement, 5)) {
        patch((void *)g_profile->keyboard_iat, &original_keyboard, 4);
        Log_Write("[停止] 无法安装动作阶段，已撤回采样入口。"); return;
    }
    if (!patch((void *)g_profile->async_iat, &async, 4)) {
        patch((void *)g_profile->keyboard_iat, &original_keyboard, 4);
        patch((void *)g_profile->resolver_call, saved_call, 5); return;
    }
    installed = true;
    runtime_state = 1;
    Log_Write("[启动] %s；输入采样和原版目标解析后阶段已安装。SDL 将在游戏输入线程初始化。", g_profile->name);
}

__declspec(dllexport) void InitializeASI(void)
{
    /* 常见 ASI Loader 会在 DllMain 之后再调本入口，一次性标记保证不重复安装。 */
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        self_module = module;
        /* 启动时只桥接一处键盘 IAT；不创建线程、不初始化 SDL、不加载加密提供者。
           首次游戏输入帧再完成初始化。两个非 Steam Profile 均走同一条路径。 */
        if (!InterlockedCompareExchange(&initialized, 1, 0) && Profile_Pe() && Profile_Verify()) {
            original_keyboard = *(void **)g_profile->keyboard_iat;
            void *hook = keyboard_hook;
            patch((void *)g_profile->keyboard_iat, &hook, 4);
        }
    } else if (reason == DLL_PROCESS_DETACH && !reserved && g_profile && original_keyboard) {
        if (hooked_window && (WNDPROC)GetWindowLongPtrW(hooked_window,GWLP_WNDPROC)==window_hook)
            SetWindowLongPtrW(hooked_window,GWLP_WNDPROC,(LONG_PTR)previous_window_proc);
        /* 不支持运行中反复热装卸；正常显式卸载时只撤销仍归本插件拥有的补丁。 */
        if (*(void **)g_profile->keyboard_iat == (void *)keyboard_hook)
            patch((void *)g_profile->keyboard_iat, &original_keyboard, 4);
        if (installed && *(void **)g_profile->async_iat == (void *)async_hook)
            patch((void *)g_profile->async_iat, &original_async, 4);
        if (installed) {
            int32_t displacement; memcpy(&displacement, (BYTE *)g_profile->resolver_call + 1, 4);
            if ((uintptr_t)(g_profile->resolver_call + 5 + displacement) == (uintptr_t)resolver_hook)
                patch((void *)g_profile->resolver_call, saved_call, 5);
        }
    }
    return TRUE;
}
