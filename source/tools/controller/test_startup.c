/* 宿主集成：调用真实Controller延后初始化、Registry、输入帧和TOML配置。
 * 游戏入口、SDL启动和内存写入由本进程数组替代，不启动或附加游戏。 */
#include "../../src/Modules/Controller/ControllerModule.c"
#include "../../src/Runtime/InputFrame.c"
#include <wchar.h>

const Profile *g_profile;
static Profile fixture;
static void *keyboard_slot,*async_slot;
static BYTE call_slots[4][5];
static int select_ok,verify_ok,sdl_ok,patch_calls,fail_patch,display_runs,qol_runs;
static unsigned checks,frame_callbacks;
const RuntimeContext *Runtime_GetContext(void) {return 0;}
#define CHECK(x) do {++checks;if(!(x)){printf("延后初始化集成失败：%d行\n",__LINE__);return 1;}}while(0)

bool Profile_Attach(GameId game) {(void)game;return true;}
bool Profile_Select(void) {return select_ok!=0;}
bool Profile_Verify(void) {return verify_ok!=0;}
bool Input_Initialize(void) {return sdl_ok!=0;}
void Input_Shutdown(void) {}
bool Input_Poll(PadInput *p) {(void)p;return false;}
void Input_Rumble(unsigned ms) {(void)ms;}
void Input_Mouse(bool enabled) {(void)enabled;}
void Input_ReleaseMouse(void) {}
bool Input_PhysicalDown(int key) {(void)key;return false;}
int RuntimeWin32_IsReady(void) {return 1;}
int RuntimeWin32_Read(unsigned long address,void *out,unsigned long size)
{(void)address;(void)out;(void)size;return 0;}
int RuntimeWin32_WriteCode(unsigned long address,const void *bytes,unsigned long size)
{
    /* 故障只发生一次；随后恢复写入能真实验证已有槽的回滚。 */
    if (++patch_calls==fail_patch) return 0;
    memcpy((void *)address,bytes,size);return 1;
}
int X86Detour_Install(X86Detour *d,unsigned long t,unsigned long r,unsigned long n)
{(void)d;(void)t;(void)r;(void)n;return 0;}
void RuntimeLog_VWrite(const char *f,va_list args) {(void)f;(void)args;}
int RuntimeLog_Start(void) {return 1;}
void RuntimePerf_Initialize(void) {}
int64_t RuntimePerf_Begin(void) {return 0;}
void RuntimePerf_End(RuntimePerfId id,int64_t begin) {(void)id;(void)begin;}
void RuntimePerf_FrameEnd(void) {}
int Runtime_Subscribe(RuntimeEventId event,RuntimeEventCallback callback,void *user)
{return EventBus_Subscribe(event,callback,user);}
void Runtime_EmitEvent(RuntimeEventId e,void *s,unsigned long a,unsigned long b)
{EventBus_Emit(e,s,a,b);}
static void on_frame(RuntimeEventId e,void *s,unsigned long a,unsigned long b,void *u)
{(void)e;(void)s;(void)a;(void)b;(void)u;++frame_callbacks;}
int DisplayFixModule_Initialize(const RuntimeContext *r) {(void)r;++display_runs;return 1;}
int QOLModule_Initialize(const RuntimeContext *r)
{(void)r;++qol_runs;return Runtime_Subscribe(RUNTIME_EVENT_INPUT_FRAME_END,on_frame,0);}
static int __fastcall native_frame(unsigned char *keys,void *unused)
{(void)keys;(void)unused;return 7;}
static BOOL WINAPI native_keyboard(PBYTE keys) {(void)keys;return TRUE;}
static SHORT WINAPI native_async(int key) {(void)key;return 0;}

static void reset_case(void)
{
    memset(&fixture,0,sizeof fixture);g_profile=&fixture;
    keyboard_slot=(void *)keyboard_hook;async_slot=(void *)native_async;
    fixture.keyboard_iat=(uintptr_t)&keyboard_slot;fixture.async_iat=(uintptr_t)&async_slot;
    fixture.resolver_call=(uintptr_t)call_slots[0];fixture.history_call=(uintptr_t)call_slots[1];
    fixture.retry_call=(uintptr_t)call_slots[2];fixture.end_call=(uintptr_t)call_slots[3];
    memset(call_slots,0x90,sizeof call_slots);
    original_keyboard=native_keyboard;runtime_state=0;installed=false;
    select_ok=verify_ok=sdl_ok=1;patch_calls=fail_patch=0;
}

int main(void)
{
    /* Registry使用真实模块表，替身功能模块只登记成功及输入帧订阅。
     * Controller登记结果在失败后保持真，明确重现原先idle无人接收的条件。 */
    RuntimeContext context={0};
    initialized=1;installed=true;
    CHECK(!ModuleRegistry_InitializeAll(0));
    /* 配置打开后，以已经安装采样桥的状态登记；延后业务尚未开始。 */
    GameProfile game={.game_id=GAME_ID_DAOJIAN};context.profile=&game;
    wchar_t directory[MAX_PATH];GetTempPathW(MAX_PATH,directory);
    wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%lsEDSlash-startup-%lu.toml",directory,GetCurrentProcessId());
    DeleteFileW(path);CHECK(RuntimeConfig_OpenPath(path));
    CHECK(ModuleRegistry_InitializeAll(&context));
    CHECK(ModuleRegistry_IsInitialized(RUNTIME_MODULE_CONTROLLER));
    CHECK(display_runs==1 && qol_runs==1);
    original=native_frame;
    unsigned char keyboard_object[264]={0};
    for (int failure=0;failure<9;++failure) {
        reset_case();
        if(failure==0)select_ok=0;
        else if(failure==1)verify_ok=0;
        else if(failure==2)sdl_ok=0;
        else if(failure<8)fail_patch=failure-2;
        /* 最后一项所有主桥写入成功，但真实Guard因缺少合法游戏签名失败。 */
        int distance=160+failure*16;
        CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,distance));
        initialize_runtime();
        CHECK(!ControllerModule_IsReady() && runtime_state==-1);
        CHECK(ModuleRegistry_IsInitialized(RUNTIME_MODULE_CONTROLLER));
        CHECK(keyboard_slot==(void *)native_keyboard);
        CHECK(async_slot==(void *)native_async);
        for(unsigned slot=0;slot<4;++slot)for(unsigned byte=0;byte<5;++byte)
            CHECK(call_slots[slot][byte]==0x90);
        unsigned generation=RuntimeConfig_Current()->generation;
        CHECK(input_frame(keyboard_object,0)==7);
        CHECK(RuntimeConfig_GetInt(CONFIG_DODGE_DISTANCE)==distance);
        CHECK(!RuntimeConfig_HasPending() && RuntimeConfig_Current()->generation==generation+1);
        CHECK(ModuleRegistry_IsInitialized(RUNTIME_MODULE_DISPLAY_FIX) && ModuleRegistry_IsInitialized(RUNTIME_MODULE_QOL));
    }
    /* 已就绪仍由Guard安全点负责；公共输入帧不能半程提交idle参数。 */
    runtime_state=1;CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,64));
    unsigned generation=RuntimeConfig_Current()->generation;
    CHECK(input_frame(keyboard_object,0)==7 && RuntimeConfig_HasPending());
    CHECK(RuntimeConfig_Current()->generation==generation && RuntimeConfig_GetInt(CONFIG_DODGE_DISTANCE)!=64);
    runtime_state=-1;CHECK(input_frame(keyboard_object,0)==7);
    CHECK(!RuntimeConfig_HasPending() && RuntimeConfig_GetInt(CONFIG_DODGE_DISTANCE)==64);
    CHECK(frame_callbacks==11);
    DeleteFileW(path);
    printf("真实延后SHA/SDL/五主桥/Guard失败隔离、回滚及idle代数回归通过：%u项\n",checks);
    return 0;
}
