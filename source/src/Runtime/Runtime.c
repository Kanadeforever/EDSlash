#include "Runtime.h"
#include "ModuleRegistry.h"

/* 全工程只存在这一份 RuntimeContext。 */
static RuntimeContext g_runtime;
static int g_runtime_initialized = 0;

int Runtime_Initialize(void* self_module)
{
    const GameProfile* profile;

    /* DllMain 和某些 ASI Loader 可能都会调用初始化；真正工作只能做一次。 */
    if (g_runtime_initialized) {
        return g_runtime.profile ? 1 : 0;
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
}
