#include "InputFrame.h"
#include "Config.h"
#include "Win32Bridge.h"
#include "X86Detour.h"
#include "ModuleRegistry.h"
#include "Log.h"
#include "Perf.h"
typedef int (__fastcall *InputFn)(unsigned char *,void *);
static InputFn original;
static X86Detour detour;
int RuntimeInput_IsReady(void) {return detour.installed && original!=0;}
/* 外层函数包围原游戏完整输入刷新，先提交已保存的普通设置，最后通知QOL扫描。 */
static int __fastcall input_frame(unsigned char *keys,void *unused)
{
    /* 这里已经离开Loader锁，允许启动后台日志线程；SDL仍由手柄采样桥初始化。 */
    (void)RuntimeLog_Start();RuntimePerf_Initialize();
    int64_t perf=RuntimePerf_Begin();
    RuntimeConfig_ApplyFrame(!ModuleRegistry_IsInitialized(RUNTIME_MODULE_CONTROLLER));
    Runtime_EmitEvent(RUNTIME_EVENT_INPUT_FRAME_BEGIN,keys,0,0);
    int result=original ? original(keys,unused):0;
    Runtime_EmitEvent(RUNTIME_EVENT_INPUT_FRAME_END,keys,(unsigned long)result,0);
    RuntimePerf_End(PERF_INPUT,perf);RuntimePerf_FrameEnd();
    return result;
}
int RuntimeInput_Initialize(const RuntimeContext *runtime)
{
    if (!runtime || !runtime->profile || !RuntimeWin32_IsReady()) return 0;
    unsigned long address=0x400000ul+runtime->profile->qol.input_update_rva;
    unsigned char bytes[5];
    static const unsigned char expected[5]={0x33,0xC0,0x8D,0x51,0x08};
    if (!RuntimeWin32_Read(address,bytes,5)) return 0;
    for (unsigned i=0;i<5;++i) if (bytes[i]!=expected[i]) return 0;
    if (!HookManager_Claim(SHARED_HOOK_INPUT_FRAME,RUNTIME_MODULE_INPUT)) return 0;
    union {unsigned long address;InputFn function;} convert;
    convert.function=input_frame;
    if (!X86Detour_Install(&detour,address,convert.address,5)) {
        HookManager_ReleaseOwned(RUNTIME_MODULE_INPUT);return 0;
    }
    convert.address=(unsigned long)detour.gateway;original=convert.function;return 1;
}
