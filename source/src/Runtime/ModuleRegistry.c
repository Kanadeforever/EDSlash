#include "ModuleRegistry.h"
#include "../Modules/DisplayFix/DisplayFixModule.h"
#include "../Modules/QOL/QOLModule.h"
#include "../Modules/Controller/ControllerModule.h"

/*
 * 模块表是“一个 ASI、内部多模块”的统一入口。
 * 每个功能模块只负责自己的初始化函数，游戏识别、加载顺序和唯一 DllMain 都由 Runtime 统一管理。
 * 这样新增功能时不会复制启动框架，也不会让多个模块分别判断当前游戏版本。
 */
static const ModuleDescriptor MODULES[] = {
    { RUNTIME_MODULE_DISPLAY_FIX, "DisplayFix", DisplayFixModule_Initialize },
    { RUNTIME_MODULE_QOL, "QOL", QOLModule_Initialize },
    { RUNTIME_MODULE_CONTROLLER, "Controller", ControllerModule_Initialize }
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
         * 统一 ASI 的目标就是“一个功能坏了不要拖死所有其它功能”，所以这里不能因为一个模块失败就全局早退。
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
