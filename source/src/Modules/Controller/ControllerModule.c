#include "ControllerText.h"
#include "Plugin.h"
#include "ControllerModule.h"
#include "../../Runtime/Log.h"
#include "../../Runtime/Perf.h"
#include "../../Runtime/Win32Bridge.h"
#include "../../Runtime/SettingsWindow.h"
#include "Combat.h"
#include "Guard.h"
#include "Feedback.h"
#include "Menu.h"
#include "Cursor.h"
#include "Inspect.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

WCHAR g_directory[MAX_PATH];
HWND g_window;
PadInput g_input;
Intent g_intent;
static ControlState control;
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
int ControllerModule_IsReady(void) { return runtime_state == 1; }
static void initialize_runtime(void);
static WNDPROC previous_window_proc;
static HWND hooked_window;
static bool native_control;
static PadInput previous_pad;
static PadInput native_pad_anchor;
static unsigned previous_mouse_buttons;
static LPARAM previous_mouse_position;
static bool mouse_position_known,settings_capture_seen;

static void use_physical_mouse(void)
{
    if (SettingsWindow_Active() || runtime_state!=1 || control.mouse || native_control) return;
    Combat_ExportHistory();
    native_pad_anchor=previous_pad;
    ActionMenu_Suspend();Menu_Suspend();Game_Release();Combat_Suspend();
    native_control=true;
    memset(&g_intent,0,sizeof g_intent);g_intent.layer=LAYER_NATIVE;
    Log_Write(ControllerText_InputSource_MouseTakeoverLog);
}

static LRESULT CALLBACK window_hook(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if(message==WM_MOUSEWHEEL && SettingsWindow_Wheel((short)(wp>>16)))return 0;
    /* 用户的实际鼠标操作可接管，不要求进入手柄虚拟鼠标救援模式。
     * 救援模式中 SetCursorPos/SendInput 属于插件自身，绝不能反向认成物理来源。 */
    if (message==WM_MOUSEMOVE) {
        if (mouse_position_known && lp!=previous_mouse_position) use_physical_mouse();
        previous_mouse_position=lp;mouse_position_known=true;
    } else if (message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN ||
               message==WM_MBUTTONDOWN || message==WM_MOUSEWHEEL ||
               ((message==WM_KEYDOWN || message==WM_SYSKEYDOWN) && !(lp & (1L<<30)))) use_physical_mouse();
    if ((message == WM_ACTIVATEAPP && !wp) || message == WM_KILLFOCUS) {
        /* 游戏失焦后可能不再轮询键盘，不能等下一帧才松开插件按下的鼠标按钮。 */
        g_input.focused = false;
        control.ready = false; control.running = false; control.start_pending = false;
        control.previous_layer = LAYER_NONE;
        memset(&g_intent, 0, sizeof g_intent);
        Game_Release(); Input_ReleaseMouse(); Input_Rumble(0);
        Combat_Reset();ActionMenu_Suspend();Menu_Suspend();
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
    va_list args;va_start(args,format);
    RuntimeLog_VWrite(format,args);va_end(args);
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
    int result=RuntimeWin32_WriteCode((unsigned long)(uintptr_t)where,data,(unsigned long)count);
    if (result==2) Log_Write(ControllerText_Patch_WriteFinalizationFailedLog);
    return result!=0;
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
            else Log_Write(ControllerText_Window_FocusLossHookFailedLog,GetLastError());
        }
    }
    g_input.menu = Game_Menu();
    g_input.connected = Input_Poll(&g_input);
    /* 设置入口读取原始物理边沿，不能被世界改绑、鼠标接管或菜单AB交换抹掉。 */
    static uint32_t settings_previous_buttons;
    uint32_t settings_pressed=g_input.buttons & ~settings_previous_buttons;
    settings_previous_buttons=g_input.buttons;
    /* 安全点及模块确认由同一个生产函数协调，Guard延后接收时保留代数，下帧再确认。 */
    static unsigned guard_generation;
    Guard_SyncSettings(&g_input,&guard_generation);
    g_input.swap_menu_ab=RuntimeConfig_GetInt(CONFIG_MENU_SWAP_AB)!=0;
    static const unsigned char physical[]={PAD_A,PAD_B,PAD_X,PAD_Y,PAD_BACK,PAD_START,PAD_L3,PAD_R3};
    g_input.world_remap=true;
    for(unsigned i=0;i<7;++i)g_input.world_buttons[i]=physical[RuntimeConfig_GetInt((ConfigId)(CONFIG_WORLD_INTERACT+i))];
    Runtime_EmitEvent(RUNTIME_EVENT_INPUT_SAMPLED,keys,g_input.now,0);
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
    g_input.action_menu=ActionMenu_Active();
    g_intent = Control_Step(&control, &g_input);
    if (native_control && fresh_pad && !control.mouse) {
        Combat_ImportHistory();native_control=false;
        Log_Write(ControllerText_InputSource_ControllerTakeoverLog);
    }
    if (native_control && !control.mouse && !g_intent.mode_changed) {
        memset(&g_intent,0,sizeof g_intent);g_intent.layer=LAYER_NATIVE;
    }
    if (g_intent.mode_changed) {
        native_control=false;
        /* 救援模式最后一条程序生成的鼠标移动可能还在消息队列中，重建位置基准。 */
        mouse_position_known=false;
    }
    bool settings_was_open=SettingsWindow_Active();
    if(SettingsWindow_Pad(g_input.buttons,settings_pressed,g_input.lt,g_input.rt,g_input.lx,g_input.ly,g_input.rx,g_input.ry,g_input.now)) {
        settings_capture_seen=true;
        /* 救援鼠标模式也必须停止生成鼠标事件，否则导航摇杆同时拖动真实指针。 */
        Input_Mouse(false);Input_ReleaseMouse();
        Game_Release();Combat_Suspend();g_intent.layer=LAYER_MENU;g_intent.held=g_intent.pressed=0;
        memset(keys,0,256);return result;
    }
    if(settings_was_open || settings_capture_seen) {
        settings_capture_seen=false;
        /* 关闭当帧及后续旧持键不再走世界/原菜单；连同扳机一起等中立。 */
        control.ready=false;Game_Release();Combat_Suspend();g_intent.layer=LAYER_NONE;g_intent.held=g_intent.pressed=0;memset(keys,0,256);return result;
    }
    if(!RuntimeConfig_GetInt(CONFIG_CONTROLLER_ENABLED)) {
        Game_Release();Combat_Suspend();g_intent.layer=LAYER_NATIVE;g_intent.held=g_intent.pressed=0;
        return result; /* 设置输入已经处理；原物理输入不被关闭的手柄桥接管。 */
    }
    Game_Diagnose();
    if (g_intent.reset) { Game_Release(); Input_ReleaseMouse(); }
    if (g_intent.mode_changed || !g_input.connected || !g_input.focused) Combat_Reset();
    if (g_intent.mode_changed) {
        Input_Rumble(g_intent.rumble_ms);
        Log_Write(ControllerText_InputMode_ChangedLog, control.mouse ? ControllerText_InputMode_MouseLabel : ControllerText_InputMode_ControllerLabel, g_intent.rumble_ms);
    }
    Input_Mouse(g_intent.layer == LAYER_MOUSE && !g_intent.mode_changed);
    /* 菜单不能依赖世界/玩家就绪；在真实游戏键盘采样线程完成独立焦点与业务。 */
    bool action_was_open=ActionMenu_Active();
    bool action_closed=ActionMenu_Update();
    if (action_closed) Control_BlockMenuInputs(&control,&g_input);
    if (!action_was_open && !ActionMenu_Active() && !action_closed) Menu_Update();
    Game_Keyboard(keys);
    return result;
}

static SHORT WINAPI async_hook(int key)
{
    /* 设置窗口直接读取本ASI自己的系统入口。这里是游戏的入口，捕获时全部屏蔽，
     * 避免窗口中的Enter、Y、方向键或鼠标同时触发原游戏的控制台和动作。 */
    if(SettingsWindow_Active())return 0;
    SHORT native = original_async(key);
    if (game_thread != GetCurrentThreadId()) return native;
    return Game_Async(key, native);
}

static void __fastcall resolver_hook(void *mouse, void *unused)
{
    (void)unused;
    /* 先让原版解析新一帧场景和玩家，再提交控制器动作，避免拿上一张地图的 Role。 */
    ((This0)g_profile->resolver)(mouse, NULL);
    Runtime_EmitEvent(RUNTIME_EVENT_INPUT_RESOLVED,mouse,0,0);
    int64_t perf=RuntimePerf_Begin();
    Game_Update();
    RuntimePerf_End(PERF_GAME,perf);
}

static void __fastcall history_hook(void *original,void *unused,int selector,int direction,int extra)
{
    (void)unused;
    /* 调用点位于原生 Runtime 建立成功之后，已经保留自动续段历史门及受控角色检查。
       普通手柄只写自己的历史；鼠标来源继续走原记录函数，不伪造鼠标左右键标志。 */
    if (Combat_OwnsHistory()) Combat_RecordExtra(selector,direction,extra);
    else {
        /* 原生实际建立的新动作接管显示；单纯移动鼠标不撤掉尚在执行的手柄图标。 */
        Feedback_End();
        ((This3)g_profile->history_record)(original, NULL,selector,direction,(void *)(intptr_t)extra);
    }
}

static int __fastcall retry_hook(void *original,void *unused,int selector,int policy,void *target)
{
    (void)unused;
    /* 普通手柄不使用原版鼠标缓冲。保留原函数周围的计数、超时和其它维护，
       只阻断这一次鼠标来源的重试；明确切入鼠标模式后完整恢复原调用。 */
    if (!Combat_AllowsMouseRetry()) return 0;
    return ((This3)g_profile->skill_release)(original, NULL,selector,policy,target);
}

static void __fastcall end_hook(void *original,void *unused)
{
    (void)unused;
    /* 此调用位于原生当前玩家 Runtime 结束链，角色活动指针已经清空。
       只更新时间标记，不取消动作、不补发按键，保留自动续段的原生顺序。 */
    if (Combat_OwnsHistory()) Combat_End();
    else {
        Feedback_RuntimeEnded();
        ((This0)g_profile->end_record)(original, NULL);
    }
}

static void initialize_runtime(void)
{
    /* 此时已经离开 DllMain 的 Loader 锁，允许文件散列、配置与 SDL 初始化。
       先置失败状态；只有所有验证和安装成功才改成可运行，递归也不会重复初始化。 */
    runtime_state = -1;
    Log_Write(ControllerText_Startup_ModuleInitializingLog);
    if (!Profile_Select() || !Profile_Verify()) {
        Log_Write(ControllerText_Startup_ProfileMismatchLog);
        if (*(void **)g_profile->keyboard_iat==(void *)keyboard_hook)
            patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        Input_Shutdown();return;
    }
    /* SDL失败与没有连接手柄不同。前者不再安装动作桥，后者允许正常等待设备。
     * 初始化发生在游戏输入线程而非Loader锁；失败后Runtime可以接收idle配置。 */
    if (!Input_Initialize()) {
        if (*(void **)g_profile->keyboard_iat==(void *)keyboard_hook)
            patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        Log_Write(ControllerText_Startup_SdlInitializationFailedLog);
        Input_Shutdown();return;
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
        Log_Write(ControllerText_Startup_ActionStageInstallFailedLog); Input_Shutdown();return;
    }
    if (!patch((void *)g_profile->async_iat, &async, 4)) {
        patch((void *)g_profile->keyboard_iat, &original_keyboard, 4);
        patch((void *)g_profile->resolver_call, saved_call, 5); Input_Shutdown();return;
    }
    BYTE history_replacement[5]={0xE8};
    intptr_t history_relative=(intptr_t)history_hook-(intptr_t)g_profile->history_call-5;
    memcpy(history_replacement+1,&history_relative,4);
    if (!patch((void *)g_profile->history_call,history_replacement,5)) {
        patch((void *)g_profile->async_iat,&original_async,4);
        patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        patch((void *)g_profile->resolver_call,saved_call,5);Input_Shutdown();return;
    }
    BYTE retry_replacement[5]={0xE8};
    intptr_t retry_relative=(intptr_t)retry_hook-(intptr_t)g_profile->retry_call-5;
    memcpy(retry_replacement+1,&retry_relative,4);
    if (!patch((void *)g_profile->retry_call,retry_replacement,5)) {
        patch((void *)g_profile->history_call,saved_history_call,5);
        patch((void *)g_profile->async_iat,&original_async,4);
        patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        patch((void *)g_profile->resolver_call,saved_call,5);Input_Shutdown();return;
    }
    BYTE end_replacement[5]={0xE8};
    intptr_t end_relative=(intptr_t)end_hook-(intptr_t)g_profile->end_call-5;
    memcpy(end_replacement+1,&end_relative,4);
    if (!patch((void *)g_profile->end_call,end_replacement,5)) {
        patch((void *)g_profile->retry_call,saved_retry_call,5);
        patch((void *)g_profile->history_call,saved_history_call,5);
        patch((void *)g_profile->async_iat,&original_async,4);
        patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        patch((void *)g_profile->resolver_call,saved_call,5);Input_Shutdown();return;
    }
    if (!Guard_Initialize() || !Feedback_Initialize() || !Menu_Initialize() || !ActionMenu_Initialize() || !Cursor_Initialize() || !Inspect_Initialize()) {
        Inspect_Shutdown();Cursor_Shutdown();ActionMenu_Shutdown();Menu_Shutdown();Guard_Shutdown();Feedback_Shutdown();
        patch((void *)g_profile->end_call,saved_end_call,5);
        patch((void *)g_profile->retry_call,saved_retry_call,5);
        patch((void *)g_profile->history_call,saved_history_call,5);
        patch((void *)g_profile->async_iat,&original_async,4);
        patch((void *)g_profile->keyboard_iat,&original_keyboard,4);
        patch((void *)g_profile->resolver_call,saved_call,5);
        Log_Write(ControllerText_Startup_GameplayHooksInstallFailedLog);Input_Shutdown();return;
    }
    installed = true;
    runtime_state = 1;
    Log_Write(ControllerText_Startup_BusinessReadyLog, g_profile->name);
}

int ControllerModule_Initialize(const RuntimeContext *runtime)
{
    if (!runtime || !runtime->profile) return 0;
    if (GetModuleHandleW(L"EDSlashController.asi")) {
        Log_Write(ControllerText_Startup_LegacyAsiDetectedLog);return 0;
    }
    if (InterlockedCompareExchange(&initialized,1,0)) return installed;
    self_module=(HINSTANCE)runtime->self_module;
    DWORD length=GetModuleFileNameW(self_module,g_directory,MAX_PATH);
    WCHAR *slash=length && length<MAX_PATH ? wcsrchr(g_directory,L'\\'):NULL;
    if (!slash || !Profile_Attach(runtime->profile->game_id) || !Profile_Verify()) return 0;
    slash[1]=0;
    if (!HookManager_Claim(SHARED_HOOK_CONTROLLER_KEYBOARD,RUNTIME_MODULE_CONTROLLER) ||
        !HookManager_Claim(SHARED_HOOK_CONTROLLER_ASYNC,RUNTIME_MODULE_CONTROLLER) ||
        !HookManager_Claim(SHARED_HOOK_CONTROLLER_RESOLVER,RUNTIME_MODULE_CONTROLLER)) {
        HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);return 0;
    }
    original_keyboard=*(void **)g_profile->keyboard_iat;
    void *hook=keyboard_hook;
    if (!patch((void *)g_profile->keyboard_iat,&hook,4)) {
        HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);return 0;
    }
    Log_Write(ControllerText_Startup_SamplingBridgeInstalledLog);return 1;
}
void ControllerModule_Shutdown(void)
{
    /* 先释放设置占用的原系统菜单虚表，随后才能撤回Controller菜单包装。 */
    SettingsWindow_Close();
    /* 仅在显式卸载时撤回仍归本模块的入口；进程终止不在Loader锁内关闭SDL线程。 */
    if (!g_profile || !original_keyboard) return;
        Inspect_Shutdown();Cursor_Shutdown();ActionMenu_Shutdown();Menu_Shutdown();Feedback_Shutdown();Guard_Shutdown();
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
    Input_Shutdown();
    runtime_state=-1;
}
