#include "DisplayFixModule.h"

/*
 * 两个函数分别由 Backend_DaoJian.c / Backend_WaiZhuan.c 提供。
 * 只有 Runtime 已经识别出的那个 Profile 会被调用，另一个后端虽然在同一个 ASI 里，但整场运行都不会进入。
 */
int DisplayFixDaoJian_Initialize(void* module);
int DisplayFixWaiZhuan_Initialize(void* module);

static int claim_display_fix_shared_hooks(void)
{
    /*
     * v0.1-dev1 先把 DisplayFix 已经实际占用的公共入口登记为“DisplayFix 唯一提供者”。
     * 下一阶段手柄模块如果需要 Strategy/UI Draw 等信息，应订阅 EventBus，而不能再 Claim 同一个物理入口。
     *
     * 这里特别注意“部分声明成功以后，后一个声明失败”的情况：
     * 如果不回滚，DisplayFix 虽然没有继续初始化，却会永久占着前几个 Hook 的 owner 槽，
     * 后面的 Controller/QoL 模块就会误以为这些入口已经有人真正提供。
     * 所以任意一步失败都会把本模块刚才声明的全部 owner 一次性清回去。
     */
    if (!HookManager_Claim(SHARED_HOOK_STRATEGY_LIFECYCLE, RUNTIME_MODULE_DISPLAY_FIX) ||
        !HookManager_Claim(SHARED_HOOK_MAIN_HUD_LAYOUT, RUNTIME_MODULE_DISPLAY_FIX) ||
        !HookManager_Claim(SHARED_HOOK_UI_MANAGER_DRAW, RUNTIME_MODULE_DISPLAY_FIX) ||
        !HookManager_Claim(SHARED_HOOK_UI_ROOT_PICKER, RUNTIME_MODULE_DISPLAY_FIX) ||
        !HookManager_Claim(SHARED_HOOK_WORLD_MOUSE_PRESS, RUNTIME_MODULE_DISPLAY_FIX) ||
        !HookManager_Claim(SHARED_HOOK_GLOBAL_MOUSE_RELEASE, RUNTIME_MODULE_DISPLAY_FIX)) {
        HookManager_ReleaseOwned(RUNTIME_MODULE_DISPLAY_FIX);
        return 0;
    }

    return 1;
}

int DisplayFixModule_Initialize(const RuntimeContext* runtime)
{
    if (!runtime || !runtime->profile || !runtime->self_module) {
        return 0;
    }

    if (!claim_display_fix_shared_hooks()) {
        return 0;
    }

    if (runtime->profile->game_id == GAME_ID_DAOJIAN) {
        return DisplayFixDaoJian_Initialize(runtime->self_module);
    }

    if (runtime->profile->game_id == GAME_ID_WAIZHUAN) {
        return DisplayFixWaiZhuan_Initialize(runtime->self_module);
    }

    return 0;
}
