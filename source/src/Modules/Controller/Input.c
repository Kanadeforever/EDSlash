#include "Plugin.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* SDL3 只通过公开的 C ABI 动态加载。SDL bool 是 C bool，不能误声明成 Win32 BOOL。
   这些签名与 SDL3 官方 API 对照，构建不依赖本机安装 SDL SDK。 */
typedef struct SDL_Gamepad SDL_Gamepad;
static struct {
    void (*SetMainReady)(void);
    bool (*InitSubSystem)(uint32_t);
    void (*UpdateGamepads)(void);
    uint32_t *(*GetGamepads)(int *);
    SDL_Gamepad *(*OpenGamepad)(uint32_t);
    bool (*GamepadConnected)(SDL_Gamepad *);
    bool (*GetGamepadButton)(SDL_Gamepad *, int);
    int16_t (*GetGamepadAxis)(SDL_Gamepad *, int);
    void (*CloseGamepad)(SDL_Gamepad *);
    bool (*RumbleGamepad)(SDL_Gamepad *, uint16_t, uint16_t, uint32_t);
    const char *(*GetError)(void);
    void (*Free)(void *);
} sdl;
static SDL_Gamepad *pad;
static int init_state, deadzone = 8000, mouse_speed = 900;
static uint32_t last_find, last_mouse;
static float remainder_x, remainder_y;
static bool mouse_left, mouse_right;

bool Input_PhysicalDown(int key)
{
    /* 使用本插件自己的 USER32 导入，读取真实状态，避免把自己叠加的虚拟 Alt 当成真实按键。 */
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

int Config_Number(const WCHAR *section, const WCHAR *key, int fallback, int minimum, int maximum)
{
    WCHAR path[MAX_PATH];
    if (wcslen(g_directory) + 24 >= MAX_PATH) return fallback;
    swprintf(path, MAX_PATH, L"%lsEDSlashController.ini", g_directory);
    int value = (int)GetPrivateProfileIntW(section, key, fallback, path);
    /* 配置越界时钳制，避免 100% 死区造成除零或极端速度让鼠标飞出屏幕。 */
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return value;
}

static bool load_sdl(void)
{
    WCHAR path[MAX_PATH];
    if (wcslen(g_directory) + 9 >= MAX_PATH) return false;
    swprintf(path, MAX_PATH, L"%lsSDL3.dll", g_directory);
    /* 使用插件旁的明确路径，不从 PATH 随机拿另一份 SDL。此函数只在输入线程执行。 */
    HMODULE module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        Log_Write("[SDL] 无法加载插件旁的 32 位 SDL3.dll，错误码=%lu。", GetLastError()); return false;
    }
#define LOAD(field, symbol) do { \
        FARPROC proc = GetProcAddress(module, symbol); \
        if (!proc) { Log_Write("[SDL] 缺少导出：%s", symbol); return false; } \
        memcpy(&sdl.field, &proc, sizeof proc); \
    } while (0)
    LOAD(SetMainReady, "SDL_SetMainReady");
    LOAD(InitSubSystem, "SDL_InitSubSystem");
    LOAD(UpdateGamepads, "SDL_UpdateGamepads");
    LOAD(GetGamepads, "SDL_GetGamepads");
    LOAD(OpenGamepad, "SDL_OpenGamepad");
    LOAD(GamepadConnected, "SDL_GamepadConnected");
    LOAD(GetGamepadButton, "SDL_GetGamepadButton");
    LOAD(GetGamepadAxis, "SDL_GetGamepadAxis");
    LOAD(CloseGamepad, "SDL_CloseGamepad");
    LOAD(RumbleGamepad, "SDL_RumbleGamepad");
    LOAD(GetError, "SDL_GetError");
    LOAD(Free, "SDL_free");
#undef LOAD
    sdl.SetMainReady();
    /* 0x2000 是 SDL_INIT_GAMEPAD，只启用手柄，不接管游戏的视频或音频系统。 */
    if (!sdl.InitSubSystem(0x2000u)) {
        Log_Write("[SDL] 手柄子系统初始化失败：%s", sdl.GetError()); return false;
    }
    deadzone = Config_Number(L"Input", L"Deadzone", 8000, 1000, 24000);
    mouse_speed = Config_Number(L"Mouse", L"Speed", 900, 100, 3000);
    Log_Write("[SDL] 游戏输入线程初始化成功；等待手柄，死区=%d。", deadzone);
    return true;
}

bool Input_Poll(PadInput *input)
{
    if (!init_state) init_state = load_sdl() ? 1 : -1;
    if (init_state < 0) return false;
    /* 手动更新状态，不从 SDL 事件队列取走可能属于其它模块的事件。 */
    sdl.UpdateGamepads();
    if (pad && !sdl.GamepadConnected(pad)) {
        sdl.CloseGamepad(pad); pad = NULL;
        Log_Write("[手柄] 已断开，释放插件拥有的输入。");
    }
    if (!pad && (!last_find || input->now - last_find >= 1000)) {
        last_find = input->now;
        int count = 0;
        uint32_t *ids = sdl.GetGamepads(&count);
        if (ids) {
            for (int i = 0; i < count && !pad; ++i) pad = sdl.OpenGamepad(ids[i]);
            sdl.Free(ids);
        }
        if (pad) Log_Write("[手柄] 已连接；请松开摇杆、扳机和按键后开始操作。");
    }
    if (!pad) return false;
    for (int i = 0; i <= PAD_RIGHT; ++i)
        if (sdl.GetGamepadButton(pad, i)) input->buttons |= KEY(i);
    Control_Stick(sdl.GetGamepadAxis(pad,0),sdl.GetGamepadAxis(pad,1),deadzone,&input->lx,&input->ly);
    input->rx = Control_Axis(sdl.GetGamepadAxis(pad, 2), deadzone);
    input->ry = Control_Axis(sdl.GetGamepadAxis(pad, 3), deadzone);
    input->lt = sdl.GetGamepadAxis(pad, 4) >= 8000;
    input->rt = sdl.GetGamepadAxis(pad, 5) >= 8000;
    return true;
}

void Input_Rumble(unsigned ms)
{
    if (pad && Config_Number(L"Feedback", L"Enable", 1, 0, 1)) {
        if (!sdl.RumbleGamepad(pad, ms ? 18000 : 0, ms ? 18000 : 0, ms))
            Log_Write("[震动] 当前设备未接受震动：%s", sdl.GetError());
    }
}

static void mouse_button(bool *owned, bool wanted, DWORD down, DWORD up)
{
    if (*owned == wanted) return;
    INPUT event;
    memset(&event, 0, sizeof event);
    event.type = INPUT_MOUSE;
    event.mi.dwFlags = wanted ? down : up;
    if (SendInput(1, &event, sizeof event) == 1) *owned = wanted;
}

void Input_ReleaseMouse(void)
{
    /* 只发本插件按下过的按钮的松开事件，不释放玩家自己的鼠标按钮。 */
    mouse_button(&mouse_left, false, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
    mouse_button(&mouse_right, false, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
    remainder_x = remainder_y = 0;
    last_mouse = 0;
}

void Input_Mouse(bool enabled)
{
    if (!enabled || !g_input.focused || !g_input.connected) { Input_ReleaseMouse(); return; }
    uint32_t elapsed = last_mouse ? g_input.now - last_mouse : 0;
    last_mouse = g_input.now;
    if (elapsed > 50) elapsed = 50;
    /* 右摇杆固定是左摇杆速度的三分之一；保留小数余量，使低速微调不会被整数截断吃掉。 */
    remainder_x += (g_input.lx + g_input.rx / 3.0f) * mouse_speed * elapsed / 1000.0f;
    remainder_y += (g_input.ly + g_input.ry / 3.0f) * mouse_speed * elapsed / 1000.0f;
    int dx = (int)remainder_x, dy = (int)remainder_y;
    remainder_x -= dx; remainder_y -= dy;
    if (dx || dy) {
        POINT point; RECT bounds;
        if (GetCursorPos(&point) && GetClientRect(g_window, &bounds)) {
            POINT origin = {0, 0}; ClientToScreen(g_window, &origin);
            point.x += dx; point.y += dy;
            if (point.x < origin.x) point.x = origin.x;
            if (point.y < origin.y) point.y = origin.y;
            if (point.x >= origin.x + bounds.right) point.x = origin.x + bounds.right - 1;
            if (point.y >= origin.y + bounds.bottom) point.y = origin.y + bounds.bottom - 1;
            SetCursorPos(point.x, point.y);
        }
    }
    mouse_button(&mouse_left, g_input.rt, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
    mouse_button(&mouse_right, g_input.lt, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
}
