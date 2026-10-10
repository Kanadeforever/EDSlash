#ifndef EDSLASH_PLUGIN_H
#define EDSLASH_PLUGIN_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include "Control.h"
#include "../../Runtime/Config.h"
#include "../../Runtime/Runtime.h"

/* 每一项都属于一份已经核对的Steam／非Steam EXE，禁止在业务函数内猜另一版本的地址。 */
typedef struct {
    const char *name, *sha256;
    unsigned game_id;
    uint32_t entry, size;
    uintptr_t keyboard_iat, async_iat, keyboard_buffer, resolver_call, resolver;
    uintptr_t mouse_global, world_global, entities_global, handles_global, ui, skill_global;
    uintptr_t submit, install_state, skill_release, left_get, right_get, right_set;
    uintptr_t inspect_gate, relation, template_value, hover_set, get_jm;
    uintptr_t quick_use, left_set, projection, world_to_grid;
    uintptr_t ui_property;
    uintptr_t skill_groups, methods, game_tick, combo_timeout;
    uintptr_t lookup, skill_eligibility, role_method, method_usable, method_distance;
    uintptr_t method_range; /* 原Runtime有效距离档getter，每档64世界单位；不是裸距离。 */
    uintptr_t facing_point, facing_direction, direction8, history_call, history_record;
    uintptr_t retry_call;
    uintptr_t end_call,end_record;
    uintptr_t history_clear;
    uintptr_t guard_get,stamina_adjust,dodge_start,guard_check;
    uintptr_t hit_receiver,runtime_owner;
    uintptr_t combo_get,inventory_root,inventory_get,item_at;
    uintptr_t guard_periodic_call,guard_hit_call,guard_input_release_call;
    uintptr_t guard_run_call;
    uintptr_t dodge_gate,dodge_legacy,dodge_resume,dodge_failure,guard_threshold;
    uintptr_t prepared_set,icon_resolve,right_icon_call,icon_draw,throw_group;
    uintptr_t dodge_init_call,dodge_init,dodge_motion_call,dodge_motion;
    uintptr_t map_bounds,cell_passable,grid_commit,role_effect;
    uintptr_t animation_get,animation_finished,guard_angle_gate;
    /* PlayerInit确认的全局角色类别，不能使用对象分类字段代替。 */
    uintptr_t player_class_global,player_class_probe;
    /* 菜单入口成对来自两份准确基线；虚表槽另核原函数，不能猜其它GUI的回调。 */
    /* 原版设置页：四版本分别核对原控件调值、实时应用和返回保存入口。 */
    /* 新游戏两层只复用原角色提交和档案验证；名称由原EDIT/QOL独立处理。 */
    uintptr_t menu_character_vtable,menu_character_tick,menu_character_show,menu_character_hover,menu_character_primary;
    uintptr_t menu_newgame_vtable,menu_newgame_tick,menu_newgame_show,menu_newgame_hover,menu_newgame_primary,menu_newgame_cycle;
    uintptr_t menu_name_vtable,menu_name_set;
    uintptr_t menu_native_text_draw;
    uintptr_t menu_settings_vtable,menu_settings_tick,menu_settings_show,menu_settings_hover;
    uintptr_t menu_settings_primary,menu_settings_close,menu_settings_apply,menu_settings_slider_set;
    /* 原Tab分支使用的地图对象和Show业务，避免依赖键盘锁存。 */
    uintptr_t menu_map_global,menu_map_vtable,menu_map_show;
    uintptr_t menu_title_vtable,menu_system_vtable,menu_confirm_vtable;
    uintptr_t menu_title_activate,menu_texture,menu_animation_reset;
    uintptr_t menu_system_primary,menu_confirm_submit;
    uintptr_t menu_title_tick,menu_title_show,menu_title_hover;
    uintptr_t menu_system_tick,menu_system_show,menu_system_hover;
    uintptr_t menu_confirm_tick,menu_confirm_show,menu_confirm_hover;
    /* 读档页与原生软件光标绘制入口，不改世界鼠标采样或真实光标坐标。 */
    uintptr_t menu_load_vtable,menu_load_tick,menu_load_show,menu_load_hover;
    uintptr_t menu_load_select,menu_load_page,menu_load_submit,menu_load_primary,menu_sound;
    uintptr_t settings_actor_get,settings_string_get,settings_icon_global;
    uintptr_t settings_skill_name,settings_skill_description,settings_string_destroy,settings_query_skill,settings_empty_string,settings_text_get,settings_text_table;
    uintptr_t projection_global; /* 原屏幕到世界投影对象，用其原函数反算预览屏幕坐标。 */
    uintptr_t icon_focus_call; /* 原图标绘制之后、原说明绘制之前的CALL。 */
    uintptr_t cursor_position_call,cursor_position_iat,cursor_sprite_call1,cursor_sprite_call2,cursor_sprite_draw;
    uintptr_t inspect_hover_call,inspect_hover,inspect_portal_gate;
    uintptr_t menu_message_vtable,menu_message_global,menu_message_open,menu_message_text_lookup,menu_message_text_table;
    uintptr_t menu_message_tick,menu_message_show,menu_message_hover,menu_message_primary,menu_message_base_tick;
    /* 静态选择器本身也参与签名与地图地址核对，防止正确函数搭配错误全局指针。 */
    uintptr_t inspect_static_picker,inspect_static_gate,inspect_map_global,inspect_basic_get;
    uintptr_t menu_talk_vtable,menu_talk_tick,menu_talk_show,menu_talk_hover,menu_talk_select,menu_talk_cancel;
    uintptr_t menu_text_vtable,menu_text_tick,menu_text_show,menu_text_hover,menu_text_next;
    uintptr_t menu_talk_picker_call,menu_talk_picker,menu_talk_delay_global;
    /* 日志分类和技能页分开调用原业务，不能以通用鼠标点击代替。 */
    uintptr_t menu_quest_vtable,menu_quest_tick,menu_quest_show,menu_quest_hover;
    uintptr_t menu_quest_primary,menu_quest_switch,menu_quest_select,menu_quest_position;
    uintptr_t menu_skill_vtable,menu_skill_tick,menu_skill_show,menu_skill_hover;
    uintptr_t menu_skill_primary,menu_skill_secondary,menu_skill_switch,menu_skill_slot;
    uintptr_t menu_skill_base_call,menu_skill_combo_hit,menu_skill_combo_call1,menu_skill_combo_call2;
    uintptr_t menu_skill_combo_capacity;
    /* 两种50格界面：格号只提供焦点，实际物品操作仍交原入口。 */
    uintptr_t menu_bag_vtable,menu_bag_tick,menu_bag_show,menu_bag_hover,menu_bag_primary,menu_bag_secondary,menu_bag_global;
    uintptr_t menu_storage_vtable,menu_storage_tick,menu_storage_show,menu_storage_hover,menu_storage_primary,menu_storage_secondary,menu_storage_global;
    uintptr_t menu_item_swap,menu_bag_empty;
    /* 特殊物品区以真实子控件为焦点，商店仍有独立商品格号。 */
    uintptr_t menu_shop_vtable,menu_shop_tick,menu_shop_show,menu_shop_hover,menu_shop_primary,menu_shop_switch,menu_shop_submit;
    uintptr_t menu_craft_vtable,menu_craft_tick,menu_craft_show,menu_craft_hover,menu_craft_primary;
    uintptr_t menu_inlay_vtable,menu_inlay_tick,menu_inlay_show,menu_inlay_hover,menu_inlay_primary;
    uintptr_t menu_charm_vtable,menu_charm_tick,menu_charm_show,menu_charm_hover,menu_charm_primary;
    uintptr_t menu_shop_position_call1,menu_shop_position_call2;
    uintptr_t menu_inlay_position_call;
    /* HUD快捷栏与原左右手动作选择器；来源必须从各自虚表/原调用解码核对。 */
    uintptr_t focus_rect_draw,focus_frame_get,focus_image_get,menu_item_drop;
    uintptr_t menu_hud_vtable,menu_hud_tick,menu_hud_show,menu_hud_hover,menu_hud_primary,menu_hud_hit;
    uintptr_t menu_action_vtable,menu_action_global,menu_action_open,menu_action_rebuild;
    uintptr_t menu_action_commit,menu_action_tick,menu_action_hover;
    unsigned inspect_ready_offset;
    unsigned dodge_counter_offset;
    unsigned stamina_offset;
    unsigned health_offset;
    unsigned pending_offset;
    unsigned invalid_offset, interact_offset, active_offset;
} Profile;
typedef struct { uintptr_t address; unsigned char bytes[12]; } Signature;
extern const Profile *g_profile;
extern Intent g_intent;
extern PadInput g_input;
extern WCHAR g_directory[MAX_PATH];
extern HWND g_window;

void Log_Write(const char *format, ...);
bool Memory_Readable(const void *pointer, size_t bytes);
uint32_t Read32(const void *base, unsigned offset);
void *ReadPtr(const void *base, unsigned offset);
void Write32(void *base, unsigned offset, uint32_t value);
bool Memory_Patch(void *address,const void *bytes,size_t count);
bool Profile_Select(void);
bool Profile_Attach(GameId game);
bool Profile_Verify(void);
bool Input_Initialize(void);
void Input_Shutdown(void);
bool Input_Poll(PadInput *input);
void Input_Rumble(unsigned ms);
void Input_Mouse(bool enabled);
void Input_ReleaseMouse(void);
bool Input_PhysicalDown(int key);
bool Game_Menu(void);
void Game_Update(void);
void Game_Release(void);
/* 只供软件指针绘制使用，不写MouseManager、真实鼠标或角色位置。 */
bool Game_JumpAnchor(POINT *point);
void Game_Keyboard(BYTE *keys);
SHORT Game_Async(int key, SHORT native);
void Game_Diagnose(void);
void *Game_Resolve(uint32_t handle);
bool Game_Enemy(void *role, void *candidate);
/* 以世界控制句柄解析当前玩家，供输入来源交接及防御业务使用。 */
void *Game_Player(void);

/* 游戏thiscall使用ECX传self，其余参数压栈，并由游戏函数清栈。
   MSVC C使用fastcall桥接：第二参数占用EDX但游戏不读取它，因此调用时传NULL。
   这些类型依据准确EXE的ret 4/ret 0x10等指令核对，空EDX参数不会增加栈参数。 */
typedef int (__fastcall *This0)(void *, void *);
typedef int (__fastcall *This1)(void *, void *, int);
typedef int (__fastcall *This2)(void *, void *, int, int);
typedef int (__fastcall *This3)(void *, void *, int, int, void *);
typedef int (__fastcall *This4)(void *, void *, int, int, int, int);
#endif
