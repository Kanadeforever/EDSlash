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
    SHARED_HOOK_GROUND_ITEM_UPDATE = 6,
    SHARED_HOOK_INPUT_FRAME = 7,
    SHARED_HOOK_PICKUP_ENTRY = 8,
    SHARED_HOOK_CONTROLLER_KEYBOARD = 9,
    SHARED_HOOK_CONTROLLER_ASYNC = 10,
    SHARED_HOOK_CONTROLLER_RESOLVER = 11,
    /* Controller独占三类菜单的Tick/Show/hover槽，绘制仍归DisplayFix。 */
    SHARED_HOOK_CONTROLLER_MENU = 12,
    /* 两组只由Controller提供；软件光标绘制与世界悬停事件分别登记，避免跨模块抢入口。 */
    SHARED_HOOK_CONTROLLER_CURSOR = 13,
    SHARED_HOOK_CONTROLLER_INSPECT = 14,
    SHARED_HOOK_COUNT = 15
} SharedHookId;

typedef enum RuntimeModuleId {
    RUNTIME_MODULE_NONE = 0,
    RUNTIME_MODULE_DISPLAY_FIX = 1,
    RUNTIME_MODULE_CONTROLLER = 2,
    RUNTIME_MODULE_QOL = 3,
    RUNTIME_MODULE_INPUT = 4,
    RUNTIME_MODULE_COUNT = 5
} RuntimeModuleId;

/* 第一个 Claim 成功；相同 owner 重复 Claim 也视为成功；不同 owner 抢同一个 Hook 会失败。 */
/* Claim只接受NONE与COUNT之间的真实模块；同owner重复声明成功，冲突或非法值不改槽。 */
int HookManager_Claim(SharedHookId hook_id, RuntimeModuleId owner);

/* 查询当前Hook的唯一owner；空槽或非法hook_id均返回NONE，不改变槽。 */
RuntimeModuleId HookManager_GetOwner(SharedHookId hook_id);

/*
 * 释放某个模块当前声明的全部共享 Hook 所有权。
 * 这不是“卸载已经写进游戏内存的机器码 Hook”；这里只回滚尚未完成初始化时的所有权声明。
 * 以后如果做热卸载，必须另行设计真正的物理 Hook 撤销生命周期，不能误把这个函数当卸载器。
 */
/* Release仅清合法owner的声明，不恢复机器码；非法owner无操作，其他模块不受影响。 */
void HookManager_ReleaseOwned(RuntimeModuleId owner);

#endif
