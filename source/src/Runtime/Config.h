#ifndef EDSLASH_CONFIG_H
#define EDSLASH_CONFIG_H
#include <stddef.h>

/* 模块使用固定编号读内存快照；编号不是INI键名，也不在游戏帧中读磁盘。 */
typedef enum {
    CONFIG_DISPLAY_ENABLED, CONFIG_BASE_HEIGHT, CONFIG_ASPECT_RATIO,
    CONFIG_FONT_DPI, CONFIG_CENTER_HUD, CONFIG_AUXILIARY_UI,
    CONFIG_GROUND_NAMES, CONFIG_PICKUP_MODE, CONFIG_PICKUP_INTERVAL, CONFIG_DROP_DELAY,
    CONFIG_CONTROLLER_ENABLED, CONFIG_DEADZONE, CONFIG_MOVE_LEAD, CONFIG_MOUSE_SPEED,
    CONFIG_RUMBLE, CONFIG_CHARGE_GUARD, CONFIG_FREE_RUN, CONFIG_OMNI_GUARD,
    CONFIG_DIRECTIONAL_DODGE, CONFIG_DODGE_DISTANCE, CONFIG_GUARD_MODE,
    CONFIG_GUARD_PERCENT, CONFIG_RECOVERY_MODE, CONFIG_RECOVERY_PERCENT,
    CONFIG_INSPECT_DISTANCE, CONFIG_LEGACY_ULTIMATE, CONFIG_COMBO_SWITCH,
    CONFIG_AIM_EXPAND_MS, CONFIG_MENU_SWAP_AB,
    CONFIG_WORLD_INTERACT,CONFIG_WORLD_AIM,CONFIG_WORLD_LEFT,CONFIG_WORLD_RIGHT,
    CONFIG_WORLD_RUN,CONFIG_WORLD_MAP,CONFIG_WORLD_SYSTEM,
    CONFIG_COUNT
} ConfigId;
typedef enum { CONFIG_BOOL, CONFIG_INT, CONFIG_PERCENT, CONFIG_CHOICE, CONFIG_TEXT } ConfigType;
typedef enum { CONFIG_APPLY_FRAME, CONFIG_APPLY_IDLE, CONFIG_APPLY_RESTART } ConfigApply;
typedef struct {
    ConfigId id;
    const char *table, *key, *label, *description;
    ConfigType type;
    int default_value, minimum, maximum, step;
    const char *default_text, *choices;
    ConfigApply apply;
} ConfigDescriptor;
typedef struct { int values[CONFIG_COUNT]; char aspect_ratio[40]; unsigned generation; } ConfigSnapshot;
/* 自动模式沿用原游戏当前绑定；自定义按Player+0x348的角色selector隔离。 */
typedef struct { int custom, selector, right; } ConfigBinding;
/* 设置窗口的候选修改：调用期间借用text；非文本项使用value，不持有游戏对象。 */
typedef struct {ConfigId id;int value;const char *text;} ConfigEdit;
typedef struct {unsigned game,role,slot;ConfigBinding value;} ConfigBindingEdit;

int RuntimeConfig_Initialize(void *module);
int RuntimeConfig_OpenPath(const wchar_t *path);
const ConfigSnapshot *RuntimeConfig_Current(void);
/* 已保存值可与实际生效值不同（待空闲／待重启），窗口不能只显示active。 */
const ConfigSnapshot *RuntimeConfig_Saved(void);
ConfigBinding RuntimeConfig_GetSavedBinding(unsigned game,unsigned role,unsigned slot);
/* 所有候选一起校验、一次原子保存；任一失败不保存部分字段。 */
int RuntimeConfig_SaveBatch(const ConfigEdit *edits,size_t count,
                            const ConfigBindingEdit *bindings,size_t binding_count);
int RuntimeConfig_GetInt(ConfigId id);
const ConfigDescriptor *RuntimeConfig_Descriptor(ConfigId id);
/* 修改先验证并原子保存，成功后更新候选快照；在帧边界显式提交生效。 */
int RuntimeConfig_SetInt(ConfigId id, int value);
/* 画面比例或描述表枚举名；枚举名保存后与SetInt编号使用同一快照链。 */
int RuntimeConfig_SetText(ConfigId id, const char *value);
int RuntimeConfig_SetBinding(unsigned game, unsigned role, unsigned slot, ConfigBinding binding);
ConfigBinding RuntimeConfig_GetBinding(unsigned game, unsigned role, unsigned slot);
int RuntimeConfig_HasPending(void);
int RuntimeConfig_ApplyFrame(int action_idle);
int RuntimeConfig_NeedsRestart(void);
const char *RuntimeConfig_Error(void);
/* 模板由同一描述表生成，构建工具和运行时缺失配置都使用这份定义。 */
int RuntimeConfig_DefaultText(char *output, size_t capacity, size_t *size);
#endif
