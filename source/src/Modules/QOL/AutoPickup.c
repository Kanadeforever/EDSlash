#include "AutoPickup.h"
#include "../../Runtime/Perf.h"
#include "GroundItems.h"
#include "ItemClassifier.h"
#include "../../Runtime/Win32Bridge.h"

#define GAME_IMAGE_BASE 0x00400000ul

#define ACTION_MANAGER_OFFSET 0x04ul
#define ACTION_SLOT_SIZE 6ul
#define ACTION_SLOT_OBJECT_OFFSET 2ul
#define ACTION_SLOT_SCAN_MAX 0x8000ul
#define ACTION_ID_PICKUP 22

typedef int (__fastcall *ActionEntryFn)(void* action_object,
                                        void* unused_edx,
                                        int action,
                                        int a3,
                                        int a4,
                                        int a5,
                                        int a6,
                                        int a7);

static const GameProfile* g_profile;
static ActionEntryFn g_action_entry;
static AutoPickupPolicy g_policy;
static unsigned long g_interval_ms;
static unsigned long g_last_scan_ms;
static int g_timer_started;
static void *g_self_module;
static int g_scan_logged;
static unsigned long g_wait_scans;

/*
 * 一次自动拾取只在同步调用原版 action=22 的期间把此标志设为 1。
 * 原版 ActionEntry 返回后立刻清零，所以玩家自己的 Z 路径不会进入自动过滤。
 */
static int g_native_pickup_scan_active;

/*
 * 动作对象属于当前场景的 GroundManager。
 * 只有 GroundManager 指针变化时才重新扫描动作槽表，避免每个扫描周期都遍历整块内存。
 */
static unsigned long g_bound_ground_manager;
static unsigned long g_bound_pickup_action;

static ActionEntryFn action_entry_from_address(unsigned long address)
{
    union {
        unsigned long address;
        ActionEntryFn function;
    } value;

    value.address = address;
    return value.function;
}

static int read_u32(unsigned long address, unsigned long* value)
{
    return RuntimeWin32_Read(address, value, 4ul);
}

static int action_matches_manager(unsigned long action, unsigned long manager)
{
    unsigned long vtable;
    unsigned long action_manager;

    if (!g_profile || action == 0ul || manager == 0ul) {
        return 0;
    }

    if (!read_u32(action, &vtable) ||
        !read_u32(action + ACTION_MANAGER_OFFSET, &action_manager)) {
        return 0;
    }

    return vtable == GAME_IMAGE_BASE + g_profile->qol.pickup_action_vtable_rva &&
           action_manager == manager;
}

static unsigned long locate_pickup_action(void)
{
    unsigned long manager;
    unsigned long table;
    RuntimeMemoryRegion region;
    unsigned long available;
    unsigned long slot_count;
    unsigned long index;
    unsigned long found;
    unsigned long found_count;

    if (!g_profile) {
        return 0ul;
    }

    if (!read_u32(GAME_IMAGE_BASE + g_profile->qol.ground_manager_global_rva, &manager) ||
        !read_u32(GAME_IMAGE_BASE + g_profile->qol.action_slot_table_global_rva, &table) ||
        manager == 0ul ||
        table == 0ul) {
        g_bound_ground_manager = 0ul;
        g_bound_pickup_action = 0ul;
        return 0ul;
    }

    if (manager == g_bound_ground_manager &&
        action_matches_manager(g_bound_pickup_action, manager)) {
        return g_bound_pickup_action;
    }

    g_bound_ground_manager = manager;
    g_bound_pickup_action = 0ul;

    if (!RuntimeWin32_Query(table, &region) ||
        table < region.base ||
        table >= region.base + region.size) {
        return 0ul;
    }

    available = (region.base + region.size) - table;
    slot_count = available / ACTION_SLOT_SIZE;
    if (slot_count > ACTION_SLOT_SCAN_MAX) {
        slot_count = ACTION_SLOT_SCAN_MAX;
    }

    found = 0ul;
    found_count = 0ul;

    for (index = 0ul; index < slot_count; ++index) {
        unsigned long candidate_address;
        unsigned long candidate;

        candidate_address = table + index * ACTION_SLOT_SIZE + ACTION_SLOT_OBJECT_OFFSET;
        if (!read_u32(candidate_address, &candidate)) {
            continue;
        }
        if (!action_matches_manager(candidate, manager)) {
            continue;
        }

        found = candidate;
        ++found_count;

        /*
         * 正常场景只应存在一个匹配的拾取动作对象。
         * 若出现多个候选，宁可本次扫描不自动拾取，也不猜哪个对象应该被调用。
         */
        if (found_count > 1ul) {
            return 0ul;
        }
    }

    if (found_count == 1ul) {
        g_bound_pickup_action = found;
        return found;
    }

    return 0ul;
}

static int policy_accepts(PickupItemClass item_class)
{
    if (g_policy == AUTO_PICKUP_POLICY_ALL) {
        return 1;
    }
    if (item_class == PICKUP_ITEM_MONEY) {
        return 1;
    }
    if (g_policy >= AUTO_PICKUP_POLICY_MONEY_RECOVERY &&
        item_class == PICKUP_ITEM_RECOVERY) {
        return 1;
    }
    if (g_policy >= AUTO_PICKUP_POLICY_USEFUL &&
        (item_class == PICKUP_ITEM_GEM || item_class == PICKUP_ITEM_CHARM)) {
        return 1;
    }
    return 0;
}

static void run_native_pickup_scan(void)
{
    unsigned long action;

    if (!g_action_entry || g_native_pickup_scan_active) {
        return;
    }

    action = locate_pickup_action();
    if (action == 0ul) {
        if (++g_wait_scans==10ul)
            RuntimeWin32_Log(g_self_module, "[QoL][等待] 尚未定位唯一拾取对象，未调用动作；进入场景后继续检查。");
        return;
    }
    if (!g_scan_logged) {
        g_scan_logged=1;
        RuntimeWin32_Log(g_self_module, "[QoL][运行] 已定位拾取对象，开始调用原版动作22扫描；是否入包由原版判断。");
    }

    /*
     * 插件不直接改背包，也不删除地面对象。
     * 它只让游戏在当前主线程执行一次自己的拾取动作，距离、容量、音效和对象生命周期仍由原版处理。
     */
    g_native_pickup_scan_active = 1;
    g_action_entry((void*)action, (void*)0, ACTION_ID_PICKUP, 0, 0, 0, 1, 1);
    g_native_pickup_scan_active = 0;
}

int AutoPickup_Initialize(const RuntimeContext* runtime,
                          AutoPickupPolicy policy,
                          unsigned long interval_ms)
{
    if (!runtime || !runtime->profile) {
        return 0;
    }

    g_profile = runtime->profile;
    g_self_module=runtime->self_module;g_scan_logged=0;g_wait_scans=0ul;
    g_policy = policy;
    g_interval_ms = interval_ms;
    g_action_entry = action_entry_from_address(
        GAME_IMAGE_BASE + runtime->profile->qol.action_entry_rva);

    g_timer_started = 0;
    g_last_scan_ms = 0ul;
    g_native_pickup_scan_active = 0;
    g_bound_ground_manager = 0ul;
    g_bound_pickup_action = 0ul;

    return g_action_entry ? 1 : 0;
}

void AutoPickup_AfterInputFrame(void)
{
    unsigned long now;

    if (g_policy == AUTO_PICKUP_POLICY_OFF) {
        return;
    }

    now = RuntimeWin32_TickCount();

    /*
     * 第一次输入帧只建立计时起点。
     * IntervalMs>0 时会完整等待一个周期，避免 ASI 刚加载就立刻扫描一次。
     */
    if (!g_timer_started) {
        g_timer_started = 1;
        g_last_scan_ms = now;
        if (g_interval_ms != 0ul) {
            return;
        }
    }

    if (g_interval_ms != 0ul && (now - g_last_scan_ms) < g_interval_ms) {
        return;
    }

    g_last_scan_ms = now;
    int64_t perf=RuntimePerf_Begin();
    run_native_pickup_scan();
    RuntimePerf_End(PERF_PICKUP,perf);
}

void AutoPickup_Disable(void)
{
    /* 拾取过滤和输入入口是两处 Hook。只装好输入入口而过滤安装失败时，
     * 不允许残留的输入包装继续发起扫描，否则可能绕过玩家选择的物品类别。 */
    g_policy = AUTO_PICKUP_POLICY_OFF;
    g_native_pickup_scan_active = 0;
}

int AutoPickup_AllowPickupCandidate(unsigned long ground_item)
{
    PickupItemClass item_class;

    if (!g_native_pickup_scan_active) {
        return 1;
    }

    if (!ItemClassifier_IsGroundItem(ground_item)) {
        return 0;
    }

    if (!GroundItems_IsReadyForAutomaticPickup(ground_item)) {
        return 0;
    }

    if (g_policy == AUTO_PICKUP_POLICY_ALL) {
        return 1;
    }

    if (!ItemClassifier_GetPickupClass(ground_item, &item_class)) {
        return 0;
    }

    return policy_accepts(item_class);
}

/* 切换模式只更新过滤策略；周期变化或由关闭启用时重新开始计时，不重装入口。 */
void AutoPickup_ApplySettings(AutoPickupPolicy policy,unsigned long interval_ms)
{
    if (g_interval_ms!=interval_ms || (g_policy==AUTO_PICKUP_POLICY_OFF && policy!=AUTO_PICKUP_POLICY_OFF))
        g_timer_started=0;
    g_policy=policy;g_interval_ms=interval_ms;
}
