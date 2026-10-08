#include "QOLModule.h"
#include "GroundItems.h"
#include "ItemClassifier.h"
#include "AutoPickup.h"
#include "PickupNotice.h"
#include "../../Runtime/HookManager.h"
#include "../../Runtime/Win32Bridge.h"
#include "../../Runtime/X86Detour.h"
#include "../../Runtime/Config.h"
#include "../../Runtime/InputFrame.h"
#include "../../Runtime/SettingsWindow.h"

#define GAME_IMAGE_BASE 0x00400000ul
#define CONFIG_PATH_CAPACITY 260ul

typedef void (__fastcall *GroundUpdateFn)(void* object, void* unused_edx);
typedef int (__cdecl *PickupEntryFn)(int ground_item, int action_this);

typedef struct QolSettings {
    int show_ground_names;
    AutoPickupPolicy pickup_policy;
    unsigned long scan_interval_ms;
    unsigned long drop_delay_ms;
} QolSettings;

static GroundUpdateFn g_original_ground_update;
static PickupEntryFn g_original_pickup_entry;

static X86Detour g_ground_update_detour;
static X86Detour g_pickup_entry_detour;

static GroundUpdateFn ground_update_from_address(unsigned long address)
{
    union { unsigned long address; GroundUpdateFn function; } value;
    value.address = address;
    return value.function;
}

static PickupEntryFn pickup_entry_from_address(unsigned long address)
{
    union { unsigned long address; PickupEntryFn function; } value;
    value.address = address;
    return value.function;
}

static unsigned long ground_update_address(GroundUpdateFn function)
{
    union { unsigned long address; GroundUpdateFn function; } value;
    value.function = function;
    return value.address;
}

static unsigned long pickup_entry_address(PickupEntryFn function)
{
    union { unsigned long address; PickupEntryFn function; } value;
    value.function = function;
    return value.address;
}

static int bytes_equal(unsigned long address, const unsigned char* expected, unsigned long size)
{
    unsigned char actual[16];
    unsigned long i;

    if (!expected || size == 0ul || size > (unsigned long)sizeof(actual)) {
        return 0;
    }
    if (!RuntimeWin32_Read(address, actual, size)) {
        return 0;
    }

    for (i = 0ul; i < size; ++i) {
        if (actual[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

static int verify_qol_entries(const GameProfile* profile)
{
    static const unsigned char ground_head[7] = {
        0x83u, 0xECu, 0x08u, 0x8Du, 0x54u, 0x24u, 0x00u
    };
    static const unsigned char pickup_head[5] = {
        0x56u, 0x8Bu, 0x74u, 0x24u, 0x0Cu
    };

    if (!profile) {
        return 0;
    }

    return bytes_equal(GAME_IMAGE_BASE + profile->qol.ground_item_update_rva,
                       ground_head,
                       (unsigned long)sizeof(ground_head)) &&
           bytes_equal(GAME_IMAGE_BASE + profile->qol.pickup_entry_rva,
                       pickup_head,
                       (unsigned long)sizeof(pickup_head));
}

static int load_settings(void *self_module,QolSettings *settings)
{
    (void)self_module;
    settings->show_ground_names=RuntimeConfig_GetInt(CONFIG_GROUND_NAMES);
    settings->pickup_policy=(AutoPickupPolicy)RuntimeConfig_GetInt(CONFIG_PICKUP_MODE);
    settings->scan_interval_ms=(unsigned long)RuntimeConfig_GetInt(CONFIG_PICKUP_INTERVAL);
    settings->drop_delay_ms=(unsigned long)RuntimeConfig_GetInt(CONFIG_DROP_DELAY);
    return 1;
}

static void __fastcall qol_ground_update(void* object, void* unused_edx)
{
    if (g_original_ground_update) {
        g_original_ground_update(object, unused_edx);
    }

    GroundItems_AfterUpdate(object);
}

static void after_input(RuntimeEventId event,void *subject,unsigned long v1,unsigned long v2,void *user)
{
    (void)event;(void)subject;(void)v1;(void)v2;(void)user;
    /* 安全帧边界只同步几个内存值，不重新安装Hook，也不重置地面物品生命周期。 */
    GroundItems_ApplySettings(RuntimeConfig_GetInt(CONFIG_GROUND_NAMES),
        (unsigned long)RuntimeConfig_GetInt(CONFIG_DROP_DELAY));
    AutoPickup_ApplySettings((AutoPickupPolicy)RuntimeConfig_GetInt(CONFIG_PICKUP_MODE),
        (unsigned long)RuntimeConfig_GetInt(CONFIG_PICKUP_INTERVAL));
    /* 原暂停仍刷新菜单输入；设置窗口存在时仅同步配置，不执行世界拾取扫描。 */
    if(!SettingsWindow_Active())AutoPickup_AfterInputFrame();
}

static int __cdecl qol_pickup_entry(int ground_item, int action_this)
{
    if (!g_original_pickup_entry) {
        return 0;
    }

    if (!AutoPickup_AllowPickupCandidate((unsigned long)ground_item)) {
        return 0;
    }

    PickupNoticeSnapshot snapshot;
    PickupNotice_Before((unsigned long)ground_item,(unsigned long)action_this,&snapshot);
    int result=g_original_pickup_entry(ground_item,action_this);
    PickupNotice_After(&snapshot);
    return result;
}

static void rollback_hooks(void)
{
    /* 先关闭自动业务，再尝试恢复物理入口，部分回滚失败也不会变成无过滤拾取。 */
    PickupNotice_Disable();AutoPickup_Disable();
    /* 每个入口都要尝试撤销，不能因第一处失败就跳过其余入口。
     * 有入口未撤销时保留所有权，防止后续模块覆盖仍然生效的物理跳转。 */
    int pickup_removed = X86Detour_Remove(&g_pickup_entry_detour);
    int ground_removed = X86Detour_Remove(&g_ground_update_detour);
    if (pickup_removed && ground_removed) {
        HookManager_ReleaseOwned(RUNTIME_MODULE_QOL);
    }
}

static int install_ground_hook(const GameProfile* profile)
{
    unsigned long target;

    if (!HookManager_Claim(SHARED_HOOK_GROUND_ITEM_UPDATE, RUNTIME_MODULE_QOL)) {
        return 0;
    }

    target = GAME_IMAGE_BASE + profile->qol.ground_item_update_rva;
    if (!X86Detour_Install(&g_ground_update_detour,
                           target,
                           ground_update_address(qol_ground_update),
                           7ul)) {
        return 0;
    }

    g_original_ground_update = ground_update_from_address(g_ground_update_detour.gateway);
    return g_original_ground_update ? 1 : 0;
}

static int install_pickup_hook(const GameProfile *profile)
{
    if (!HookManager_Claim(SHARED_HOOK_PICKUP_ENTRY,RUNTIME_MODULE_QOL)) return 0;
    unsigned long target=GAME_IMAGE_BASE+profile->qol.pickup_entry_rva;
    if (!X86Detour_Install(&g_pickup_entry_detour,target,pickup_entry_address(qol_pickup_entry),5)) return 0;
    g_original_pickup_entry=pickup_entry_from_address(g_pickup_entry_detour.gateway);
    return g_original_pickup_entry ? 1:0;
}

int QOLModule_Initialize(const RuntimeContext* runtime)
{
    QolSettings settings;

    if (!runtime || !runtime->profile || !runtime->self_module) {
        return 0;
    }
    if (!RuntimeWin32_IsReady() || !RuntimeInput_IsReady()) {
        RuntimeWin32_Log(runtime->self_module,"[QoL][停止] 公共输入帧或Win32桥不可用，未安装拾取Hook。");
        return 0;
    }
    RuntimeWin32_Log(runtime->self_module, "[QoL] 统一模块：初始化地面名称、自动拾取与成功提示。");
    if (!verify_qol_entries(runtime->profile)) {
        RuntimeWin32_Log(runtime->self_module, "[QoL][停止] 原生入口签名不匹配，未安装模块。");
        return 0;
    }
    if (!load_settings(runtime->self_module, &settings)) {
        RuntimeWin32_Log(runtime->self_module, "[QoL][停止] 无法定位同目录配置文件。");
        return 0;
    }

    RuntimeWin32_LogNumber(runtime->self_module, "[QoL] 地面名称常显=", (unsigned long)settings.show_ground_names);
    RuntimeWin32_LogNumber(runtime->self_module, "[QoL] 自动拾取模式=", (unsigned long)settings.pickup_policy);
    RuntimeWin32_LogNumber(runtime->self_module, "[QoL] 扫描间隔毫秒=", settings.scan_interval_ms);
    RuntimeWin32_LogNumber(runtime->self_module, "[QoL] 掉落等待毫秒=", settings.drop_delay_ms);

    if (!PickupNotice_Initialize(runtime)) {
        RuntimeWin32_Log(runtime->self_module,"[QoL][停止] 拾取提示接口或公共事件订阅验证失败。");return 0;
    }

    if (!GroundItems_Initialize(runtime,
                                settings.show_ground_names,
                                settings.drop_delay_ms)) {
        PickupNotice_Disable();return 0;
    }

    if (!ItemClassifier_Initialize(runtime) ||
         !AutoPickup_Initialize(runtime,
                                settings.pickup_policy,
                                settings.scan_interval_ms)) {
        PickupNotice_Disable();return 0;
    }

    if (!install_ground_hook(runtime->profile)) {
        RuntimeWin32_Log(runtime->self_module, "[QoL][停止] 地面物品入口安装失败，开始回滚。");
        rollback_hooks();
        return 0;
    }

    if (!install_pickup_hook(runtime->profile)) {
        RuntimeWin32_Log(runtime->self_module, "[QoL][停止] 自动拾取入口安装失败，开始回滚。");
        rollback_hooks();
        return 0;
    }

    if (!Runtime_Subscribe(RUNTIME_EVENT_INPUT_FRAME_END,after_input,(void*)0)) {
        rollback_hooks();return 0;
    }
    RuntimeWin32_Log(runtime->self_module, "[QoL][成功] 地面名称与自动拾取入口已就绪；实际运行仍需场景验证。");
    return 1;
}
