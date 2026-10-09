#include "QOLText.h"
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
/* 首次定位分帧推进，每帧最多1024槽；找到一个候选仍须扫描完以验证唯一性。 */
static unsigned long search_table,search_next,search_limit,search_found,search_matches,search_found_slot;
static unsigned long bound_slot;
static unsigned char search_generation,bound_generation;
static int search_active;

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
    unsigned long header[2];
    if (!g_profile || !action || !manager || !RuntimeWin32_Read(action,header,8ul)) return 0;
    return header[0]==GAME_IMAGE_BASE+g_profile->qol.pickup_action_vtable_rva && header[1]==manager;

}

static int registered_action(unsigned long action,unsigned long manager,unsigned long table,
                             unsigned long index,unsigned char generation)
{
    unsigned char slot[6];
    if (!action || !RuntimeWin32_Read(table+index*ACTION_SLOT_SIZE,slot,6ul)) return 0;
    unsigned long pointer=(unsigned long)slot[2]|((unsigned long)slot[3]<<8)|
        ((unsigned long)slot[4]<<16)|((unsigned long)slot[5]<<24);
    /* 仅vtable相同无法发现槽释放/复用；登记指针与代数也要匹配，才可调用原动作。 */
    return pointer==action && slot[1]==generation && action_matches_manager(action,manager);
}

static unsigned long locate_pickup_action(void)
{
    unsigned long manager,table;
    if (!g_profile) return 0ul;
    if (!read_u32(GAME_IMAGE_BASE+g_profile->qol.ground_manager_global_rva,&manager) ||
        !read_u32(GAME_IMAGE_BASE+g_profile->qol.action_slot_table_global_rva,&table) || !manager || !table) {
        g_bound_ground_manager=g_bound_pickup_action=0ul;search_active=0;return 0ul;
    }
    if (manager==g_bound_ground_manager && table==search_table && g_bound_pickup_action) {
        if (registered_action(g_bound_pickup_action,manager,table,bound_slot,bound_generation)) return g_bound_pickup_action;
    }
    if (manager!=g_bound_ground_manager || table!=search_table || !search_active) {
        RuntimeMemoryRegion region;
        g_bound_ground_manager=manager;g_bound_pickup_action=0ul;
        if (!RuntimeWin32_Query(table,&region) || table<region.base || table>=region.base+region.size) {
            search_active=0;return 0ul;
        }
        search_table=table;search_next=search_found=search_matches=0ul;
        search_limit=(region.base+region.size-table)/ACTION_SLOT_SIZE;
        if(search_limit>ACTION_SLOT_SCAN_MAX)search_limit=ACTION_SLOT_SCAN_MAX;
        search_active=1;
    }
    unsigned long amount=search_limit-search_next;
    if(amount>1024ul)amount=1024ul;
    unsigned char block[1024ul*ACTION_SLOT_SIZE];
    /* 一次验证并读取本帧连续块，不对数万个空槽逐一VirtualQuery。
     * 所有候选仍逐个检查实际vtable/manager，不直接缓存未知对象或提前调用。 */
    if (!amount || !RuntimeWin32_Read(table+search_next*ACTION_SLOT_SIZE,block,amount*ACTION_SLOT_SIZE)) {
        search_active=0;return 0ul;
    }
    for(unsigned long i=0;i<amount;++i) {
        const unsigned char *p=block+i*ACTION_SLOT_SIZE+ACTION_SLOT_OBJECT_OFFSET;
        unsigned long candidate=(unsigned long)p[0]|((unsigned long)p[1]<<8)|
            ((unsigned long)p[2]<<16)|((unsigned long)p[3]<<24);
        if(action_matches_manager(candidate,manager)) {
            search_found=candidate;
            search_found_slot=search_next+i;search_generation=block[i*ACTION_SLOT_SIZE+1];
            if(++search_matches>1ul) {search_active=0;return 0ul;}
        }
    }
    search_next+=amount;
    if(search_next<search_limit)return 0ul;
    search_active=0;
    if(search_matches==1ul && registered_action(search_found,manager,table,search_found_slot,search_generation)) {
        bound_slot=search_found_slot;bound_generation=search_generation;g_bound_pickup_action=search_found;
    }
    return g_bound_pickup_action;
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

/* 每个候选读取实际当前库存；币种不占背包，堆叠资格沿原查询，不能只看空格。 */
static int candidate_capacity(unsigned long object,PickupItemClass type)
{
    typedef unsigned long (__fastcall *InventoryGet)(void *,void *);
    typedef int (__fastcall *StackRoom)(void *,void *,int,int,unsigned long *);
    InventoryGet get=(InventoryGet)(uintptr_t)(GAME_IMAGE_BASE+g_profile->qol.inventory_get_rva);
    unsigned long bag=get((void *)(GAME_IMAGE_BASE+g_profile->qol.inventory_root_rva),NULL),id,quantity;
    if(!bag || !ItemClassifier_GetPickupInfo(object,&id,&quantity))return 0;
    if(type==PICKUP_ITEM_MONEY) {
        unsigned long money;if(!read_u32(bag+0x20ul,&money) || money>0x7FFFFFFFul)return 0;
        /* 原加钱函数用有符号32位溢出拒绝整笔，近上限也要计入本堆金额。 */
        return quantity<=0x7FFFFFFFul-money;
    }
    unsigned long items[50];if(!RuntimeWin32_Read(bag+0xA4ul,items,sizeof items))return 0;
    for(unsigned i=0;i<50;++i)if(items[i]==0xFFFFFFFFul)return 1;
    if(!g_profile->qol.inventory_stack_room_rva)return 0;
    unsigned long fits=0;StackRoom find=(StackRoom)(uintptr_t)(GAME_IMAGE_BASE+g_profile->qol.inventory_stack_room_rva);
    /* 原函数查询背包/快捷栏62槽及记录的可堆叠标志/9件上限，返回可合并数量。
     * 它只查询，不合并；实际部分合并与剩余数量仍交原拾取业务处理。 */
    return find((void *)bag,NULL,(int)id,(int)quantity,&fits)>=0 && fits>0;
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
            RuntimeWin32_Log(g_self_module, QOLText_AutoPickup_WaitingForUniqueTargetLog);
        return;
    }
    if (!g_scan_logged) {
        g_scan_logged=1;
        RuntimeWin32_Log(g_self_module, QOLText_AutoPickup_NativeScanStartedLog);
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
    g_bound_pickup_action = 0ul;search_active=0;

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

    if (!search_active && g_interval_ms != 0ul && (now - g_last_scan_ms) < g_interval_ms) {
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

    if (!ItemClassifier_GetPickupClass(ground_item, &item_class)) {
        return 0;
    }

    return policy_accepts(item_class) && candidate_capacity(ground_item,item_class);
}

/* 切换模式只更新过滤策略；周期变化或由关闭启用时重新开始计时，不重装入口。 */
void AutoPickup_ApplySettings(AutoPickupPolicy policy,unsigned long interval_ms)
{
    if (g_interval_ms!=interval_ms || (g_policy==AUTO_PICKUP_POLICY_OFF && policy!=AUTO_PICKUP_POLICY_OFF))
        g_timer_started=0;
    g_policy=policy;g_interval_ms=interval_ms;
}
