#ifndef EDSLASH_PLUGIN_H
#define EDSLASH_PLUGIN_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include "Control.h"
#include "../../Runtime/Config.h"
#include "../../Runtime/Runtime.h"

/* 每一项都属于一份已经核对的非 Steam EXE，禁止在业务函数内猜另一版本的地址。 */
typedef struct {
    const char *name, *sha256;
    uint32_t entry, size;
    uintptr_t keyboard_iat, async_iat, keyboard_buffer, resolver_call, resolver;
    uintptr_t mouse_global, world_global, entities_global, handles_global, ui, skill_global;
    uintptr_t submit, install_state, skill_release, left_get, right_get, right_set;
    uintptr_t inspect_gate, relation, template_value, hover_set, get_jm;
    uintptr_t quick_use, left_set, projection, world_to_grid;
    uintptr_t ui_property;
    uintptr_t skill_groups, methods, game_tick, combo_timeout;
    uintptr_t lookup, skill_eligibility, role_method, method_usable, method_distance;
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
    uintptr_t menu_title_vtable,menu_system_vtable,menu_confirm_vtable;
    uintptr_t menu_title_activate,menu_texture,menu_animation_reset;
    uintptr_t menu_system_primary,menu_confirm_submit;
    uintptr_t menu_title_tick,menu_title_show,menu_title_hover;
    uintptr_t menu_system_tick,menu_system_show,menu_system_hover;
    uintptr_t menu_confirm_tick,menu_confirm_show,menu_confirm_hover;
    /* 读档页与原生软件光标绘制入口，不改世界鼠标采样或真实光标坐标。 */
    uintptr_t menu_load_vtable,menu_load_tick,menu_load_show,menu_load_hover;
    uintptr_t menu_load_select,menu_load_page,menu_load_submit,menu_load_primary,menu_sound;
    uintptr_t cursor_position_call,cursor_position_iat,cursor_sprite_call1,cursor_sprite_call2,cursor_sprite_draw;
    uintptr_t inspect_hover_call,inspect_hover,inspect_portal_gate;
    uintptr_t menu_message_vtable,menu_message_global,menu_message_open,menu_message_text_lookup,menu_message_text_table;
    uintptr_t menu_message_tick,menu_message_show,menu_message_hover,menu_message_primary,menu_message_base_tick;
    uintptr_t inspect_static_gate,inspect_map_global,inspect_basic_get;
    uintptr_t menu_talk_vtable,menu_talk_tick,menu_talk_show,menu_talk_hover,menu_talk_select,menu_talk_cancel;
    uintptr_t menu_text_vtable,menu_text_tick,menu_text_show,menu_text_hover,menu_text_next;
    uintptr_t menu_talk_picker_call,menu_talk_picker,menu_talk_delay_global;
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
void Game_Keyboard(BYTE *keys);
SHORT Game_Async(int key, SHORT native);
void Game_Diagnose(void);
void *Game_Resolve(uint32_t handle);
bool Game_Enemy(void *role, void *candidate);
/* 以世界控制句柄解析当前玩家，供输入来源交接及防御业务使用。 */
void *Game_Player(void);

/* GCC 的 thiscall 会把首参数放 ECX，其余压栈，并由游戏函数清栈。
   这些类型依据两份 EXE 的 ret 4/ret 0x10 等真实指令核对，不使用逻辑伪原型。 */
typedef int (__attribute__((thiscall)) *This0)(void *);
typedef int (__attribute__((thiscall)) *This1)(void *, int);
typedef int (__attribute__((thiscall)) *This2)(void *, int, int);
typedef int (__attribute__((thiscall)) *This3)(void *, int, int, void *);
typedef int (__attribute__((thiscall)) *This4)(void *, int, int, int, int);
#endif
