#include "../../src/Runtime/Perf.h"
#include "Plugin.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include "../../src/Runtime/FileIO.h"
/* 此测试运行实际Input.c和静态SDL，虚拟设备只提供可重复的输入，不冒充真实手柄验收。 */
WCHAR g_directory[MAX_PATH];
HWND g_window;
PadInput g_input;
static unsigned checks,rumble_calls;
static Uint16 last_low,last_high;
#define CHECK(test) do {++checks;if(!(test)){fprintf(stderr,"静态SDL检查失败，第%d行：%s；%s\n",__LINE__,#test,SDL_GetError());return 1;}}while(0)
void Log_Write(const char *format,...)
{
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');
}
static bool SDLCALL rumble(void *user,Uint16 low,Uint16 high)
{
    (void)user;++rumble_calls;last_low=low;last_high=high;return true;
}
static SDL_JoystickID attach(void)
{
    SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=6;desc.nbuttons=15;
    desc.button_mask=(1u<<15)-1;desc.axis_mask=(1u<<6)-1;
    desc.name="EDSlash测试设备";desc.vendor_id=0x1234;desc.product_id=0x4321;desc.Rumble=rumble;
    return SDL_AttachVirtualJoystick(&desc);
}
int main(void)
{
    WCHAR cwd[MAX_PATH],dir[MAX_PATH],path[MAX_PATH];
    CHECK(GetCurrentDirectoryW(MAX_PATH,cwd)>0);
    CHECK(swprintf(dir,MAX_PATH,L"%ls\\输入回归_%lu",cwd,GetCurrentProcessId())>0);
    CHECK(CreateDirectoryW(dir,NULL));
    CHECK(swprintf(g_directory,MAX_PATH,L"%ls\\",dir)>0);
    CHECK(swprintf(path,MAX_PATH,L"%lsEDSlash.toml",g_directory)>0);
    CHECK(RuntimeConfig_OpenPath(path));
    /* 测试只启用虚拟设备，避免机器上真实手柄决定枚举顺序。 */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI,"0");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED,"0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT,"0");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI,"0");
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT,"0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    CHECK(Input_Initialize());
    CHECK(SDL_GetVersion()==SDL_VERSION);
    SDL_JoystickID id=attach();CHECK(id!=0 && SDL_IsGamepad(id));
    SDL_Joystick *joystick=SDL_OpenJoystick(id);CHECK(joystick!=NULL);
    char guid_text[64];
    SDL_GUID virtual_guid=SDL_GetJoystickGUID(joystick);
    SDL_GUIDToString(virtual_guid,guid_text,sizeof guid_text);
    CHECK(strlen(guid_text)==32);
    g_input.focused=true;g_input.now=1000;
    CHECK(Input_Poll(&g_input));
    /* 所有使用的按钮逐一经真正的SDL映射与生产采样读取。 */
    for (int button=0;button<15;++button) {
        CHECK(SDL_SetJoystickVirtualButton(joystick,button,true));
        PadInput sample={.focused=true,.now=1100u+(unsigned)button};
        CHECK(Input_Poll(&sample));CHECK(sample.buttons==KEY(button));
        CHECK(SDL_SetJoystickVirtualButton(joystick,button,false));
    }
    CHECK(SDL_SetJoystickVirtualAxis(joystick,0,32767));
    CHECK(SDL_SetJoystickVirtualAxis(joystick,1,-32768));
    CHECK(SDL_SetJoystickVirtualAxis(joystick,2,-32768));
    CHECK(SDL_SetJoystickVirtualAxis(joystick,3,32767));
    CHECK(SDL_SetJoystickVirtualAxis(joystick,4,32767));
    CHECK(SDL_SetJoystickVirtualAxis(joystick,5,32767));
    PadInput sample={.focused=true,.now=1200};
    CHECK(Input_Poll(&sample));
    CHECK(sample.lx>0.7f && sample.ly<-0.7f && sample.rx==-1 && sample.ry==1 && sample.lt && sample.rt);
    Input_Rumble(200);CHECK(rumble_calls && last_low==18000 && last_high==18000);
    Input_Rumble(0);CHECK(last_low==0 && last_high==0);
    SDL_CloseJoystick(joystick);CHECK(SDL_DetachVirtualJoystick(id));
    sample=(PadInput){.focused=true,.now=2200};CHECK(!Input_Poll(&sample));
    id=attach();CHECK(id!=0);
    sample=(PadInput){.focused=true,.now=3300};CHECK(Input_Poll(&sample));
    CHECK(SDL_DetachVirtualJoystick(id));Input_Shutdown();
    CHECK(SDL_WasInit(SDL_INIT_GAMEPAD)==0);
    /* 再初始化覆盖损坏数据库，失败仍必须正常启用内置映射。 */
    WCHAR mapping[MAX_PATH];CHECK(swprintf(mapping,MAX_PATH,L"%lsgamecontrollerdb.txt",g_directory)>0);
    CHECK(RuntimeFile_WriteAtomic(mapping,"不是有效映射\r\n",strlen("不是有效映射\r\n"),1));
    CHECK(Input_Initialize());Input_Shutdown();
    /* 用同一虚拟设备GUID写一条有效Windows映射，证明明确路径读取与键位覆盖都生效。 */
    char override[768];
    int n=snprintf(override,sizeof override,
        "%s,EDSlash覆盖测试,a:b1,b:b0,x:b2,y:b3,back:b4,guide:b5,start:b6,"
        "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,"
        "dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
        "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,platform:Windows,\r\n",guid_text);
    CHECK(n>0 && n<(int)sizeof override);
    CHECK(RuntimeFile_WriteAtomic(mapping,override,(size_t)n,0));
    CHECK(Input_Initialize());
    char *effective=SDL_GetGamepadMappingForGUID(virtual_guid);
    CHECK(effective && strstr(effective,"a:b1") && strstr(effective,"platform:Windows"));
    SDL_free(effective);
    id=attach();CHECK(id!=0);
    joystick=SDL_OpenJoystick(id);CHECK(joystick!=NULL);
    CHECK(SDL_SetJoystickVirtualButton(joystick,0,true));
    PadInput remapped={.focused=true,.now=4500};
    CHECK(Input_Poll(&remapped));CHECK(remapped.buttons==KEY(PAD_B));
    SDL_CloseJoystick(joystick);CHECK(SDL_DetachVirtualJoystick(id));Input_Shutdown();
    /* 本模块只退出自己的引用，不能关闭另一个使用者已经初始化的GAMEPAD。 */
    CHECK(SDL_InitSubSystem(SDL_INIT_GAMEPAD));CHECK(Input_Initialize());Input_Shutdown();
    CHECK(SDL_WasInit(SDL_INIT_GAMEPAD)!=0);SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    CHECK(SDL_WasInit(SDL_INIT_GAMEPAD)==0);
    CHECK(SDL_WasInit(SDL_INIT_VIDEO)==0 && SDL_WasInit(SDL_INIT_AUDIO)==0);
    CHECK(DeleteFileW(mapping));CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(dir));
    printf("生产Input与静态SDL虚拟设备、中文目录、断线重连、震动检查通过：%u项\n",checks);
    return 0;
}

// 性能替身不改变生产业务或原有测试顺序。
void RuntimePerf_Initialize(void) {}
int64_t RuntimePerf_Begin(void) {return 0;}
void RuntimePerf_End(RuntimePerfId id,int64_t begin) {(void)id;(void)begin;}
void RuntimePerf_FrameEnd(void) {}
