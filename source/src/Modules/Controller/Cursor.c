#include "Cursor.h"
#include "Menu.h"
#include <string.h>

static BYTE saved[3][6];
static bool installed;
static BOOL (WINAPI *native_position)(LPPOINT);
/* 原精灵Draw有六个栈参数，游戏以ret24清栈；绘制目标不是鼠标管理对象。 */
typedef int (__attribute__((thiscall)) *SpriteDraw)(void *,void *,int,int,int,int,int);
static bool pad_visual(void)
{
    return installed && g_input.connected && g_input.focused && g_intent.layer!=LAYER_NONE &&
        g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_MOUSE;
}
static BOOL WINAPI position_hook(LPPOINT point)
{
    /* 只替换软件光标绘制函数里那一次坐标取得。环形旧矩形记录和背景恢复仍会运行，
     * 因此焦点改变后不会留下旧图样；真正的世界/GUI采样GetCursorPos完全没有修改。 */
    if (pad_visual() && Menu_CursorAnchor(point)) return TRUE;
    return native_position(point);
}
static int __attribute__((fastcall)) sprite_hook(void *self,void *unused,
    void *surface,int x,int y,int frame,int shade,int flags)
{
    (void)unused;
    POINT anchor;
    /* 普通手柄只在有效菜单焦点画原图样；世界或未知页隐藏图样。
     * 物理来源和BACK+START救援模式完整保留原位置、精灵、动画、颜色与参数。 */
    if (pad_visual() && (Menu_HidesCursor() || !Menu_CursorAnchor(&anchor))) return 0;
    return ((SpriteDraw)g_profile->cursor_sprite_draw)(self,surface,x,y,frame,shade,flags);
}
static uintptr_t target(unsigned i)
{
    return i==0 ? g_profile->cursor_position_call:i==1 ? g_profile->cursor_sprite_call1:g_profile->cursor_sprite_call2;
}
static void bytes(unsigned i,BYTE *out)
{
    uintptr_t function=i==0 ? (uintptr_t)position_hook:(uintptr_t)sprite_hook;
    memset(out,0x90,6);out[0]=0xE8;
    int32_t relative=(int32_t)(function-target(i)-5);memcpy(out+1,&relative,4);
}
void Cursor_Shutdown(void)
{
    if (!g_profile) return;
    for (unsigned i=0;i<3;++i) {
        BYTE expected[6];bytes(i,expected);unsigned length=i==0 ? 6:5;
        if (Memory_Readable((void *)target(i),length) && !memcmp((void *)target(i),expected,length))
            Memory_Patch((void *)target(i),saved[i],length);
    }
    installed=false;
}
bool Cursor_Initialize(void)
{
    if (installed) return true;
    for (unsigned i=0;i<3;++i) {
        unsigned length=i==0 ? 6:5;
        if (!Memory_Readable((void *)target(i),length)) return false;
        memcpy(saved[i],(void *)target(i),length);
        int32_t relative;memcpy(&relative,saved[i]+1,4);
        if (i==0) {
            uintptr_t iat;memcpy(&iat,saved[0]+2,4);
            if (saved[0][0]!=0xFF || saved[0][1]!=0x15 || iat!=g_profile->cursor_position_iat ||
                !Memory_Readable((void *)iat,4)) return false;
            native_position=*(BOOL (WINAPI **)(LPPOINT))iat;
            if (!native_position) return false;
        } else if (saved[i][0]!=0xE8 || target(i)+5+relative!=g_profile->cursor_sprite_draw) return false;
    }
    if (!HookManager_Claim(SHARED_HOOK_CONTROLLER_CURSOR,RUNTIME_MODULE_CONTROLLER)) return false;
    for (unsigned i=0;i<3;++i) {
        BYTE replacement[6];bytes(i,replacement);
        if (!Memory_Patch((void *)target(i),replacement,i==0 ? 6:5)) {
            Cursor_Shutdown();return false;
        }
    }
    installed=true;Log_Write("[光标] 原鼠标图样作为菜单右下角焦点标记；手柄世界隐藏，物理来源恢复。");return true;
}
