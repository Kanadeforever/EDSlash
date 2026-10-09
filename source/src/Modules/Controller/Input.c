#include "ControllerText.h"
#include "Plugin.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

/* SDL由官方完整源码静态链接，签名来自真实头文件，不再加载SDL3.dll。 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "../../Runtime/Config.h"
#include "../../Runtime/FileIO.h"
#include "../../Runtime/Perf.h"
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

bool Input_Initialize(void)
{
    if (init_state) return init_state==1;
    /* 这里只由Loader锁外的游戏输入线程调用。只初始化输入子系统，不接管音视频。 */
    SDL_SetMainReady();
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        init_state=-1;Log_Write(ControllerText_Sdl_InitializationFailedLog,SDL_GetError());return false;
    }
    init_state=1;
    WCHAR path[MAX_PATH];
    int length=swprintf(path,MAX_PATH,L"%lsgamecontrollerdb.txt",g_directory);
    if (length>0 && length<MAX_PATH) {
        DWORD attributes=GetFileAttributesW(path);
        if (attributes==INVALID_FILE_ATTRIBUTES) {
            Log_Write(ControllerText_Sdl_ExternalMappingsMissingLog);
        } else if (!(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
            char utf8[MAX_PATH*4];
            /* SDL文件接口使用UTF-8，中文目录必须转换成UTF-8，不能用系统ANSI代替。 */
            if (WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,utf8,sizeof utf8,NULL,NULL)) {
                int count=SDL_AddGamepadMappingsFromFile(utf8);
                if (count<0) Log_Write(ControllerText_Sdl_ExternalMappingsLoadFailedLog,SDL_GetError());
                else Log_Write(ControllerText_Sdl_ExternalMappingsLoadedLog,count);
            } else Log_Write(ControllerText_Sdl_MappingPathConversionFailedLog);
        } else Log_Write(ControllerText_Sdl_MappingPathIsDirectoryLog);
    }
    Log_Write(ControllerText_Sdl_InputSubsystemReadyLog,SDL_MAJOR_VERSION,SDL_MINOR_VERSION,SDL_MICRO_VERSION);
    return true;
}
void Input_Shutdown(void)
{
    /* 只关闭本模块打开的手柄和取得的子系统引用，不调用全局SDL_Quit。 */
    Input_ReleaseMouse();
    if (pad) {SDL_RumbleGamepad(pad,0,0,0);SDL_CloseGamepad(pad);pad=NULL;}
    if (init_state==1) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    init_state=0;last_find=0;
}

static bool poll_input(PadInput *input)
{
    if (!init_state) (void)Input_Initialize();
    if (init_state < 0) return false;
    deadzone=RuntimeConfig_GetInt(CONFIG_DEADZONE);
    mouse_speed=RuntimeConfig_GetInt(CONFIG_MOUSE_SPEED);
    /* 手动更新状态，不从 SDL 事件队列取走可能属于其它模块的事件。 */
    SDL_UpdateGamepads();
    if (pad && !SDL_GamepadConnected(pad)) {
        SDL_CloseGamepad(pad); pad = NULL;
        Log_Write(ControllerText_Device_DisconnectedLog);
    }
    if (!pad && (!last_find || input->now - last_find >= 1000)) {
        last_find = input->now;
        int count = 0;
        uint32_t *ids = SDL_GetGamepads(&count);
        if (ids) {
            for (int i = 0; i < count && !pad; ++i) pad = SDL_OpenGamepad(ids[i]);
            SDL_free(ids);
        }
        if (pad) Log_Write(ControllerText_Device_ConnectedReleaseInputsLog);
    }
    if (!pad) return false;
    for (int i = 0; i <= PAD_RIGHT; ++i)
        if (SDL_GetGamepadButton(pad, i)) input->buttons |= KEY(i);
    Control_Stick(SDL_GetGamepadAxis(pad,0),SDL_GetGamepadAxis(pad,1),deadzone,&input->lx,&input->ly);
    input->rx = Control_Axis(SDL_GetGamepadAxis(pad, 2), deadzone);
    input->ry = Control_Axis(SDL_GetGamepadAxis(pad, 3), deadzone);
    input->lt = SDL_GetGamepadAxis(pad, 4) >= 8000;
    input->rt = SDL_GetGamepadAxis(pad, 5) >= 8000;
    return true;
}

bool Input_Poll(PadInput *input)
{
    int64_t perf=RuntimePerf_Begin();bool connected=poll_input(input);
    RuntimePerf_End(PERF_SDL,perf);return connected;
}

void Input_Rumble(unsigned ms)
{
    if (pad && RuntimeConfig_GetInt(CONFIG_RUMBLE)) {
        if (!SDL_RumbleGamepad(pad, ms ? 18000 : 0, ms ? 18000 : 0, ms))
            Log_Write(ControllerText_Device_RumbleRejectedLog, SDL_GetError());
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
