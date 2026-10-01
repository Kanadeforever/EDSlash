#include "Plugin.h"
#include "Combat.h"
#include "Guard.h"
#include "Feedback.h"
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
static BYTE saved_history_call[5];
static BYTE saved_retry_call[5];
static BYTE saved_end_call[5];
static bool installed;
static DWORD game_thread;
static HINSTANCE self_module;
static int runtime_state;
static void initialize_runtime(void);
static WNDPROC previous_window_proc;
static HWND hooked_window;
static bool native_control;
static PadInput previous_pad;
static PadInput native_pad_anchor;
static unsigned previous_mouse_buttons;
static LPARAM previous_mouse_position;
static bool mouse_position_known;

static void use_physical_mouse(void)
{
    if (runtime_state!=1 || control.mouse || native_control) return;
    Combat_ExportHistory();
    native_pad_anchor=previous_pad;
    Game_Release();Combat_Suspend();
    native_control=true;
    memset(&g_intent,0,sizeof g_intent);g_intent.layer=LAYER_NATIVE;
    Log_Write("[输入来源] 物理鼠标接管；恢复原版鼠标解析、重试和动作历史。");
}

static LRESULT CALLBACK window_hook(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    /* 用户的实际鼠标操作可接管，不要求进入手柄虚拟鼠标救援模式。
     * 救援模式中 SetCursorPos/SendInput 属于插件自身，绝不能反向认成物理来源。 */
    if (message==WM_MOUSEMOVE) {
        if (mouse_position_known && lp!=previous_mouse_position) use_physical_mouse();
        previous_mouse_position=lp;mouse_position_known=true;
    } else if (message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN ||
               message==WM_MBUTTONDOWN || message==WM_MOUSEWHEEL ||
               ((message==WM_KEYDOWN || message==WM_SYSKEYDOWN) &&
                (wp==VK_MENU || wp==VK_SPACE))) use_physical_mouse();
    if ((message == WM_ACTIVATEAPP && !wp) || message == WM_KILLFOCUS) {
        /* 游戏失焦后可能不再轮询键盘，不能等下一帧才松开插件按下的鼠标按钮。 */
        g_input.focused = false;
        control.ready = false; control.running = false; control.start_pending = false;
        control.previous_layer = LAYER_NONE;
        memset(&g_intent, 0, sizeof g_intent);
        Game_Release(); Input_ReleaseMouse(); Input_Rumble(0);
        Combat_Reset();
    }
    LRESULT result = CallWindowProcW(previous_window_proc, window, message, wp, lp);
    if (message == WM_NCDESTROY) {
        /* 返回标题或切换窗口模式若重建 HWND，下一帧允许为新主窗口重新安装。 */
        hooked_window = NULL; previous_window_proc = NULL;
        mouse_position_known=false;
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

bool Memory_Patch(void *where,const void *data,size_t count)
{
    return patch(where,data,count);
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
    /* 鼠标事件可能尚未派发到窗口，补查真实按键的新边沿；稳定按住不反复争抢。 */
    unsigned mouse_buttons=(Input_PhysicalDown(VK_LBUTTON) ? 1u:0u) |
        (Input_PhysicalDown(VK_RBUTTON) ? 2u:0u) | (Input_PhysicalDown(VK_MBUTTON) ? 4u:0u);
    if (mouse_buttons & ~previous_mouse_buttons) use_physical_mouse();
    previous_mouse_buttons=mouse_buttons;
    bool fresh_pad=Control_FreshInput(&g_input,&previous_pad);
    /* 物理鼠标接管时冻结摇杆基准，缓慢改变已按住摇杆的方向也能重新接回手柄。
     * 新按键边沿仍用相邻帧判断，不能因基准中曾按住同键而漏掉重新按下。 */
    if (native_control && Control_FreshInput(&g_input,&native_pad_anchor)) fresh_pad=true;
    previous_pad=g_input;
    g_intent = Control_Step(&control, &g_input);
    if (native_control && fresh_pad && !control.mouse) {
        Combat_ImportHistory();native_control=false;
        Log_Write("[输入来源] 新的手柄操作接管；恢复独立手柄动作解析。");
    }
    if (native_control && !control.mouse && !g_intent.mode_changed) {
        memset(&g_intent,0,sizeof g_intent);g_intent.layer=LAYER_NATIVE;
    }
    if (g_intent.mode_changed) {
        native_control=false;
        /* 救援模式最后一条程序生成的鼠标移动可能还在消息队列中，重建位置基准。 */
        mouse_position_known=false;
    }
    Game_Diagnose();
    if (g_intent.reset) { Game_Release(); Input_ReleaseMouse(); }
    if (g_intent.mode_changed || !g_input.connected || !g_input.focused) Combat_Reset();
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

static void __attribute__((fastcall)) history_hook(void *original,void *unused,int selector,int direction,int extra)
{
    (void)unused;
    /* 调用点位于原生 Runtime 建立成功之后，已经保留自动续段历史门及受控角色检查。
       普通手柄只写自己的历史；鼠标来源继续走原记录函数，不伪造鼠标左右键标志。 */
    if (Combat_OwnsHistory()) Combat_RecordExtra(selector,direction,extra);
    else ((This3)g_profile->history_record)(original,selector,direction,(void *)(intptr_t)extra);
}

static int __attribute__((fastcall)) retry_hook(void *original,void *unused,int selector,int policy,void *target)
{
    (void)unused;
    /* 普通手柄不使用原版鼠标缓冲。保留原函数周围的计数、超时和其它维护，
       只阻断这一次鼠标来源的重试；明确切入鼠标模式后完整恢复原调用。 */
    if (!Combat_AllowsMouseRetry()) return 0;
    return ((This3)g_profile->skill_release)(original,selector,policy,target);
}

static void __attribute__((fastcall)) end_hook(void *original,void *unused)
{
    (void)unused;
    /* 此调用位于原生当前玩家 Runtime 结束链，角色活动指针已经清空。
       只更新时间标记，不取消动作、不补发按键，保留自动续段的原生顺序。 */
    if (Combat_OwnsHistory()) Combat_End();
    else ((This0)g_profile->end_record)(original);
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
    Log_Write("EDSlashController v0.1-dev8：最大体力百分比、方向闪避、动作图标与必杀技测试版");
    const Profile *candidate = g_profile;
    if (!Profile_Select() || candidate != g_profile || !Profile_Verify()) {
        Log_Write("[停止] 基线或机器码检查失败，撤回采样入口，未启用游戏动作。");
        patch((void *)candidate->keyboard_iat, &original_keyboard, 4);
        return;
    }
    original_async = *(void **)g_profile->async_iat;
    memcpy(saved_call, (void *)g_profile->resolver_call, 5);
    memcpy(saved_history_call,(void *)g_profile->history_call,5);
    memcpy(saved_retry_call,(void *)g_profile->retry_call,5);
    memcpy(saved_end_call,(void *)g_profile->end_call,5);
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
    BYTE history_replacement[5]={0xE8};
    intptr_t history_relative=(intptr_t)history_hook-(intptr_t)g_profile->history_call-5;
    memcpy(history_replacement+1,&history_relative,4);
    if (!patch((void *)g_profile->history_call,history_replacement,5)) {
        patch((void *)g_profile->async_iat,&original_async,4);
        patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        patch((void *)g_profile->resolver_call,saved_call,5);return;
    }
    BYTE retry_replacement[5]={0xE8};
    intptr_t retry_relative=(intptr_t)retry_hook-(intptr_t)g_profile->retry_call-5;
    memcpy(retry_replacement+1,&retry_relative,4);
    if (!patch((void *)g_profile->retry_call,retry_replacement,5)) {
        patch((void *)g_profile->history_call,saved_history_call,5);
        patch((void *)g_profile->async_iat,&original_async,4);
        patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        patch((void *)g_profile->resolver_call,saved_call,5);return;
    }
    BYTE end_replacement[5]={0xE8};
    intptr_t end_relative=(intptr_t)end_hook-(intptr_t)g_profile->end_call-5;
    memcpy(end_replacement+1,&end_relative,4);
    if (!patch((void *)g_profile->end_call,end_replacement,5)) {
        patch((void *)g_profile->retry_call,saved_retry_call,5);
        patch((void *)g_profile->history_call,saved_history_call,5);
        patch((void *)g_profile->async_iat,&original_async,4);
        patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        patch((void *)g_profile->resolver_call,saved_call,5);return;
    }
    if (!Guard_Initialize() || !Feedback_Initialize()) {
        Guard_Shutdown();Feedback_Shutdown();
        patch((void *)g_profile->end_call,saved_end_call,5);
        patch((void *)g_profile->retry_call,saved_retry_call,5);
        patch((void *)g_profile->history_call,saved_history_call,5);
        patch((void *)g_profile->async_iat,&original_async,4);
        patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        patch((void *)g_profile->resolver_call,saved_call,5);
        Log_Write("[停止] 防御/闪避入口未通过安装，已撤回手柄动作阶段。");return;
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
        Feedback_Shutdown();Guard_Shutdown();
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
            memcpy(&displacement,(BYTE *)g_profile->history_call+1,4);
            if ((uintptr_t)(g_profile->history_call+5+displacement)==(uintptr_t)history_hook)
                patch((void *)g_profile->history_call,saved_history_call,5);
            memcpy(&displacement,(BYTE *)g_profile->retry_call+1,4);
            if ((uintptr_t)(g_profile->retry_call+5+displacement)==(uintptr_t)retry_hook)
                patch((void *)g_profile->retry_call,saved_retry_call,5);
            memcpy(&displacement,(BYTE *)g_profile->end_call+1,4);
            if ((uintptr_t)(g_profile->end_call+5+displacement)==(uintptr_t)end_hook)
                patch((void *)g_profile->end_call,saved_end_call,5);
        }
    }
    return TRUE;
}
