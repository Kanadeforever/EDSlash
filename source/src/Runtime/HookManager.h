#ifndef BLADESWORD_QOL_HOOK_MANAGER_H
#define BLADESWORD_QOL_HOOK_MANAGER_H

/*
 * HookManager 在 v0.1-dev1 先建立“谁拥有哪个物理 Hook”的规则。
 * DisplayFix 后端为了降低重构风险，暂时仍负责安装它已经实机验证过的具体 Hook；
 * 但它必须先登记为唯一提供者。以后其它模块不能再直接占用同一个 Hook ID，应该订阅 Runtime 事件。
 */

typedef enum SharedHookId {
    SHARED_HOOK_STRATEGY_LIFECYCLE = 0,
    SHARED_HOOK_MAIN_HUD_LAYOUT = 1,
    SHARED_HOOK_UI_MANAGER_DRAW = 2,
    SHARED_HOOK_UI_ROOT_PICKER = 3,
    SHARED_HOOK_WORLD_MOUSE_PRESS = 4,
    SHARED_HOOK_GLOBAL_MOUSE_RELEASE = 5,
    SHARED_HOOK_COUNT = 6
} SharedHookId;

typedef enum RuntimeModuleId {
    RUNTIME_MODULE_NONE = 0,
    RUNTIME_MODULE_DISPLAY_FIX = 1,
    RUNTIME_MODULE_CONTROLLER = 2,
    RUNTIME_MODULE_QOL = 3,
    RUNTIME_MODULE_COUNT = 4
} RuntimeModuleId;

/* 第一个 Claim 成功；相同 owner 重复 Claim 也视为成功；不同 owner 抢同一个 Hook 会失败。 */
int HookManager_Claim(SharedHookId hook_id, RuntimeModuleId owner);

/* 查询当前 Hook 的唯一 owner。没有人声明时返回 RUNTIME_MODULE_NONE。 */
RuntimeModuleId HookManager_GetOwner(SharedHookId hook_id);

/*
 * 释放某个模块当前声明的全部共享 Hook 所有权。
 * 这不是“卸载已经写进游戏内存的机器码 Hook”；v0.1-dev1 只用它处理模块初始化尚未真正开始时的声明回滚。
 * 以后如果做热卸载，必须另行设计真正的物理 Hook 撤销生命周期，不能误把这个函数当卸载器。
 */
void HookManager_ReleaseOwned(RuntimeModuleId owner);

#endif
