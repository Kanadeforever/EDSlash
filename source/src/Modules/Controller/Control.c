#include "Control.h"
#include <math.h>
#include <string.h>
#include <wchar.h>

float Control_Axis(int value, int deadzone)
{
    /* 死区以内视为松杆；死区以外重新缩放，避免刚跨过死区就突然跳到很快的速度。 */
    int magnitude = value < 0 ? -value : value;
    if (magnitude <= deadzone) return 0.0f;
    if (magnitude > 32767) magnitude = 32767;
    return (float)(magnitude - deadzone) / (float)(32767 - deadzone) * (value < 0 ? -1.0f : 1.0f);
}

void Control_WorldDirection(float sx, float sy, float *wx, float *wy)
{
    /* 原版等距投影的逆变换：世界 X=画面 X+2Y，世界 Y=2Y-画面 X。
       只转换方向，随后归一化，防止斜推时前探距离翻倍。 */
    float x = sx + 2.0f * sy, y = 2.0f * sy - sx;
    float length = sqrtf(x*x + y*y);
    *wx = length > 0.0f ? x / length : 0.0f;
    *wy = length > 0.0f ? y / length : 0.0f;
}

void Control_Stick(int raw_x, int raw_y, int deadzone, float *x, float *y)
{
    /* 先把整个摇杆当作二维向量，再判断离中心的距离。
       不能先分别砍掉横轴和纵轴：那会把接近水平/垂直的斜向吸到坐标轴上。 */
    float fx=(float)raw_x,fy=(float)raw_y;
    float radius=sqrtf(fx*fx+fy*fy);
    if (radius <= deadzone || radius == 0) { *x=0;*y=0;return; }
    /* 除以实际半径只取得方向；力度统一缩放，不改变方向角。
       有的设备在对角线能给出超过圆周的值，只限制力度到 1，不逐轴裁剪。 */
    float strength=(radius-deadzone)/(32767.0f-deadzone);
    if (strength>1) strength=1;
    *x=fx/radius*strength;*y=fy/radius*strength;
}

void Control_MoveGoal(int x, int y, float sx, float sy, int lead_tiles, int *mx, int *my)
{
    float dx, dy;
    Control_WorldDirection(sx, sy, &dx, &dy);
    /* 原版 opcode1/2 仍然收整数地图格，不能伪造浮点参数。用更远的连续射线目标
       减小最后一步格坐标量化的角度误差，四舍五入消除直接截断产生的方向偏置。
       前探距离不再由走/跑决定；速度意图继续通过原生走跑协议独立传递。 */
    *mx = (int)lroundf((float)x / 64.0f + dx * lead_tiles);
    *my = (int)lroundf((float)y / 64.0f + dy * lead_tiles);
}

Intent Control_Step(ControlState *s, const PadInput *in)
{
    Intent out;
    memset(&out, 0, sizeof out);
    bool neutral = !in->buttons && !in->lt && !in->rt &&
        in->lx == 0 && in->ly == 0 && in->rx == 0 && in->ry == 0;
    if (!in->connected || !in->focused) {
        /* 拔线或切出游戏后清掉本插件拥有的持续操作；重新接回必须先松开所有控制。 */
        s->ready = false; s->running = false; s->start_pending = false;
        s->previous = in->buttons; s->blocked = in->buttons; s->chord = false;
        s->previous_layer = LAYER_NONE; out.reset = true;
        return out;
    }
    if (!s->ready) {
        s->previous = in->buttons;
        if (neutral) {
            /* 中立帧也是旧按钮屏蔽的结束点，否则断线前的 X 会在重连后多吞一次。 */
            s->ready = true; s->blocked = 0; s->chord = false;
        }
        out.reset = true;
        return out;
    }
    uint32_t edge = in->buttons & ~s->previous;
    uint32_t combo = KEY(PAD_BACK) | KEY(PAD_START);
    if ((in->buttons & combo) == combo && !s->chord) {
        s->mouse = !s->mouse; s->running = false; s->chord = true;
        s->start_pending = false; out.mode_changed = true; out.reset = true;
        out.rumble_ms = s->mouse ? 200 : 1500;
    }
    /* 组合键任意一键还没松开时，继续吞掉这两键，防止返回手柄时顺便打开系统菜单。 */
    if (s->chord) {
        edge &= ~combo;
        if (!(in->buttons & combo)) s->chord = false;
    }
    Layer next = s->mouse ? LAYER_MOUSE : in->menu ? LAYER_MENU :
        in->rt ? LAYER_SKILL : in->lt ? LAYER_GUARD :
        (in->buttons & KEY(PAD_LB)) ? LAYER_MEDICINE :
        (in->buttons & KEY(PAD_RB)) ? LAYER_ITEM : LAYER_GAME;
    s->blocked &= in->buttons;
    if (next != s->previous_layer) {
        /* 只屏蔽进入新层前就一直按着的键。同帧新按的 X 可以属于 RT 层；
           松开 RT 但继续按 X 时，则不能突然变成左手攻击。 */
        s->blocked |= in->buttons & s->previous;
        out.reset = true;
    }
    out.layer = next;
    out.held = in->buttons & ~s->blocked;
    out.pressed = edge & ~s->blocked;
    if (s->chord || out.mode_changed) {
        out.held &= ~combo; out.pressed &= ~combo;
    }
    out.lx = in->lx; out.ly = in->ly; out.rx = in->rx; out.ry = in->ry;
    bool can_move = next == LAYER_GAME || next == LAYER_SKILL || next == LAYER_MEDICINE || next == LAYER_ITEM;
    if (can_move && (in->lx != 0 || in->ly != 0)) {
        if (next == LAYER_GAME && (out.pressed & KEY(PAD_L3))) s->running = true;
    } else s->running = false;
    out.run = s->running;
    /* START 延迟一个短窗口，让依次按下 BACK+START 也不会先弹系统菜单。 */
    if ((next == LAYER_GAME || next == LAYER_MENU) && (out.pressed & KEY(PAD_START))) {
        s->start_pending = true; s->start_at = in->now;
    }
    if (next != LAYER_GAME && next != LAYER_MENU) s->start_pending = false;
    if (s->start_pending && in->now - s->start_at >= 120 && !s->chord) {
        out.menu_toggle = true; s->start_pending = false;
    }
    if (out.mode_changed) {
        /* 切换当帧不执行鼠标按下或角色业务；扳机也要先松开再重新按。 */
        s->ready = false; out.held = out.pressed = 0;
    }
    s->previous = in->buttons; s->previous_layer = next;
    return out;
}

bool Control_FreshInput(const PadInput *in, const PadInput *old)
{
    if (!in->connected || !in->focused) return false;
    if ((in->buttons & ~old->buttons) || (in->lt && !old->lt) || (in->rt && !old->rt)) return true;
    /* 慢慢推杆也必须能接管：不能只看相邻两帧的差值，否则每帧变化很小会永远漏掉。 */
    if ((hypotf(old->lx,old->ly)<=0.15f && hypotf(in->lx,in->ly)>0.15f) ||
        (hypotf(old->rx,old->ry)<=0.15f && hypotf(in->rx,in->ry)>0.15f)) return true;
    /* 从中立推杆或明显改变方向/力度才算新操作，稳定按住和轻微漂移不夺回来源。
     * 与移动死区独立，只用于所有权交接，不改变实际方向和移动力度。 */
    return fabsf(in->lx-old->lx)>0.15f || fabsf(in->ly-old->ly)>0.15f ||
           fabsf(in->rx-old->rx)>0.15f || fabsf(in->ry-old->ry)>0.15f;
}

int Control_Percent(const wchar_t *text,int fallback)
{
    if (!text) return fallback;
    while (*text==L' ' || *text==L'\t') ++text;
    if (*text==L'-' && text[1]==L'1' && !text[2]) return -1;
    /* 把整数和小数拆开读取：最终单位是百分比的百分之一，12.50存成1250。 */
    unsigned whole=0,fraction=0,digits=0;
    bool found=false;
    while (*text>=L'0' && *text<=L'9') {
        found=true;whole=whole*10+(unsigned)(*text++-L'0');if (whole>100) return fallback;
    }
    if (*text==L'.') {
        ++text;
        while (*text>=L'0' && *text<=L'9') {
            if (++digits>2) return fallback;
            fraction=fraction*10+(unsigned)(*text++-L'0');
        }
        if (!digits) return fallback;
    }
    /* 5.5与5.50含义一致；第三位小数和超过100%的输入都使用默认值。 */
    if (digits==1) fraction*=10;
    while (*text==L' ' || *text==L'\t') ++text;
    if (!found || *text || whole*100+fraction>10000) return fallback;
    return (int)(whole*100+fraction);
}
