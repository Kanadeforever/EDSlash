#include "Runtime.h"
#include "ModuleRegistry.h"
#include "Win32Bridge.h"
#include "Config.h"
#include "Log.h"
#include "InputFrame.h"

/* 全工程只存在这一份 RuntimeContext。 */
static RuntimeContext g_runtime;
static int g_runtime_initialized = 0;
static int g_runtime_ready = 0;

int Runtime_Initialize(void* self_module)
{
    const GameProfile* profile;

    /* DllMain 和某些 ASI Loader 可能都会调用初始化；真正工作只能做一次。 */
    if (g_runtime_initialized) {
        return g_runtime_ready;
    }
    g_runtime_initialized = 1;

    if (!self_module) {
        return 0;
    }

    profile = GameProfile_Detect();
    if (!profile) {
        /* 未知 EXE 最安全的行为是什么都不改。 */
        return 0;
    }

    g_runtime.self_module = self_module;
    g_runtime.profile = profile;

    /*
     * Runtime 的 Win32 桥只依赖已经识别出的 Profile。
     * 桥初始化失败不会阻止不依赖它的模块继续启动；需要系统 API 的模块会自行检查是否可用。
     */
    (void)RuntimeWin32_Initialize(profile);
    (void)RuntimeLog_Initialize(self_module);
    if (!RuntimeConfig_Initialize(self_module)) {
        RuntimeLog_Line(RuntimeConfig_Error());
        (void)RuntimeLog_Flush(0);
        return 0;
    }
    if (!RuntimeInput_Initialize(&g_runtime)) {
        /* 显示修复与手柄使用各自的其它入口，公共输入帧故障不拖垮它们。 */
        RuntimeLog_Line("[输入][警告] 公共帧入口不可用；自动拾取模块将自行停止，显示与手柄继续初始化。");
    }

    /*
     * 官方模块全部通过同一个 ModuleRegistry 启动。
     * Runtime 不知道 DisplayFix、Controller 等具体模块细节；它只提供共享上下文和基础设施。
     */
    /*
     * Registry 本身初始化失败（例如传入上下文无效）才让 Runtime 失败。
     * 单个功能模块的兼容失败由 Registry 隔离；后续模块仍会继续尝试初始化。
     * 这是“所有模块合并到一个 ASI”以后必须具备的故障隔离，否则一个可选模块就会拖死整套大修。
     */
    if (!ModuleRegistry_InitializeAll(&g_runtime)) {
        return 0;
    }

    (void)RuntimeLog_Flush(0);
    g_runtime_ready=1;
    return 1;
}

const RuntimeContext* Runtime_GetContext(void)
{
    return &g_runtime;
}

int Runtime_Subscribe(RuntimeEventId event_id, RuntimeEventCallback callback, void* user_data)
{
    return EventBus_Subscribe(event_id, callback, user_data);
}

void Runtime_EmitEvent(RuntimeEventId event_id, void* subject, unsigned long value1, unsigned long value2)
{
    EventBus_Emit(event_id, subject, value1, value2);
    if (event_id==RUNTIME_EVENT_GAMEPLAY_EXIT) (void)RuntimeLog_Flush(1000);
}
