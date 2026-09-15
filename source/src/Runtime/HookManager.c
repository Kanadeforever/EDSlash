#include "HookManager.h"

static RuntimeModuleId g_hook_owners[SHARED_HOOK_COUNT];

int HookManager_Claim(SharedHookId hook_id, RuntimeModuleId owner)
{
    RuntimeModuleId current;

    if ((unsigned long)hook_id >= (unsigned long)SHARED_HOOK_COUNT || owner == RUNTIME_MODULE_NONE) {
        return 0;
    }

    current = g_hook_owners[hook_id];
    if (current == RUNTIME_MODULE_NONE) {
        g_hook_owners[hook_id] = owner;
        return 1;
    }

    /* 同一个模块重复声明同一个 Hook 不算冲突；不同模块则明确拒绝。 */
    return current == owner ? 1 : 0;
}

RuntimeModuleId HookManager_GetOwner(SharedHookId hook_id)
{
    if ((unsigned long)hook_id >= (unsigned long)SHARED_HOOK_COUNT) {
        return RUNTIME_MODULE_NONE;
    }
    return g_hook_owners[hook_id];
}

void HookManager_ReleaseOwned(RuntimeModuleId owner)
{
    unsigned long i;

    if (owner == RUNTIME_MODULE_NONE) {
        return;
    }

    /*
     * 共享 Hook 数量很少，直接线性扫描固定数组最简单，也完全不需要动态内存。
     * 只清除仍然属于这个 owner 的槽，绝不会碰其它模块已经声明成功的 Hook。
     */
    for (i = 0u; i < (unsigned long)SHARED_HOOK_COUNT; ++i) {
        if (g_hook_owners[i] == owner) {
            g_hook_owners[i] = RUNTIME_MODULE_NONE;
        }
    }
}
