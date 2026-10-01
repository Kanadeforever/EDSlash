#ifndef EDSLASH_CONTROL_H
#define EDSLASH_CONTROL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 按钮编号与 SDL3 的标准布局一致。这里保存位置名称，不依赖手柄印的是哪种字母。 */
enum { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_GUIDE, PAD_START,
       PAD_L3, PAD_R3, PAD_LB, PAD_RB, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT };
#define KEY(b) (1u << (b))
typedef enum { LAYER_NONE, LAYER_GAME, LAYER_MENU, LAYER_MOUSE,
               LAYER_SKILL, LAYER_MEDICINE, LAYER_ITEM, LAYER_GUARD,
               LAYER_NATIVE } Layer;
typedef struct {
    uint32_t buttons;
    float lx, ly, rx, ry;
    bool lt, rt, connected, focused, menu;
    uint32_t now;
} PadInput;
typedef struct {
    Layer layer;
    uint32_t pressed, held;
    float lx, ly, rx, ry;
    bool run, menu_toggle, mode_changed, reset;
    unsigned rumble_ms;
} Intent;
typedef struct {
    uint32_t previous, blocked, start_at;
    Layer previous_layer;
    bool mouse, running, ready, start_pending, chord;
} ControlState;

/* 纯规则层不读游戏内存。测试程序可以直接喂入按键，检查组合键是否误触发基础动作。 */
Intent Control_Step(ControlState *state, const PadInput *input);
float Control_Axis(int value, int deadzone);
void Control_Stick(int raw_x, int raw_y, int deadzone, float *x, float *y);
void Control_WorldDirection(float screen_x, float screen_y, float *world_x, float *world_y);
void Control_MoveGoal(int world_x, int world_y, float sx, float sy, int lead_tiles, int *map_x, int *map_y);
/* 检查新的手柄操作，而不是持续按住的旧操作；避免两套输入互相夺回控制。 */
bool Control_FreshInput(const PadInput *current, const PadInput *previous);
#endif
