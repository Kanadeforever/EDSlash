#include "ControllerText.h"
#include "Inspect.h"
#include "Combat.h"
#include "../../Runtime/Perf.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static uint32_t focus,focus_kind,owned_hover,actor,zone_request,requested_at;
/* 原请求尚在走近/消费时，旧摇杆方向不能再提交远移动覆盖它；新方向可取消。 */
static struct {uint32_t target,kind,actor,at;void *world;float x,y;} interaction;
static void *focus_world,*hover_manager,*scene_world,*scene_map;
static bool installed;
static BYTE saved_hover[5];
static uint32_t cached_actor,cached_facing,cached_directions;
static float cached_x,cached_y;
static unsigned dynamic_count,static_count;
static int focus_grid_x,focus_grid_y;
static void *world(void) { return ReadPtr((void *)g_profile->world_global,0); }
static void *manager(void) { return ReadPtr(world(),0x30); }
static bool pad_world(void)
{
    return g_input.connected && g_input.focused && g_intent.layer==LAYER_GAME;
}
void Inspect_Reset(void)
{
    memset(&interaction,0,sizeof interaction);
    /* 不撤掉别的来源刚写入的悬停，也不把旧地图的管理器当成新地图使用。 */
    if (owned_hover && focus_world==world() && hover_manager==manager() &&
        Memory_Readable(hover_manager,0x10) && Read32(hover_manager,4)==owned_hover)
        ((This1)g_profile->hover_set)(hover_manager, NULL,0);
    focus=focus_kind=owned_hover=0;focus_world=hover_manager=NULL;
    /* 读图可能复用相同世界/地图地址；进入未就绪阶段也标记新场景，
     * 不能只靠指针变化，否则持续推杆会在新出口立即返回。 */
    if (g_profile && zone_request && (!Read32(world(),0x58) || !Game_Player())) {
        scene_world=scene_map=NULL;actor=0;
    }
    /* 出口跨图的释放门另保留整数/身份记录；不保存旧对象裸指针。 */
}
static void facing(void *role,float *x,float *y)
{
    if (g_intent.lx!=0 || g_intent.ly!=0) {
        Control_WorldDirection(g_intent.lx,g_intent.ly,x,y);return;
    }
    unsigned directions=Read32(role,0x2BF)==16 ? 16:8,id=Read32(role,0x14),heading=Read32(role,0x14B);
    if (cached_actor!=id || cached_facing!=heading || cached_directions!=directions) {
        WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
        cached_x=0;cached_y=1;
        /* 和既有战斗/防御方案相同，反查原方向接口，不能假定角色8/16向的编号顺序。 */
        for (unsigned i=0;i<directions;++i) {
            float angle=(float)i*6.28318530718f/directions;
            WorldPoint point={origin.x+(int)lroundf(cosf(angle)*256),origin.y+(int)lroundf(sinf(angle)*256)};
            if ((unsigned)((This2)g_profile->facing_direction)(role, NULL,(int)(uintptr_t)&point,(int)(uintptr_t)&origin)==heading) {
                cached_x=cosf(angle);cached_y=sinf(angle);break;
            }
        }
        cached_actor=id;cached_facing=heading;cached_directions=directions;
    }
    *x=cached_x;*y=cached_y;
}
static double proximity_score(double dx,double dy,float fx,float fy,uint32_t handle,uint32_t old_focus)
{
    /* 360度不以角度排除目标。前方加权只影响多个近身目标的排序，半径仍按真实距离。
     * 原焦点有小幅保留优势，减少摇杆微调/角色轻微位移时来回跳选。 */
    double distance=dx*dx+dy*dy;
    double score=distance*(dx*fx+dy*fy>=0 ? 0.80:1.0);
    return handle==old_focus ? score*0.92:score;
}
static bool static_valid(void *object)
{
    unsigned type=Read32(object,0x67),subtype=Read32(object,0x54)&0xFFFFu,state=Read32(object,0xBC);
    /* 按完整原返回链区分可破坏物、机关、特殊脚本；掉落另走E句柄和21。
     * 不把所有静态类型都当成同一种机关，也不能把原允许态误当排除态。 */
    if (!Memory_Readable(object,0xC0)) return false;
    /* 变换区图标的96不是普通调查对象：原谓词检查A3状态和TransGo/go脚本。
     * 只有原谓词通过的C/D类才允许走20，不能放开全部96或套普通BC状态门。 */
    if (subtype==0x96) return (type==0x0C || type==0x0D) && g_profile->inspect_portal_gate &&
        ((This0)g_profile->inspect_portal_gate)(object, NULL)!=0;
    /* 完整原选择器在helper非零时返回对象，不能把它误读成排除门。
     * 0/2是原允许态，1等其它态拒绝；可破坏80..86也是原左键可操作对象。 */
    bool active=g_profile->inspect_static_gate ? ((This0)g_profile->inspect_static_gate)(object, NULL)!=0:state==0 || state==2;
    return type>=0x0A && type<=0x14 && subtype>=0x80 && subtype<=0x100 && active;
}
typedef struct {int min_x,min_y;unsigned width,height;BYTE *cells;bool ready;} MapView;
static MapView map_view(void *map)
{
    MapView view={0};uint32_t header[6];
    /* 在同一输入调用中只读一次地图头；不跨帧缓存地图/角色指针。 */
    if (!Memory_Readable(map,sizeof header)) return view;
    memcpy(header,map,sizeof header);view.min_x=(int)header[0];view.min_y=(int)header[1];
    view.width=header[2];view.height=header[3];view.cells=(BYTE *)(uintptr_t)header[5];
    view.ready=view.cells && view.width && view.width<=65536 && view.height && view.height<=65536;
    return view;
}
static BYTE *cell(const MapView *view,int x,int y)
{
    if (!view->ready || x<0 || y<0 || x<view->min_x || y<view->min_y ||
        (int64_t)x>=(int64_t)view->min_x+view->width || (int64_t)y>=(int64_t)view->min_y+view->height) return NULL;
    uint64_t index=(uint64_t)(unsigned)y*view->width+(unsigned)x;
    if (index>0xFFFFFFFFu/19u) return NULL;
    BYTE *record=view->cells+(size_t)index*19u;
    return Memory_Readable(record,19) ? record:NULL;
}
static void submit(unsigned opcode,uint32_t handle)
{
    if (Memory_Readable(manager(),0x10)) ((This4)g_profile->submit)(manager(), NULL,(int)opcode,(opcode==20 || opcode==1) ? focus_grid_x:(int)handle,
            (opcode==20 || opcode==1) ? focus_grid_y:0,opcode==20 ? (int)handle:0);
}
void Inspect_Project(void)
{
    /* 原WorldMouseMove在resolver之后会再次生产WorldHover，必须在该事件处接回。
     * 自己没有调查焦点时只允许已有战斗目标，绝不显示静止鼠标指向的远处NPC/物件。 */
    if (!pad_world() || !Game_Player() || !Memory_Readable(manager(),0x10)) return;
    uint32_t handle=0;
    if (focus_world==world() && Game_Resolve(focus)) handle=focus;
    else {
        uint32_t target=Combat_Target();
        if (Game_Enemy(Game_Player(),Game_Resolve(target))) handle=target;
    }
    ((This1)g_profile->hover_set)(manager(), NULL,(int)handle);
    owned_hover=handle;hover_manager=manager();focus_world=world();
}
static bool inspect_update(void *role)
{
    if (!pad_world()) { Inspect_Reset();return false; }
    void *current_world=world(),*map=ReadPtr((void *)g_profile->inspect_map_global,0);uint32_t current_actor=Read32(role,0x14);
    bool moving=g_intent.lx!=0 || g_intent.ly!=0;
    if (scene_world!=current_world || actor!=current_actor || scene_map!=map) {

        zone_request=0;Inspect_Reset();scene_world=current_world;actor=current_actor;scene_map=map;
    }

    uint32_t old_focus=focus;focus=focus_kind=0;dynamic_count=static_count=0;
    float fx,fy;facing(role,&fx,&fy);
    WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)},grid;
    typedef WorldPoint *(__cdecl *Grid)(WorldPoint *,const WorldPoint *);
    ((Grid)g_profile->world_to_grid)(&grid,&origin);focus_grid_x=grid.x;focus_grid_y=grid.y;
    /* 候选探查真正按统一最大距离裁剪，而非把方形扫描边界当成圆半径。
     * 扫描格数随半径扩大，最多17×17；参数读快照，不在角色帧读取文件。 */
    int radius=RuntimeConfig_GetInt(CONFIG_INSPECT_DISTANCE);
    if (radius<16 || radius>480) radius=160;
    double maximum=(double)radius*radius,best=maximum;
    int scan_radius=(radius+63)/64;
    if (scan_radius>8) scan_radius=8;
    unsigned role_state=Read32(role,0x73);
    /* 松LT但仍在闪避/受击动作中时不自动提交区域事件，避免打断已认可的动作流程。 */
    bool interacting=(role_state==1 || role_state==0x0B) &&
        (g_intent.held & (KEY(PAD_X)|KEY(PAD_Y)))==0 && !Read32(role,g_profile->active_offset);
    if (interacting) {
        void *entities=ReadPtr((void *)g_profile->entities_global,0),*candidate=ReadPtr(entities,0x1C);
        for (unsigned n=0;candidate && n<4096;++n) {
            if (!Memory_Readable(candidate,0x6B)) break;
            void *next=ReadPtr(candidate,8);unsigned type=Read32(candidate,0x67);
            if (candidate!=role && type>=0x1E && type<=0x64 &&
                Memory_Readable(candidate,g_profile->invalid_offset+4) && !Read32(candidate,g_profile->invalid_offset) &&
                Read32(candidate,g_profile->interact_offset) && Read32(candidate,g_profile->inspect_ready_offset)) {
                WorldPoint point={(int)Read32(candidate,0x2C),(int)Read32(candidate,0x30)},target_grid;
                ((Grid)g_profile->world_to_grid)(&target_grid,&point);++dynamic_count;
                double dx=(double)point.x-origin.x,dy=(double)point.y-origin.y,distance=dx*dx+dy*dy;
                if (abs(target_grid.x-grid.x)<=2 && abs(target_grid.y-grid.y)<=2 &&
                    distance<=maximum && proximity_score(dx,dy,fx,fy,Read32(candidate,0x14),old_focus)<=best &&
                    Game_Resolve(Read32(candidate,0x14))==candidate) {
                    focus=Read32(candidate,0x14);best=proximity_score(dx,dy,fx,fy,focus,old_focus);focus_kind=19;
                }
            }
            if (next==candidate) break;
            candidate=next;
        }
    }

    /* 静态物件不在角色链。只扫描按最大距离对应的局部格范围取真实记录，不遍历全地图/句柄池。 */
    MapView view=map_view(map);
    /* 资格函数尤其TransGo会解析脚本文本。同一对象跨多个格时只判断一次，
     * 但仍比较各格的距离，避免大物件近侧焦点丢失。缓存仅在本次扫描的栈上。 */
    uint32_t handles[289];bool eligible[289];unsigned subtypes[289],cached=0;
    if (interacting) for (int y=grid.y-scan_radius;y<=grid.y+scan_radius;++y) for (int x=grid.x-scan_radius;x<=grid.x+scan_radius;++x) {
        BYTE *record=cell(&view,x,y);
        if (!record || !interacting) continue;
        double dx=(double)x*64+32-origin.x,dy=(double)y*64+32-origin.y,distance=dx*dx+dy*dy;
        if (distance>maximum) continue;
        uint32_t item_handle=Read32(record,0x0E)&0xFFFFu;
        void *item=item_handle==0xFFFFu ? NULL:Game_Resolve(item_handle);
        if (interacting && Read32(item,0x67)==0x17) {
            double score=proximity_score(dx,dy,fx,fy,item_handle,old_focus);
            if (score<=best) {
                best=score;focus=item_handle;focus_kind=21;focus_grid_x=x;focus_grid_y=y;
            }
        }
        if ((record[0x10]&0x88)!=0x88) continue;
        uint32_t handle=Read32(record,0x0C)&0xFFFFu;
        if (handle==0xFFFFu || !handle) continue;
        unsigned index=0;
        for (;index<cached && handles[index]!=handle;++index) {}
        if (index==cached) {
            void *object=Game_Resolve(handle);
            handles[index]=handle;eligible[index]=static_valid(object);subtypes[index]=Read32(object,0x54)&0xFFFFu;
            ++cached;
        }
        if (!eligible[index]) continue;
        ++static_count;unsigned subtype=subtypes[index];
        /* 使用物件占用的格而非猜对象Renderer偏移，大物件靠近的一侧也可以聚焦。 */
        double score=proximity_score(dx,dy,fx,fy,handle,old_focus);
        if (interacting && score<=best) {
            best=score;focus=handle;focus_kind=subtype==0x96 ? 20:subtype==0x89 ? 24:subtype==0x87 || subtype==0x88 ? 23:subtype<=0x86 ? 100:1;
            focus_grid_x=x;focus_grid_y=y;
        }
    }
    focus_world=current_world;
    if (old_focus!=focus) Log_Write(ControllerText_Inspect_FocusTargetLog,
        (unsigned long)focus,(unsigned long)focus_kind,dynamic_count,static_count);
    Inspect_Project();
    if (zone_request && (zone_request!=focus || focus_kind!=20)) zone_request=0;
    /* 只保留A手动提交的请求维护；走进区域本身不得生成任何换图事件。 */
    /* 不让同帧/随后尚在处理的原事件被前探移动立即覆盖；转向离开则可正常走开。
     * 保护仍在原pending中的请求，新方向可取消；不在同一区域反复发请求。 */
    if (interaction.target && interaction.world==current_world && interaction.actor==current_actor) {
        bool pending=Read32(role,g_profile->pending_offset)==interaction.kind &&
            Read32(role,g_profile->pending_offset+(interaction.kind==20 ? 12:4))==interaction.target;
        float length=hypotf(interaction.x,interaction.y)*hypotf(g_intent.lx,g_intent.ly);
        bool changed=moving && (length==0 ||
            (interaction.x*g_intent.lx+interaction.y*g_intent.ly)/length<0.95f);
        uint32_t elapsed=g_input.now-interaction.at;
        /* 仅保护仍被原版保留的请求与提交到消费的短窗口；不人为固定等待600ms。
         * 原拒绝/结束、超时或玩家新方向均释放控制。 */
        if (!changed && elapsed<2000u && (pending || elapsed<80u)) return true;
        memset(&interaction,0,sizeof interaction);
    }
    return false;
}
bool Inspect_Update(void *role)
{
    /* 只在整个调查调用外计时，不在每格/每对象读时钟；这是手柄业务中的嵌套分项。 */
    int64_t begin=RuntimePerf_Begin();bool result=inspect_update(role);
    RuntimePerf_End(PERF_INSPECT,begin);return result;
}
static bool portal_near(void)
{
    void *role=Game_Player();if (!role) return false;
    WorldPoint point={(int)Read32(role,0x2C),(int)Read32(role,0x30)},grid;
    typedef WorldPoint *(__cdecl *Grid)(WorldPoint *,const WorldPoint *);
    ((Grid)g_profile->world_to_grid)(&grid,&point);
    void *map=ReadPtr((void *)g_profile->inspect_map_global,0);
    MapView view=map_view(map);
    /* 和原pending20一样，要求玩家邻接格确实属于该目标区域；不把远处聚焦当进入。 */
    for (int y=grid.y-1;y<=grid.y+1;++y) for (int x=grid.x-1;x<=grid.x+1;++x) {
        BYTE *record=cell(&view,x,y);
        if (record && (record[0x10]&0x88)==0x88 && (Read32(record,0x0C)&0xFFFFu)==focus) return true;
    }
    return false;
}
bool Inspect_Activate(void)
{
    void *object=Game_Resolve(focus);
    if (!object || focus_world!=world()) {
        Log_Write(ControllerText_Inspect_NoNearbyTargetLog,
            dynamic_count,static_count,(unsigned long)(uintptr_t)scene_map,focus_grid_x,focus_grid_y);return false;
    }
    bool submitted=false;
    if (focus_kind==19) {
        if (!((This1)g_profile->inspect_gate)(manager(), NULL,(int)focus)) {submit(19,focus);submitted=true;}
    } else if (focus_kind==21 && Read32(object,0x67)==0x17) {submit(21,focus);submitted=true;}
    else if (focus_kind==100 && static_valid(object)) {
        void *role=Game_Player();int group=((This1)g_profile->inspect_basic_get)(role, NULL,1);
        WorldPoint point={focus_grid_x*64+32,focus_grid_y*64+32};
        if (group<0) {Log_Write(ControllerText_Inspect_BaseActionUnavailableLog);return false;}
        Combat_RequestPoint(group,&point);Combat_Update(role,Combat_Target());
        Log_Write(ControllerText_Inspect_StaticActionRequestedLog,group,point.x,point.y);return true;
    } else if (focus_kind==1 && static_valid(object)) {submit(1,focus);return true;}
    else if ((focus_kind==20 || focus_kind==23 || focus_kind==24) && static_valid(object)) {
        if (focus_kind==20 && !portal_near()) {Log_Write(ControllerText_Inspect_ExitRegionNotEnteredLog);return false;}
        submit(focus_kind,focus);
        submitted=true;
        if (focus_kind==20) {zone_request=focus;requested_at=g_input.now;}
    }
    if (submitted) {
        interaction.target=focus;interaction.kind=focus_kind;interaction.actor=Read32(Game_Player(),0x14);
        interaction.world=world();interaction.at=g_input.now;interaction.x=g_intent.lx;interaction.y=g_intent.ly;
    }
    Log_Write(ControllerText_Inspect_NativeEventRequestedLog,(unsigned long)focus_kind,(unsigned long)focus);
    return submitted;
}
static int __fastcall hover_hook(void *mouse,void *unused,int event,int x,void *y)
{
    (void)unused;
    if (g_input.connected && g_input.focused && g_intent.layer!=LAYER_NONE &&
        g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_MOUSE) {
        Inspect_Project();return 0;
    }
    return ((This3)g_profile->inspect_hover)(mouse, NULL,event,x,y);
}
void Inspect_Shutdown(void)
{
    Inspect_Reset();
    if (installed) {
        BYTE replacement[5]={0xE8};int32_t relative=(int32_t)((uintptr_t)hover_hook-g_profile->inspect_hover_call-5);
        memcpy(replacement+1,&relative,4);
        if (Memory_Readable((void *)g_profile->inspect_hover_call,5) && !memcmp((void *)g_profile->inspect_hover_call,replacement,5))
            Memory_Patch((void *)g_profile->inspect_hover_call,saved_hover,5);
    }
    installed=false;zone_request=0;scene_world=scene_map=NULL;actor=0;
}
bool Inspect_Initialize(void)
{
    if (installed) return true;
    uintptr_t target=g_profile->inspect_hover_call;
    if (!Memory_Readable((void *)target,5)) return false;
    memcpy(saved_hover,(void *)target,5);int32_t relative;memcpy(&relative,saved_hover+1,4);
    if (saved_hover[0]!=0xE8 || target+5+relative!=g_profile->inspect_hover) return false;
    if (!HookManager_Claim(SHARED_HOOK_CONTROLLER_INSPECT,RUNTIME_MODULE_CONTROLLER)) return false;
    BYTE replacement[5]={0xE8};relative=(int32_t)((uintptr_t)hover_hook-target-5);memcpy(replacement+1,&relative,4);
    installed=Memory_Patch((void *)target,replacement,5);
    if (installed) Log_Write(ControllerText_Inspect_InterfacesReadyLog);
    return installed;
}
