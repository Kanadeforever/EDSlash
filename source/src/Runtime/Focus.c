#include "RuntimeText.h"
#include "Focus.h"
#include "Win32Bridge.h"
#include "Log.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef unsigned char BYTE;
typedef int (__fastcall *This0)(void *, void *);
typedef int (__fastcall *SpriteDraw)(void *, void *,void *,int,int,int,int,int);
typedef int (__fastcall *RectDraw)(void *, void *,void *,int,int,int,int,int,int,int,int,void *);
typedef struct FocusBackend {
    uintptr_t hud_global,hud_vtable,sprite_draw,rect_draw,frame_get,image_get;
    BYTE signatures[4][12];
} FocusBackend;
#include "FocusData.h"
static FocusBackend backend;
static bool ready,subscribed,begin_subscribed;
static RuntimeModuleId drawn_owner;
static struct {RuntimeFocusProvider callback;void *user;} providers[RUNTIME_MODULE_COUNT];
/* 每个UI帧只询问一次提供者，不能每画一个技能图标就重新遍历整页控件。
 * 缓存仅含本帧矩形/编号，不保存游戏对象；BEGIN及登记变化立即失效。 */
static bool snapshot_valid;
static RuntimeFocusRequest snapshot;
static RuntimeModuleId snapshot_owner;
static const char *stage;
static void set_stage(const char *value){stage=value;}
const char *RuntimeFocus_Stage(void){return stage;}
void RuntimeFocus_DrawIcon(unsigned long surface,int x,int y);
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
    set_stage(RuntimeText_Focus_SourceRectReadStage);
    ((RectDraw)backend.rect_draw)(frame, NULL,surface,x,y,width,height,sx,sy,mode,shade,NULL);
}
static bool border(void *frame,void *surface,const RuntimeFocusRect *r,int sw,int sh,int mode,int shade)
{
    int width=r->right-r->left,height=r->bottom-r->top;
    if (width==sw && height==sh) {
        /* 快捷格通常和原框同尺寸：使用游戏原常规Draw，完全不走裁取入口。 */
        set_stage(RuntimeText_Focus_SpriteReadStage);
        ((SpriteDraw)backend.sprite_draw)(frame, NULL,surface,r->left,r->top,mode,shade,0);return true;
    }
    /* 较大的候选框按九块拼接：四角不变，四边重复原边缘片段，中央透明不画。
     * 这不是拉伸或自制新素材，每段复制的尺寸均在原图内。宽度最大512、高度最大128，并限制拼接次数。 */
    const int b=2;int cw=sw-b*2,ch=sh-b*2;
    if (cw<=0 || ch<=0 || width<b*2 || height<b*2 || width>512 || height>128) return false;
    /* 极窄源图可能需要过多重复段，超过96次则保留指针兜底。 */
    int pieces=4+2*((width-2*b+cw-1)/cw)+2*((height-2*b+ch-1)/ch);
    if(pieces>96)return false;
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
    return width>=4 && height>=4 && width<=512 && height<=128 && r->left>=-65536 && r->left<=65536 &&
           r->top>=-65536 && r->top<=65536;
}
static void focus_render(RuntimeEventId event,void *subject,unsigned long surface,unsigned long value,void *user)
{
    (void)value;
    (void)subject;bool icon_pass=user!=NULL;int *position=user;
    if(event==RUNTIME_EVENT_UI_DRAW_BEGIN){drawn_owner=RUNTIME_MODULE_NONE;snapshot_valid=false;return;}
    if (event!=RUNTIME_EVENT_UI_DRAW_END || !ready || !surface) return;
    RuntimeFocusRequest selected_request={0};RuntimeModuleId owner=RUNTIME_MODULE_NONE;
    if(snapshot_valid){selected_request=snapshot;owner=snapshot_owner;}
    else for(unsigned i=1;i<RUNTIME_MODULE_COUNT;++i) if(providers[i].callback) {
        RuntimeFocusRequest candidate={0};
        /* 提供者每帧重新验证当前页面，Runtime不持有CJm/Slot等业务对象。 */
        RuntimeFocusProvider callback=providers[i].callback;void *data=providers[i].user;
        if(callback(&candidate,data) && providers[i].callback==callback && providers[i].user==data &&
           valid_rectangle(&candidate.rectangle) &&
           (owner==RUNTIME_MODULE_NONE || candidate.priority>selected_request.priority)) {selected_request=candidate;owner=(RuntimeModuleId)i;}
    }
    snapshot=selected_request;snapshot_owner=owner;snapshot_valid=true;
    if(owner==RUNTIME_MODULE_NONE)return;
    /* 先在全部提供者中选最高优先级，再判断阶段；模态不会因绘制阶段不同被低优先级抢走。 */
    if(icon_pass ? !selected_request.icon_phase || !position || selected_request.rectangle.left!=position[0]+1 ||
        selected_request.rectangle.top!=position[1]+1 : selected_request.icon_phase)return;
    RuntimeFocusRect rectangle=selected_request.rectangle;
    void *hud=focus_readptr((void *)backend.hud_global,0);unsigned count=focus_read32(hud,0x44);
    BYTE *sprites=focus_readptr(hud,0x48);
    /* 当前可见页面由提供者验证，标题创建页也可借用已加载但隐藏的HUD框素材。
     * 资源缺少原框时拒绝，不把整张HUD背景精灵当成选择框。 */
    if (focus_read32(hud,0)!=backend.hud_vtable || count<=5 || count>64 || !focus_readable(sprites,count*32u)) return;
    void *frame=sprites+5*32u;unsigned frames=focus_read32(frame,8),index=focus_read32(frame,4);
    void *data=focus_readptr(frame,0);
    /* 一帧记录22字节。先检查动画表和当前下标，再调用原Frame getter，不让空精灵进原函数。 */
    if (!frames || frames>4096 || index>=frames || !focus_readable(data,frames*22u)) return;
    set_stage(RuntimeText_Focus_AnimationFrameReadStage);
    void *description=(void *)(uintptr_t)((This0)backend.frame_get)(frame, NULL);
    if (!focus_readable(description,22)) return;
    /* 图像getter会解引用帧里的资源管理器和登记表，不能只验证帧表本身。
     * 尚未登记的资源只拒绝绘制，不让原加载分支对空登记对象继续读取。 */
    void *bank=focus_readptr(description,8),*entries=focus_readptr(bank,4);
    unsigned resource=focus_read32(description,4)&0xFFFFFu;
    if (!focus_readable(bank,8) || !focus_readable((BYTE *)entries+resource*4u,4) ||
        !focus_readable(focus_readptr(entries,resource*4u),0x28)) return;
    set_stage(RuntimeText_Focus_ImageReadStage);
    void *image=(void *)(uintptr_t)((This0)backend.image_get)(description, NULL);
    if (!focus_readable(image,0x14) || !focus_readable((void *)(uintptr_t)surface,0x14)) return;
    int sw=(int)focus_read32(image,0xC),sh=(int)focus_read32(image,0x10);
    int dw=(int)focus_read32((void *)(uintptr_t)surface,0xC),dh=(int)focus_read32((void *)(uintptr_t)surface,0x10);
    if (sw<5 || sh<5 || sw>512 || sh>512 || dw<=0 || dh<=0 || dw>65536 || dh>65536) return;
    static unsigned logged_kind;
    unsigned kind=1u<<(unsigned)owner;
    if(!(logged_kind&kind)) {
        logged_kind|=kind;
        RuntimeLog_Write(RuntimeText_Focus_DrawGeometryLog,
            RuntimeText_Focus_SharedFrameLabel,sw,sh,(long)(rectangle.right-rectangle.left),(long)(rectangle.bottom-rectangle.top));
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
void RuntimeFocus_DrawIcon(unsigned long surface,int x,int y)
{
    /* 原图标先完成，随后画它自己的框，再由原函数继续画说明。END不重复画此类框。 */
    int position[2]={x,y};focus_render(RUNTIME_EVENT_UI_DRAW_END,(void *)1,surface,0,position);stage=NULL;
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
    /* 两阶段分别记成功，END订阅失败后重试不会重复占用BEGIN订阅槽。 */
    if(!begin_subscribed) {
        if(!Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_BEGIN,draw,NULL))return 0;
        begin_subscribed=true;
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
    providers[owner].callback=callback;providers[owner].user=user;snapshot_valid=false;return 1;
}
void RuntimeFocus_Unregister(RuntimeModuleId owner)
{
    if((unsigned)owner==RUNTIME_MODULE_NONE || (unsigned)owner>=RUNTIME_MODULE_COUNT)return;
    snapshot_valid=false;memset(&providers[owner],0,sizeof providers[owner]);if(drawn_owner==owner)drawn_owner=RUNTIME_MODULE_NONE;
}
int RuntimeFocus_WasDrawn(RuntimeModuleId owner)
{return ready && owner!=RUNTIME_MODULE_NONE && drawn_owner==owner;}
