#include "Cursor.h"
#include "Menu.h"
#include "Crash.h"
#include <string.h>

static BYTE saved[3][6];
static bool installed;
static BOOL (WINAPI *native_position)(LPPOINT);
static bool draw_subscribed,frame_visible;
/* 原精灵Draw有六个栈参数，游戏以ret24清栈；绘制目标不是鼠标管理对象。 */
typedef int (__attribute__((thiscall)) *SpriteDraw)(void *,void *,int,int,int,int,int);
/* 十参数入口只裁取源图矩形，不会缩放。每一次裁取都必须落在真实源图范围内。 */
typedef int (__attribute__((thiscall)) *RectDraw)(void *,void *,int,int,int,int,int,int,int,int,void *);
static bool pad_visual(void)
{
    return installed && g_input.connected && g_input.focused && g_intent.layer!=LAYER_NONE &&
        g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_MOUSE;
}
static void tile(void *frame,void *surface,int x,int y,int width,int height,int sx,int sy,
                 int source_width,int source_height,int mode,int shade)
{
    /* 即使上层尺寸有问题也拒绝越界，不把目标矩形直接当源图复制范围。 */
    if (width<=0 || height<=0 || sx<0 || sy<0 || sx+width>source_width || sy+height>source_height) return;
    Crash_Stage("焦点框：原源图矩形裁取");
    ((RectDraw)g_profile->focus_rect_draw)(frame,surface,x,y,width,height,sx,sy,mode,shade,NULL);
}
static bool border(void *frame,void *surface,const RECT *r,int sw,int sh,int mode,int shade)
{
    int width=r->right-r->left,height=r->bottom-r->top;
    if (width==sw && height==sh) {
        /* 快捷格通常和原框同尺寸：使用游戏原常规Draw，完全不走裁取入口。 */
        Crash_Stage("焦点框：原常规动态精灵");
        ((SpriteDraw)g_profile->cursor_sprite_draw)(frame,surface,r->left,r->top,mode,shade,0);return true;
    }
    /* 较大的候选框按九块拼接：四角不变，四边重复原边缘片段，中央透明不画。
     * 这不是拉伸或自制新素材，每段复制的尺寸均在原图内。最大128，绘制次数有界。 */
    const int b=2;int cw=sw-b*2,ch=sh-b*2;
    if (cw<=0 || ch<=0 || width<b*2 || height<b*2 || width>128 || height>128) return false;
    tile(frame,surface,r->left,r->top,b,b,0,0,sw,sh,mode,shade);
    tile(frame,surface,r->right-b,r->top,b,b,sw-b,0,sw,sh,mode,shade);
    tile(frame,surface,r->left,r->bottom-b,b,b,0,sh-b,sw,sh,mode,shade);
    tile(frame,surface,r->right-b,r->bottom-b,b,b,sw-b,sh-b,sw,sh,mode,shade);
    for(int x=b;x<width-b;) {
        int n=width-b-x;if(n>cw)n=cw;
        tile(frame,surface,r->left+x,r->top,n,b,b,0,sw,sh,mode,shade);
        tile(frame,surface,r->left+x,r->bottom-b,n,b,b,sh-b,sw,sh,mode,shade);x+=n;
    }
    for(int y=b;y<height-b;) {
        int n=height-b-y;if(n>ch)n=ch;
        tile(frame,surface,r->left,r->top+y,b,n,0,b,sw,sh,mode,shade);
        tile(frame,surface,r->right-b,r->top+y,b,n,sw-b,b,sw,sh,mode,shade);y+=n;
    }
    return true;
}
static void focus_render(RuntimeEventId event,void *subject,unsigned long surface,unsigned long value,void *user)
{
    (void)subject;(void)value;(void)user;frame_visible=false;
    if (event!=RUNTIME_EVENT_UI_DRAW_END || !pad_visual() || !surface) return;
    RECT rectangle;if (!ActionMenu_FocusFrame(&rectangle) && !Menu_FocusFrame(&rectangle)) return;
    void *hud=ReadPtr((void *)g_profile->skill_global,0);unsigned count=Read32(hud,0x44);
    BYTE *sprites=ReadPtr(hud,0x48);
    /* 资源缺少原框时拒绝，不把整张HUD背景精灵当成选择框。 */
    if (!Read32(hud,0x64) || count<=5 || count>64 || !Memory_Readable(sprites,count*32u)) return;
    void *frame=sprites+5*32u;unsigned frames=Read32(frame,8),index=Read32(frame,4);
    void *data=ReadPtr(frame,0);
    /* 一帧记录22字节。先检查动画表和当前下标，再调用原Frame getter，不让空精灵进原函数。 */
    if (!frames || frames>4096 || index>=frames || !Memory_Readable(data,frames*22u)) return;
    Crash_Stage("焦点框：原动画帧读取");
    void *description=(void *)(uintptr_t)((This0)g_profile->focus_frame_get)(frame);
    if (!Memory_Readable(description,22)) return;
    /* 图像getter会解引用帧里的资源管理器和登记表，不能只验证帧表本身。
     * 尚未登记的资源只拒绝绘制，不让原加载分支对空登记对象继续读取。 */
    void *bank=ReadPtr(description,8),*entries=ReadPtr(bank,4);
    unsigned resource=Read32(description,4)&0xFFFFFu;
    if (!Memory_Readable(bank,8) || !Memory_Readable((BYTE *)entries+resource*4u,4) ||
        !Memory_Readable(ReadPtr(entries,resource*4u),0x28)) return;
    Crash_Stage("焦点框：原图像读取");
    void *image=(void *)(uintptr_t)((This0)g_profile->focus_image_get)(description);
    if (!Memory_Readable(image,0x14) || !Memory_Readable((void *)(uintptr_t)surface,0x14)) return;
    int sw=(int)Read32(image,0xC),sh=(int)Read32(image,0x10);
    int dw=(int)Read32((void *)(uintptr_t)surface,0xC),dh=(int)Read32((void *)(uintptr_t)surface,0x10);
    if (sw<5 || sh<5 || sw>512 || sh>512 || dw<=0 || dh<=0 || dw>65536 || dh>65536) return;
    static unsigned logged_kind;
    unsigned kind=ActionMenu_Active() ? 2u:1u;
    if(!(logged_kind&kind)) {
        logged_kind|=kind;
        Log_Write("[焦点绘制] %s 原图=%d,%d 目标=%ld,%ld；同尺寸原Draw，异尺寸原图边缘拼接，禁止越界裁取。",
            kind==2 ? "动作菜单":"快捷栏",sw,sh,(long)(rectangle.right-rectangle.left),(long)(rectangle.bottom-rectangle.top));
    }
    frame_visible=border(frame,(void *)(uintptr_t)surface,&rectangle,sw,sh,(int)Read32(hud,0x58),(int)Read32(hud,0x5C));
    Crash_Stage("手柄普通运行");
}
static void focus_draw(RuntimeEventId event,void *subject,unsigned long surface,unsigned long value,void *user)
{
    focus_render(event,subject,surface,value,user);
    /* 无效资源的提前返回也清诊断阶段，不能让之后无关异常显示为旧焦点绘制。 */
    Crash_Stage("手柄普通运行");
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
    if (pad_visual()) {
        bool hiding=Menu_HidesCursor();RECT rectangle;
        /* 原框资源尚未就绪时保留旧箭头作为临时焦点，不能隐藏后留下无指示菜单。
         * 持有物品本来就不隐藏，其原图标继续用中心锚点。外传标题仍沿原隐藏规则。 */
        if (ActionMenu_Active() || Menu_FocusFrame(&rectangle)) hiding=hiding && frame_visible;
        if (hiding || !Menu_CursorAnchor(&anchor)) return 0;
    }
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
    Crash_Shutdown();
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
    if (!draw_subscribed) {
        /* 订阅已经存在的绘制结束事件，不占HUD Draw槽或改变DisplayFix绘制次序。
         * 注册失败撤掉光标包装，不能留下隐藏箭头却没有新焦点框的半装状态。 */
        if (!g_profile->focus_rect_draw || !Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_END,focus_draw,NULL)) {
            Cursor_Shutdown();return false;
        }
        draw_subscribed=true;
    }
    if(!Crash_Initialize()) Log_Write("[诊断] 未能注册异常地址记录，普通输入仍可用。");
    installed=true;Log_Write("[光标] 原鼠标图样作为菜单右下角焦点标记；手柄世界隐藏，物理来源恢复。");return true;
}
