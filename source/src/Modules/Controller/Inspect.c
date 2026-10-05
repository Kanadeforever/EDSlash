#include "Inspect.h"
#include "Combat.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static uint32_t focus,focus_kind,owned_hover,actor,zone_request,requested_at;
static void *focus_world,*hover_manager,*scene_world,*scene_map;
static bool needs_neutral,installed;
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
    /* 不撤掉别的来源刚写入的悬停，也不把旧地图的管理器当成新地图使用。 */
    if (owned_hover && focus_world==world() && hover_manager==manager() &&
        Memory_Readable(hover_manager,0x10) && Read32(hover_manager,4)==owned_hover)
        ((This1)g_profile->hover_set)(hover_manager,0);
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
            if ((unsigned)((This2)g_profile->facing_direction)(role,(int)(uintptr_t)&point,(int)(uintptr_t)&origin)==heading) {
                cached_x=cosf(angle);cached_y=sinf(angle);break;
            }
        }
        cached_actor=id;cached_facing=heading;cached_directions=directions;
    }
    *x=cached_x;*y=cached_y;
}
static bool cone(double dx,double dy,float fx,float fy)
{
    double distance=dx*dx+dy*dy,dot=dx*fx+dy*fy;
    /* 正面总角度90度。松杆继续使用角色真实朝向，不回退到全圆最近物体。
     * 微小容差只消除float在恰好45度边界上的舍入误差。 */
    return distance<1 || (dot>=0 && dot*dot+0.001>=distance*0.5);
}
static bool static_valid(void *object)
{
    unsigned type=Read32(object,0x67),subtype=Read32(object,0x54)&0xFFFFu,state=Read32(object,0xBC);
    /* 原悬停支持的静态类范围，调查只纳入已核对的87/88/89。
     * 不把可攻击80..86、特殊96、掉落或无效记录当成机关。 */
    if (!Memory_Readable(object,0xC0)) return false;
    /* 变换区图标的96不是普通调查对象：原谓词检查A3状态和TransGo/go脚本。
     * 只有原谓词通过的C/D类才允许走20，不能放开全部96或套普通BC状态门。 */
    if (subtype==0x96) return (type==0x0C || type==0x0D) && g_profile->inspect_portal_gate &&
        ((This0)g_profile->inspect_portal_gate)(object)!=0;
    return type>=0x0A && type<=0x14 && (subtype==0x87 || subtype==0x88 || subtype==0x89) && state!=0 && state!=2;
}
static BYTE *cell(void *map,int x,int y)
{
    if (!Memory_Readable(map,0x18)) return NULL;
    unsigned width=Read32(map,8),height=Read32(map,12);
    if (!width || width>65536 || !height || height>65536 || x<0 || y<0 ||
        x<(int)Read32(map,0) || y<(int)Read32(map,4) ||
        (int64_t)x>=(int64_t)(int)Read32(map,0)+width ||
        (int64_t)y>=(int64_t)(int)Read32(map,4)+height) return NULL;
    uint64_t index=(uint64_t)(unsigned)y*width+(unsigned)x;
    BYTE *cells=ReadPtr(map,0x14);
    if (!cells || index>0xFFFFFFFFu/19u) return NULL;
    BYTE *record=cells+(size_t)index*19u;
    return Memory_Readable(record,19) ? record:NULL;
}
static void submit(unsigned opcode,uint32_t handle)
{
    if (Memory_Readable(manager(),0x10)) ((This4)g_profile->submit)(manager(),(int)opcode,opcode==20 ? focus_grid_x:(int)handle,
            opcode==20 ? focus_grid_y:0,opcode==20 ? (int)handle:0);
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
    ((This1)g_profile->hover_set)(manager(),(int)handle);
    owned_hover=handle;hover_manager=manager();focus_world=world();
}
bool Inspect_Update(void *role)
{
    if (!pad_world()) { Inspect_Reset();return false; }
    void *current_world=world(),*map=ReadPtr(role,0x6F);uint32_t current_actor=Read32(role,0x14);
    bool moving=g_intent.lx!=0 || g_intent.ly!=0;
    if (scene_world!=current_world || actor!=current_actor || scene_map!=map) {
        if (zone_request && moving) needs_neutral=true;
        zone_request=0;Inspect_Reset();scene_world=current_world;actor=current_actor;scene_map=map;
    }
    if (!moving) needs_neutral=false;
    uint32_t old_focus=focus;focus=focus_kind=0;dynamic_count=static_count=0;
    float fx,fy;facing(role,&fx,&fy);
    WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)},grid;
    typedef WorldPoint *(__cdecl *Grid)(WorldPoint *,const WorldPoint *);
    ((Grid)g_profile->world_to_grid)(&grid,&origin);
    double best=1e30;
    unsigned role_state=Read32(role,0x73);
    /* 松LT但仍在闪避/受击动作中时不自动提交区域事件，避免打断已认可的动作流程。 */
    bool interacting=(role_state==1 || role_state==0x0B) &&
        (g_intent.held & (KEY(PAD_X)|KEY(PAD_Y)))==0 && !Read32(role,g_profile->active_offset);
    if (interacting) {
        void *entities=ReadPtr((void *)g_profile->entities_global,0),*candidate=ReadPtr(entities,0x1C);
        for (unsigned n=0;candidate && n<4096;++n) {
            if (!Memory_Readable(candidate,0x6B)) break;
            void *next=ReadPtr(candidate,8);unsigned type=Read32(candidate,0x67);
            if (candidate!=role && type>=0x1E && type<=0x64 && Game_Resolve(Read32(candidate,0x14))==candidate &&
                Memory_Readable(candidate,g_profile->invalid_offset+4) && !Read32(candidate,g_profile->invalid_offset) &&
                Read32(candidate,g_profile->interact_offset) && Read32(candidate,g_profile->inspect_ready_offset)) {
                WorldPoint point={(int)Read32(candidate,0x2C),(int)Read32(candidate,0x30)},target_grid;
                ((Grid)g_profile->world_to_grid)(&target_grid,&point);++dynamic_count;
                double dx=(double)point.x-origin.x,dy=(double)point.y-origin.y,distance=dx*dx+dy*dy;
                if (abs(target_grid.x-grid.x)<=2 && abs(target_grid.y-grid.y)<=2 &&
                    cone(dx,dy,fx,fy) && distance<best) {
                    best=distance;focus=Read32(candidate,0x14);focus_kind=19;
                }
            }
            if (next==candidate) break;
            candidate=next;
        }
    }
    uint32_t nearby_zone=0;double zone_distance=1e30;
    /* 静态物件不在角色链。只扫描玩家周围49个格的真实静态记录，不遍历全地图/句柄池。 */
    for (int y=grid.y-3;y<=grid.y+3;++y) for (int x=grid.x-3;x<=grid.x+3;++x) {
        BYTE *record=cell(map,x,y);
        if (!record || (record[0x10]&0x88)!=0x88) continue;
        uint32_t handle=Read32(record,0x0C)&0xFFFFu;
        if (handle==0xFFFFu || !handle) continue;
        void *object=Game_Resolve(handle);
        if (!static_valid(object)) continue;
        ++static_count;unsigned subtype=Read32(object,0x54)&0xFFFFu;
        int gx=x-grid.x,gy=y-grid.y;
        double dx=(double)x*64+32-origin.x,dy=(double)y*64+32-origin.y,distance=dx*dx+dy*dy;
        bool adjacent=abs(gx)<=1 && abs(gy)<=1;
        if (subtype==0x96 && adjacent && distance<zone_distance) {nearby_zone=handle;zone_distance=distance;}
        /* 使用物件占用的格而非猜对象Renderer偏移，大物件靠近的一侧也可以聚焦。 */
        if (interacting && (subtype==0x89 || subtype==0x96 || adjacent) && cone(dx,dy,fx,fy) && distance<best) {
            best=distance;focus=handle;focus_kind=subtype==0x96 ? 20:subtype==0x89 ? 24:23;
            focus_grid_x=x;focus_grid_y=y;
        }
    }
    focus_world=current_world;
    if (old_focus!=focus) Log_Write("[调查焦点] 句柄=%08lx 原事件=%lu 动态候选=%u 静态格=%u 正面90度。",
        (unsigned long)focus,(unsigned long)focus_kind,dynamic_count,static_count);
    Inspect_Project();
    if (zone_request && zone_request!=nearby_zone) zone_request=0;
    /* 原96的TransGo/go资格通过后，把近身进入意图交给原20事件：参数为格X/格Y/句柄。
     * 不自己加载地图或改脚本；87/88/89仍需A，不能当成出口自动触发。 */
    if (!needs_neutral && moving && interacting && focus_kind==20 && focus==nearby_zone &&
        focus!=zone_request && !(g_intent.pressed & KEY(PAD_A))) {
        zone_request=focus;requested_at=g_input.now;submit(20,focus);
        Log_Write("[调查进入] 进入原TransGo换区邻域，句柄=%08lx，提交原业务一次。",(unsigned long)focus);
        return true;
    }
    /* 不让同帧/随后尚在处理的原事件被前探移动立即覆盖；转向离开则可正常走开。
     * 原脚本未响应时最多等600ms，不永久锁住角色，不在同一区域反复发请求。 */
    bool pending=Read32(role,g_profile->pending_offset)==20 && Read32(role,g_profile->pending_offset+12)==zone_request;
    return moving && focus==zone_request && zone_request && (pending || g_input.now-requested_at<600u);
}
void Inspect_Activate(void)
{
    void *object=Game_Resolve(focus);
    if (!object || focus_world!=world()) {
        Log_Write("[调查] 正面没有有效目标，动态=%u 静态格=%u。",dynamic_count,static_count);return;
    }
    if (focus_kind==19) {
        if (!((This1)g_profile->inspect_gate)(manager(),(int)focus)) submit(19,focus);
    } else if ((focus_kind==20 || focus_kind==23 || focus_kind==24) && static_valid(object)) {
        submit(focus_kind,focus);
        if (focus_kind==20) {zone_request=focus;requested_at=g_input.now;}
    }
    Log_Write("[调查操作] 原事件=%lu 句柄=%08lx。",(unsigned long)focus_kind,(unsigned long)focus);
}
static int __attribute__((fastcall)) hover_hook(void *mouse,void *unused,int event,int x,void *y)
{
    (void)unused;
    if (g_input.connected && g_input.focused && g_intent.layer!=LAYER_NONE &&
        g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_MOUSE) {
        Inspect_Project();return 0;
    }
    return ((This3)g_profile->inspect_hover)(mouse,event,x,y);
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
    installed=false;zone_request=0;needs_neutral=false;scene_world=scene_map=NULL;actor=0;
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
    if (installed) Log_Write("[调查] 动态交互/静态87、88、89、原TransGo出口及正面90度焦点接通，原WorldHover事件已隔离。");
    return installed;
}
