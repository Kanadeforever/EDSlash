#include "ModuleRegistry.h"
#include "../Modules/DisplayFix/DisplayFixModule.h"

/*
 * 官方模块表是“一个 ASI、内部多模块”的核心。
 * v0.1-dev1 只有 DisplayFix；下一阶段加入手柄时，只需要新增 ControllerModule 并在这里登记，
 * 不需要增加第二个 ASI、第二套游戏识别、第二个 DllMain，也不需要重新设计加载顺序。
 */
static const ModuleDescriptor MODULES[] = {
    { RUNTIME_MODULE_DISPLAY_FIX, "DisplayFix", DisplayFixModule_Initialize }
};

/*
 * 每个模块只有一个很小的初始化状态位。
 * 固定数组按 RuntimeModuleId 索引，不使用 malloc；未知/越界 ID 永远按“未初始化”处理。
 */
static int g_module_initialized[RUNTIME_MODULE_COUNT];

int ModuleRegistry_InitializeAll(const struct RuntimeContext* runtime)
{
    unsigned long i;
    unsigned long count = (unsigned long)(sizeof(MODULES) / sizeof(MODULES[0]));

    if (!runtime) {
        return 0;
    }

    for (i = 0u; i < count; ++i) {
        const ModuleDescriptor* module = &MODULES[i];
        int initialized = 0;

        /*
         * 表项自己如果写坏（空函数、非法 ID），只把这一项视为失败并继续后面的模块。
         * 统一 ASI 的目标就是“一个功能坏了不要拖死所有其它功能”，所以这里不能再沿用旧式全局早退。
         */
        if (module->initialize &&
            module->module_id > RUNTIME_MODULE_NONE &&
            module->module_id < RUNTIME_MODULE_COUNT) {
            initialized = module->initialize(runtime) ? 1 : 0;
            g_module_initialized[module->module_id] = initialized;
        }
    }

    return 1;
}

int ModuleRegistry_IsInitialized(RuntimeModuleId module_id)
{
    if (module_id <= RUNTIME_MODULE_NONE || module_id >= RUNTIME_MODULE_COUNT) {
        return 0;
    }

    return g_module_initialized[module_id] ? 1 : 0;
}
