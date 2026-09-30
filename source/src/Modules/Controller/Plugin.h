#ifndef EDSLASH_PLUGIN_H
#define EDSLASH_PLUGIN_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include "Control.h"

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
bool Profile_Select(void);
bool Profile_Pe(void);
bool Profile_Verify(void);
bool Input_Poll(PadInput *input);
void Input_Rumble(unsigned ms);
void Input_Mouse(bool enabled);
void Input_ReleaseMouse(void);
bool Input_PhysicalDown(int key);
int Config_Number(const WCHAR *section, const WCHAR *key, int fallback, int minimum, int maximum);
bool Game_Menu(void);
void Game_Update(void);
void Game_Release(void);
void Game_Keyboard(BYTE *keys);
SHORT Game_Async(int key, SHORT native);
void Game_Diagnose(void);
void *Game_Resolve(uint32_t handle);
bool Game_Enemy(void *role, void *candidate);

/* GCC 的 thiscall 会把首参数放 ECX，其余压栈，并由游戏函数清栈。
   这些类型依据两份 EXE 的 ret 4/ret 0x10 等真实指令核对，不使用逻辑伪原型。 */
typedef int (__attribute__((thiscall)) *This0)(void *);
typedef int (__attribute__((thiscall)) *This1)(void *, int);
typedef int (__attribute__((thiscall)) *This2)(void *, int, int);
typedef int (__attribute__((thiscall)) *This3)(void *, int, int, void *);
typedef int (__attribute__((thiscall)) *This4)(void *, int, int, int, int);
#endif
