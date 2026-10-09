#include "ControllerText.h"
#include "Cursor.h"
#include "Menu.h"
#include "Crash.h"
#include "../../Runtime/Focus.h"
#include "../../Runtime/SettingsWindow.h"
#include <string.h>

static BYTE saved[4][6];
static bool installed;
static BOOL (WINAPI *native_position)(LPPOINT);
/* Controller只提供当前焦点，所有原素材/动画/源范围/绘制均由Runtime负责。 */
typedef int (__fastcall *SpriteDraw)(void *, void *,void *,int,int,int,int,int);
static bool pad_visual(void)
{
    return installed && g_input.connected && g_input.focused && g_intent.layer!=LAYER_NONE &&
        g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_MOUSE;
}
static int provide_focus(RuntimeFocusRequest *request,void *user)
{
    (void)user;if(!pad_visual())return 0;
    RECT r;
    if(ActionMenu_FocusFrame(&r)){request->priority=100;request->icon_phase=1;}
    else if(Menu_FocusFrame(&r)) {
        unsigned reason;void *root=Menu_Context(&reason);
        request->priority=Read32(root,0)==g_profile->menu_message_vtable ? 100:50;
        request->icon_phase=Read32(root,0)==g_profile->menu_skill_vtable;
    }
    else return 0;
    request->rectangle=(RuntimeFocusRect){r.left,r.top,r.right,r.bottom};return 1;
}
static BOOL WINAPI position_hook(LPPOINT point)
{
    /* 只替换软件光标绘制函数里那一次坐标取得。环形旧矩形记录和背景恢复仍会运行，
     * 因此焦点改变后不会留下旧图样；真正的世界/GUI采样GetCursorPos完全没有修改。 */
    if(SettingsWindow_Active())return native_position(point);
    if (pad_visual() && (Game_JumpAnchor(point) || Menu_CursorAnchor(point))) return TRUE;
    return native_position(point);
}
static int __fastcall sprite_hook(void *self,void *unused,
    void *surface,int x,int y,int frame,int shade,int flags)
{
    (void)unused;
    POINT anchor;
    if(SettingsWindow_Active()) {
        if(!SettingsWindow_ShowPointer())return 0;
        return ((SpriteDraw)g_profile->cursor_sprite_draw)(self, NULL,surface,x,y,frame,shade,flags);
    }
    /* 普通手柄只在有效菜单焦点画原图样；世界或未知页隐藏图样。
     * 物理来源和BACK+START救援模式完整保留原位置、精灵、动画、颜色与参数。 */
    if (pad_visual()) {
        bool jump_preview=Game_JumpAnchor(&anchor);
        bool hiding=!jump_preview && Menu_HidesCursor();RECT rectangle;
        /* 原框资源尚未就绪时保留旧箭头作为临时焦点，不能隐藏后留下无指示菜单。
         * 持有物品本来就不隐藏，其原图标继续用中心锚点。外传标题仍沿原隐藏规则。 */
        if (ActionMenu_Active() || Menu_FocusFrame(&rectangle)) hiding=hiding && RuntimeFocus_WasDrawn(RUNTIME_MODULE_CONTROLLER);
        if (hiding || (!jump_preview && !Menu_CursorAnchor(&anchor))) return 0;
    }
    return ((SpriteDraw)g_profile->cursor_sprite_draw)(self, NULL,surface,x,y,frame,shade,flags);
}
static int __fastcall icon_sprite_hook(void *self,void *unused,
    void *surface,int x,int y,int frame,int shade,int flags)
{
    (void)unused;
    int result=((SpriteDraw)g_profile->cursor_sprite_draw)(self, NULL,surface,x,y,frame,shade,flags);
    if(pad_visual())RuntimeFocus_DrawIcon((unsigned long)(uintptr_t)surface,x,y);
    return result;
}
static uintptr_t target(unsigned i)
{
    return i==0 ? g_profile->cursor_position_call:i==1 ? g_profile->cursor_sprite_call1:i==2 ? g_profile->cursor_sprite_call2:g_profile->icon_focus_call;
}
static void bytes(unsigned i,BYTE *out)
{
    uintptr_t function=i==0 ? (uintptr_t)position_hook:i==3 ? (uintptr_t)icon_sprite_hook:(uintptr_t)sprite_hook;
    memset(out,0x90,6);out[0]=0xE8;
    int32_t relative=(int32_t)(function-target(i)-5);memcpy(out+1,&relative,4);
}
void Cursor_Shutdown(void)
{
    Crash_Shutdown();RuntimeFocus_Unregister(RUNTIME_MODULE_CONTROLLER);
    if (!g_profile) return;
    for (unsigned i=0;i<4;++i) {
        BYTE expected[6];bytes(i,expected);unsigned length=i==0 ? 6:5;
        if (Memory_Readable((void *)target(i),length) && !memcmp((void *)target(i),expected,length))
            Memory_Patch((void *)target(i),saved[i],length);
    }
    installed=false;
}
bool Cursor_Initialize(void)
{
    if (installed) return true;
    for (unsigned i=0;i<4;++i) {
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
    for (unsigned i=0;i<4;++i) {
        BYTE replacement[6];bytes(i,replacement);
        if (!Memory_Patch((void *)target(i),replacement,i==0 ? 6:5)) {
            Cursor_Shutdown();return false;
        }
    }
    if(!RuntimeFocus_Register(RUNTIME_MODULE_CONTROLLER,provide_focus,NULL)) {
        Cursor_Shutdown();return false;
    }
    if(!Crash_Initialize()) Log_Write(ControllerText_Cursor_CrashRecorderUnavailableLog);
    installed=true;Log_Write(ControllerText_Cursor_FocusFeedbackReadyLog);return true;
}
