#include "Focus.h"
#include "Win32Bridge.h"
#include "Log.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned char BYTE;
typedef int (__attribute__((thiscall)) *This0)(void *);
typedef int (__attribute__((thiscall)) *SpriteDraw)(void *,void *,int,int,int,int,int);
typedef int (__attribute__((thiscall)) *RectDraw)(void *,void *,int,int,int,int,int,int,int,int,void *);
typedef struct FocusBackend {
    uintptr_t hud_global,hud_vtable,sprite_draw,rect_draw,frame_get,image_get;
    BYTE signatures[4][12];
} FocusBackend;
#include "FocusData.h"
static FocusBackend backend;
static bool ready,subscribed;
static RuntimeModuleId drawn_owner;
static struct {RuntimeFocusProvider callback;void *user;} providers[RUNTIME_MODULE_COUNT];
static const char *stage;
static void set_stage(const char *value){stage=value;}
const char *RuntimeFocus_Stage(void){return stage;}
static bool focus_readable(const void *p,size_t bytes)
{return p && bytes && bytes<=UINT32_MAX && RuntimeWin32_IsReadable((unsigned long)(uintptr_t)p,(unsigned long)bytes);}
static uint32_t focus_read32(const void *p,unsigned offset)
{uint32_t result=0;if(p)RuntimeWin32_Read((unsigned long)(uintptr_t)p+offset,&result,4);return result;}
static void *focus_readptr(const void *p,unsigned offset){return (void *)(uintptr_t)focus_read32(p,offset);}
static void tile(void *frame,void *surface,int x,int y,int width,int height,int sx,int sy,
                 int source_width,int source_height,int mode,int shade)
{
    /* 即使上层尺寸有问题也拒绝越界，不把目标矩形直接当源图复制范围。 */
    if (width<=0 || height<=0 || sx<0 || sy<0 || sx+width>source_width || sy+height>source_height) return;
    set_stage("焦点框：原源图矩形裁取");
    ((RectDraw)backend.rect_draw)(frame,surface,x,y,width,height,sx,sy,mode,shade,NULL);
}
static bool border(void *frame,void *surface,const RuntimeFocusRect *r,int sw,int sh,int mode,int shade)
{
    int width=r->right-r->left,height=r->bottom-r->top;
    if (width==sw && height==sh) {
        /* 快捷格通常和原框同尺寸：使用游戏原常规Draw，完全不走裁取入口。 */
        set_stage("焦点框：原常规动态精灵");
        ((SpriteDraw)backend.sprite_draw)(frame,surface,r->left,r->top,mode,shade,0);return true;
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
static bool valid_rectangle(const RuntimeFocusRect *r)
{
    /* 用64位做减法，模块传入坏尺寸也不能溢出为可用矩形。 */
    int64_t width=(int64_t)r->right-r->left,height=(int64_t)r->bottom-r->top;
    return width>=4 && height>=4 && width<=128 && height<=128 && r->left>=-65536 && r->left<=65536 &&
           r->top>=-65536 && r->top<=65536;
}
static void focus_render(RuntimeEventId event,void *subject,unsigned long surface,unsigned long value,void *user)
{
    (void)subject;(void)value;(void)user;drawn_owner=RUNTIME_MODULE_NONE;
    if (event!=RUNTIME_EVENT_UI_DRAW_END || !ready || !surface) return;
    RuntimeFocusRequest selected={0};RuntimeModuleId owner=RUNTIME_MODULE_NONE;
    for(unsigned i=1;i<RUNTIME_MODULE_COUNT;++i) if(providers[i].callback) {
        RuntimeFocusRequest candidate={0};
        /* 提供者每帧重新验证当前页面，Runtime不持有CJm/Slot等业务对象。 */
        RuntimeFocusProvider callback=providers[i].callback;void *data=providers[i].user;
        if(callback(&candidate,data) && providers[i].callback==callback && providers[i].user==data &&
           valid_rectangle(&candidate.rectangle) &&
           (owner==RUNTIME_MODULE_NONE || candidate.priority>selected.priority)) {selected=candidate;owner=(RuntimeModuleId)i;}
    }
    if(owner==RUNTIME_MODULE_NONE)return;
    RuntimeFocusRect rectangle=selected.rectangle;
    void *hud=focus_readptr((void *)backend.hud_global,0);unsigned count=focus_read32(hud,0x44);
    BYTE *sprites=focus_readptr(hud,0x48);
    /* 资源缺少原框时拒绝，不把整张HUD背景精灵当成选择框。 */
    if (focus_read32(hud,0)!=backend.hud_vtable || !focus_read32(hud,0x64) || count<=5 || count>64 || !focus_readable(sprites,count*32u)) return;
    void *frame=sprites+5*32u;unsigned frames=focus_read32(frame,8),index=focus_read32(frame,4);
    void *data=focus_readptr(frame,0);
    /* 一帧记录22字节。先检查动画表和当前下标，再调用原Frame getter，不让空精灵进原函数。 */
    if (!frames || frames>4096 || index>=frames || !focus_readable(data,frames*22u)) return;
    set_stage("焦点框：原动画帧读取");
    void *description=(void *)(uintptr_t)((This0)backend.frame_get)(frame);
    if (!focus_readable(description,22)) return;
    /* 图像getter会解引用帧里的资源管理器和登记表，不能只验证帧表本身。
     * 尚未登记的资源只拒绝绘制，不让原加载分支对空登记对象继续读取。 */
    void *bank=focus_readptr(description,8),*entries=focus_readptr(bank,4);
    unsigned resource=focus_read32(description,4)&0xFFFFFu;
    if (!focus_readable(bank,8) || !focus_readable((BYTE *)entries+resource*4u,4) ||
        !focus_readable(focus_readptr(entries,resource*4u),0x28)) return;
    set_stage("焦点框：原图像读取");
    void *image=(void *)(uintptr_t)((This0)backend.image_get)(description);
    if (!focus_readable(image,0x14) || !focus_readable((void *)(uintptr_t)surface,0x14)) return;
    int sw=(int)focus_read32(image,0xC),sh=(int)focus_read32(image,0x10);
    int dw=(int)focus_read32((void *)(uintptr_t)surface,0xC),dh=(int)focus_read32((void *)(uintptr_t)surface,0x10);
    if (sw<5 || sh<5 || sw>512 || sh>512 || dw<=0 || dh<=0 || dw>65536 || dh>65536) return;
    static unsigned logged_kind;
    unsigned kind=1u<<(unsigned)owner;
    if(!(logged_kind&kind)) {
        logged_kind|=kind;
        RuntimeLog_Write("[焦点绘制] %s 原图=%d,%d 目标=%ld,%ld；同尺寸原Draw，异尺寸原图边缘拼接，禁止越界裁取。",
            "共享焦点",sw,sh,(long)(rectangle.right-rectangle.left),(long)(rectangle.bottom-rectangle.top));
    }
    /* 原SpriteDraw和RectDraw都把帧的E/10位置减12/14原点后加到目标坐标。
     * 提供者传的是最终可见矩形，先减掉这个偏移，动画换帧也每帧重读。
     * 只补偿绘制位置，不改变原帧数据、源图范围或菜单命中坐标。 */
    int16_t origin[4];
    if(!RuntimeWin32_Read((unsigned long)(uintptr_t)description+0xE,origin,sizeof origin))return;
    int dx=(int)origin[0]-origin[2],dy=(int)origin[1]-origin[3];
    rectangle.left-=dx;rectangle.right-=dx;rectangle.top-=dy;rectangle.bottom-=dy;
    if(border(frame,(void *)(uintptr_t)surface,&rectangle,sw,sh,(int)focus_read32(hud,0x58),(int)focus_read32(hud,0x5C)))drawn_owner=owner;
    set_stage(NULL);
}
static void draw(RuntimeEventId event,void *subject,unsigned long surface,unsigned long value,void *user)
{
    focus_render(event,subject,surface,value,user);stage=NULL;
}
int RuntimeFocus_Initialize(const RuntimeContext *runtime)
{
    if(ready)return 1;
    if(!runtime || !runtime->profile || runtime->profile->game_id<1 || runtime->profile->game_id>2)return 0;
    FocusBackend candidate=focus_profiles[runtime->profile->game_id-1];
    uintptr_t addresses[4]={candidate.sprite_draw,candidate.rect_draw,candidate.frame_get,candidate.image_get};
    for(unsigned i=0;i<4;++i) {
        BYTE actual[12];
        if(!RuntimeWin32_Read((unsigned long)addresses[i],actual,12) || memcmp(actual,candidate.signatures[i],12))return 0;
    }
    if(!subscribed) {
        if(!Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_END,draw,NULL))return 0;
        subscribed=true;
    }
    backend=candidate;ready=true;return 1;
}
int RuntimeFocus_Register(RuntimeModuleId owner,RuntimeFocusProvider callback,void *user)
{
    if((unsigned)owner==RUNTIME_MODULE_NONE || (unsigned)owner>=RUNTIME_MODULE_COUNT || !callback)return 0;
    if(providers[owner].callback && (providers[owner].callback!=callback || providers[owner].user!=user))return 0;
    providers[owner].callback=callback;providers[owner].user=user;return 1;
}
void RuntimeFocus_Unregister(RuntimeModuleId owner)
{
    if((unsigned)owner==RUNTIME_MODULE_NONE || (unsigned)owner>=RUNTIME_MODULE_COUNT)return;
    memset(&providers[owner],0,sizeof providers[owner]);if(drawn_owner==owner)drawn_owner=RUNTIME_MODULE_NONE;
}
int RuntimeFocus_WasDrawn(RuntimeModuleId owner)
{return ready && owner!=RUNTIME_MODULE_NONE && drawn_owner==owner;}
