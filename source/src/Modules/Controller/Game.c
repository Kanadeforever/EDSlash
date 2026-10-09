#include "ControllerText.h"
#include "Plugin.h"
#include "Combat.h"
#include "Guard.h"
#include "Feedback.h"
#include "Menu.h"
#include "Inspect.h"
#include "../../Runtime/SettingsWindow.h"
#include "../../Runtime/Perf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t movement_owner;
static void *owned_world;
static bool guard_owned;
static float facing_x, facing_y = 1.0f;
static unsigned context_reason, context_id;
static uintptr_t context_object;
static unsigned native_frames;
static unsigned scan_count,eligible_count;
static bool move_goal_valid,move_goal_run;
static int move_goal_x,move_goal_y,move_lead;
static DWORD last_move_submit;
/* B的落点预览只保存世界身份、句柄和整数坐标，不把鼠标或角色坐标当输出缓存。 */
static struct {bool active,left_style;void *world;uint32_t actor,started,preview_elapsed,expand_ms;int selector,maximum;float dx,dy;WorldPoint point;} jump;
static void jump_cancel(void)
{
    if(jump.active)Feedback_End();
    memset(&jump,0,sizeof jump);
}

static void *global(uintptr_t address) { return ReadPtr((void *)address, 0); }
static void *world(void) { return global(g_profile->world_global); }
static void *manager(void) { return ReadPtr(world(), 0x30); }
static void *mouse(void) { return global(g_profile->mouse_global); }
static void *resolve(uint32_t handle);
static void *player(void)
{
    /* 控制对象的权威来源是 WorldRoot 的管理器句柄。鼠标解析缓存不适合作为手柄是否可用的门，
       尤其键盘采样发生在本帧鼠标解析之前，读取 +0x38 可能拿到上一帧或空缓存。 */
    return resolve(Read32(manager(),0x0C));
}

static void *resolve(uint32_t handle)
{
    /* 句柄最低 15 位是表下标，后两位是代数。不能长期保存 Role 裸指针。 */
    BYTE *table = global(g_profile->handles_global);
    if (!handle || !table) return NULL;
    BYTE *entry = table + (handle & 0x7FFFu) * 6u;
    if (!Memory_Readable(entry, 6) || entry[1] != ((handle >> 15) & 3)) return NULL;
    void *object = ReadPtr(entry, 2);
    if (!Memory_Readable(object, 0x6B) || Read32(object, 0x14) != handle) return NULL;
    return object;
}

static bool role_valid(void *role)
{
    unsigned type=Read32(role,0x67);
    return Memory_Readable(role, g_profile->invalid_offset + 4) &&
        type >= 0x1E && type <= 0x64 && resolve(Read32(role, 0x14)) == role &&
        Read32(role, g_profile->invalid_offset) == 0;
}

void *Game_Player(void)
{
    void *role = player();
    return role_valid(role) ? role : NULL;
}

static void submit(int opcode, int a, int b, int c)
{
    void *owner = manager();
    if (Memory_Readable(owner, 0x10)) ((This4)g_profile->submit)(owner, opcode, a, b, c);
}

static bool move_cell(void *map,unsigned width,BYTE *cells,unsigned kind,int x,int y)
{
    /* 只询问原地形资格，动态角色占用及最终碰撞仍交给原移动状态处理。
     * 每格19字节；先验证边界和乘法，避免坏地图尺寸导致越界读取。 */
    if (!((This2)g_profile->map_bounds)(map,x,y)) return false;
    if (!width || width>65536 || x<0 || y<0) return false;
    uint64_t index=(uint64_t)(unsigned)y*width+(unsigned)x;
    if (index>UINT32_MAX/19u) return false;
    BYTE *cell=cells+(size_t)index*19u;
    return Memory_Readable(cell,19) && ((This1)g_profile->cell_passable)(cell,(int)kind);
}

static bool move_clip(void *role,int *goal_x,int *goal_y)
{
    void *map=ReadPtr(role,0x6F);
    /* 地图尚未就绪时沿用原请求，由原业务拒绝；不拿不完整数据作碰撞结论。 */
    if (!g_profile->map_bounds || !g_profile->cell_passable || !g_profile->world_to_grid ||
        !Memory_Readable(map,0x18)) return true;
    /* 地图头与移动类型每次采样只读一次，不在逐格循环重复读取/验证头字段。 */
    uint32_t header[6];memcpy(header,map,sizeof header);
    unsigned width=header[2],kind=*((BYTE *)role+0x62);BYTE *cells=(BYTE *)(uintptr_t)header[5];
    if (!width || width>65536 || !cells) return true;
    typedef WorldPoint *(__cdecl *Grid)(WorldPoint *,const WorldPoint *);
    WorldPoint position={(int)Read32(role,0x2C),(int)Read32(role,0x30)},start;
    ((Grid)g_profile->world_to_grid)(&start,&position);
    int dx=*goal_x-start.x,dy=*goal_y-start.y;
    if (abs(dx)>64 || abs(dy)>64) return false;
    int steps=abs(dx)>abs(dy) ? abs(dx):abs(dy),x=start.x,y=start.y;
    /* 远目标若落在墙后，原寻路会绕向另一侧。逐格缩短目标到直线上的最后可通行格，
     * 通畅处保留原远目标和全向精度，不改角色位置，也不绕过原移动/碰撞。 */
    for (int i=1;i<=steps;++i) {
        int nx=start.x+(int)lround((double)dx*i/steps),ny=start.y+(int)lround((double)dy*i/steps);
        bool clear=move_cell(map,width,cells,kind,nx,ny);
        if (clear && nx!=x && ny!=y) clear=move_cell(map,width,cells,kind,nx,y) && move_cell(map,width,cells,kind,x,ny);
        if (!clear) {*goal_x=x;*goal_y=y;return x!=start.x || y!=start.y;}
        x=nx;y=ny;
    }
    return steps!=0;
}

bool Game_Menu(void)
{
    if (!g_profile) return true;
    unsigned reason;
    void *root=Menu_Context(&reason);
    context_reason=reason;context_id=Read32(root,0x28);context_object=(uintptr_t)root;
    /* 页面关闭后仍等手柄回中，避免菜单方向/扳机立刻变成走路和技能。 */
    return root!=NULL || Menu_BlocksGameplay();
}

void Game_Diagnose(void)
{
    static DWORD previous_time;
    static unsigned previous_reason=~0u,previous_id=~0u;
    static bool waiting_world;
    static DWORD waiting_since;
    unsigned reason=context_reason;
    bool world_wait=!world() || !Read32(world(),0x58);
    bool actor_wait=!world_wait && !role_valid(player());
    if (world_wait) reason=3;
    else if (actor_wait) reason=4;
    /* 菜单的物理路由reason=3与原世界未就绪编号重合，计时必须用真实状态布尔值。
     * 此时间包含前端停留，不能当成读档计时，更不能把菜单停留累计为世界等待。 */
    if (world_wait || actor_wait) {
        if (!waiting_world) {waiting_world=true;waiting_since=g_input.now;}
    } else if (waiting_world) {
        /* 只记录实际观察到的世界/角色未就绪时间，不解除原加载门，也不凭时间推测脚本完成。 */
        Log_Write(ControllerText_Game_WorldAndPlayerReadyLog,
            (unsigned long)(g_input.now-waiting_since),reason);
        waiting_world=false;
    }
    bool requested=g_input.buttons || g_input.lx!=0 || g_input.ly!=0;
    if (reason==previous_reason && context_id==previous_id &&
        (!requested || g_input.now-previous_time<1000)) return;
    previous_reason=reason;previous_id=context_id;previous_time=g_input.now;
    Log_Write(ControllerText_Game_InputChainStateLog,
        g_intent.layer,reason,context_id,(unsigned long)context_object,(unsigned long)(uintptr_t)player(),
        (unsigned long)Read32(mouse(),0x38),(unsigned long)Read32(manager(),0x0C),
        (unsigned long)Read32(world(),0x58),native_frames,(unsigned long)g_input.buttons,(unsigned long)g_intent.held,(unsigned long)g_intent.pressed,g_input.lx,g_input.ly,
        (unsigned long)Read32(player(),0x73),(unsigned long)Read32(player(),g_profile->active_offset));
}

void Game_Release(void)
{
    Guard_Reset();Inspect_Reset();jump_cancel();
    if (!g_profile) return;
    /* 只终止本插件启动的移动，而且必须仍是同一世界、同一句柄的移动状态。
       技能追敌、其它地图复用的内存、鼠标自己发起的移动都不能被盲目停掉。 */
    void *role = resolve(movement_owner);
    if (movement_owner && world() == owned_world && role == player() && role_valid(role)) {
        if (Read32(role, 0x73) == 0x0B && !Read32(role, g_profile->active_offset))
            ((This4)g_profile->install_state)(role, 1, 0, 0, 0);
        submit(3, 0, 0, 0);
    }
    if (guard_owned && world() == owned_world && role_valid(player()) && !Input_PhysicalDown(VK_MENU))
        submit(16, 0, 0, 0);
    movement_owner = 0;
    move_goal_valid=false;
    owned_world = NULL; guard_owned = false;
}

static bool enemy(void *me, void *candidate)
{
    if (candidate == me || !role_valid(candidate) || Read32(candidate,0x67)!=0x28 ||
        Read32(candidate, g_profile->interact_offset)) return false;
    int group = (int)Read32(candidate, 0x1A7);
    if (group < 0 || group > 99) return false;
    void *record = ReadPtr(candidate, 0x18B);
    if (!Memory_Readable(record, 4) || ((This2)g_profile->template_value)(record, 0x1F, 1) != 0xFFFF) return false;
    /* 原版关系查询只是组合资格之一，单独 relation!=0 不代表敌人。 */
    typedef int (__cdecl *Relation)(void *, void *);
    return ((Relation)g_profile->relation)(me, candidate) != 0;
}

void *Game_Resolve(uint32_t handle) { return resolve(handle); }
bool Game_Enemy(void *role,void *candidate) { return enemy(role,candidate); }

static void *choose_target_impl(void *me)
{
    scan_count=eligible_count=0;
    void *entities = global(g_profile->entities_global);
    void *candidate = ReadPtr(entities, 0x1C), *best = NULL;
    float limit = 640.0f, best_score = limit * limit;
    float fx, fy;
    Control_WorldDirection(facing_x, facing_y, &fx, &fy);
    bool direction = g_intent.lx != 0 || g_intent.ly != 0;
    /* 遍历数有上限，异常链或循环不能拖死游戏。距离用世界坐标，比较时也使用相同单位。 */
    for (unsigned count = 0; candidate && count < 4096; ++count) {
        if (!Memory_Readable(candidate, g_profile->invalid_offset + 4)) break;
        ++scan_count;
        void *next = ReadPtr(candidate, 8);
        /* 距离/方向无资格的对象不应先调用模板和关系查询；选择结果与原规则相同。 */
        bool nearby=Read32(candidate,0x67)==0x28;
        if (nearby) {
            float dx = (float)(int)Read32(candidate,0x2C) - (int)Read32(me,0x2C);
            float dy = (float)(int)Read32(candidate,0x30) - (int)Read32(me,0x30);
            float distance = dx*dx + dy*dy;
            float dot = dx*fx + dy*fy;
            /* 有明确方向时排除身后目标；无方向按距离选，不把远处 NPC 当成最近的目标。 */
            if ((!direction || dot >= 0) && distance < best_score && enemy(me,candidate)) {
                ++eligible_count;best=candidate;best_score=distance;
            }
        }
        if (next == candidate) break;
        candidate = next;
    }
    return best;
}

/* 一次完整遍历只读两次时钟，不在每个敌人资格判断里增加计时调用。 */
static void *choose_target(void *me)
{
    int64_t perf=RuntimePerf_Begin();void *target=choose_target_impl(me);
    RuntimePerf_End(PERF_TARGET,perf);return target;
}

/* 技能快捷和B落点预览共用选择码读取：只读原绑定，不注入Q等键盘事件。
 * user_binding用于RT的自定义槽；B固定动作取原第一槽，避免MOD改绑RT+A后改变B含义。 */
#include "../../Runtime/DefaultSkills.h"
static bool skill_choice(unsigned slot,bool user_binding,int *selection,bool *left_style)
{
    if(slot>=14 || !selection || !left_style)return false;
    if(user_binding) {
        /* 配置按存档Player的创建角色编号保存；场景Actor同偏移是其它数据，不能混用。 */
        void *archive=g_profile->inventory_get ? (void *)(uintptr_t)((This0)g_profile->inventory_get)((void *)g_profile->inventory_root):NULL;
        unsigned role=Memory_Readable(archive,0x34C) ? Read32(archive,0x348):0;
        ConfigBinding binding=RuntimeConfig_GetBinding(Runtime_GetContext()->profile->game_id,
            role,slot+1);
        /* RT快捷是独立技能请求，不作为左手/右手动作选择。旧hand字段不改变这个路由。 */
        if(binding.custom){*selection=binding.selector;*left_style=false;return binding.selector>=0;}
        return false; /* 未设置的RT位置不取原键盘选择；固定B仍走下面的原动作来源。 */
    }
    int key=RuntimeSkill_DefaultKey(slot+1);if(!key)return false;
    void *hud=global(g_profile->skill_global),*record=ReadPtr(hud,0xC18);
    for(unsigned n=0;record && n<128;++n) {
        if(!Memory_Readable(record,0x20))break;
        if((int)Read32(record,0x18)==key) {
            *selection=(int)Read32(record,0x14);*left_style=!user_binding && Read32(record,0x1C)!=0;return *selection>=0;
        }
        void *next=ReadPtr(record,8);if(next==record)break;record=next;
    }
    return false;
}

static void shortcuts(void)
{
    void *hud = global(g_profile->skill_global);
    if (!Memory_Readable(hud,0xC20)) return;
    static const int six[] = {PAD_A,PAD_B,PAD_X,PAD_Y,PAD_LEFT,PAD_RIGHT};
    if (g_intent.layer == LAYER_MEDICINE) {
        for (int i=0;i<6;++i) if (g_intent.pressed & KEY(six[i])) {
            ((This1)g_profile->quick_use)(hud,i);
            Log_Write(ControllerText_Game_RecoveryItemShortcutLog,i+1);
        }
    } else if (g_intent.layer == LAYER_ITEM) {
        for (int i=0;i<6;++i) if (g_intent.pressed & KEY(six[i])) {
            int slot=(int)Read32(hud,0x208+(unsigned)(i+6)*0xE4);
            if (slot<0 || slot>135) {Log_Write(ControllerText_Game_ThrowSlotInvalidLog,i+1);continue;}
            void *inventory=(void *)(uintptr_t)((This0)g_profile->inventory_get)((void *)g_profile->inventory_root);
            void *item=inventory ? (void *)(uintptr_t)((This1)g_profile->item_at)(inventory,slot):NULL;
            if (!Memory_Readable(item,0x24) || (int)Read32(item,0x1C)<=0) {
                Log_Write(ControllerText_Game_ThrowSlotEmptyLog,i+1);continue;
            }
            /* 按原版右手 getter 生成同一个具体物品选择码，但不切换右手、不调用 Y。
               扣数量、弹道和动作资格由原版执行器决定，不能自己提前删掉物品。 */
            int selection=(int)Read32(item,0x20)+10000;
            Combat_Request(selection,ACTION_THROW,false);
            Log_Write(ControllerText_Game_ThrowActionRequestedLog,i+1,selection);
        }
    } else if (g_intent.layer==LAYER_GUARD || g_intent.layer==LAYER_DUAL) {
        static const int faces[]={PAD_A,PAD_B,PAD_X,PAD_Y};
        bool legacy=RuntimeConfig_GetInt(CONFIG_LEGACY_ULTIMATE)!=0;
        if (g_intent.layer==LAYER_DUAL || legacy) {
            for (unsigned i=0;i<4;++i) if (g_intent.pressed&KEY(faces[i])) Feedback_Ultimate(i);
        }
        /* 双扳机不切连招，不代办RT技能。旧模式固定十字；新模式按配置二选一。 */
        if (g_intent.layer==LAYER_GUARD) {
            static const int directions[]={PAD_UP,PAD_RIGHT,PAD_DOWN,PAD_LEFT};
            static const int combo_faces[]={PAD_Y,PAD_B,PAD_A,PAD_X};
            const int *keys=legacy || RuntimeConfig_GetInt(CONFIG_COMBO_SWITCH)==1 ? directions:combo_faces;
            for (unsigned i=0;i<4;++i) if (g_intent.pressed&KEY(keys[i])) Combat_SelectCombo(i);
        }
    }
    if (g_intent.layer != LAYER_SKILL) return;
    static const int buttons[] = {PAD_A,PAD_B,PAD_X,PAD_Y,PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT,PAD_LB,PAD_RB,PAD_BACK,PAD_START,PAD_L3,PAD_R3};
    for (unsigned i=0;i<14;++i) if (g_intent.pressed & KEY(buttons[i])) {
        int selection;bool left_style;
        if(skill_choice(i,true,&selection,&left_style)) {
            Combat_Request(selection,ACTION_SKILL,left_style);
            Log_Write(ControllerText_Game_SkillSlotRequestedLog,i+1,selection);
        } else Log_Write(ControllerText_Game_SkillSlotUnboundLog,i+1);
    }
    /* 单LT按配置用面键或十字切套，RT快捷及右杆不改变长期连招选择。 */

}

/* B是原第一技能快捷的落点前端。解析/资格/缓冲/动作/图标全部复用快捷技能，
 * 不读取玩家init记录，不另设Method选择或跳跃执行分支。 */
static bool jump_update(void *role)
{
    if(g_intent.layer!=LAYER_GAME){jump_cancel();return false;}
    if(jump.active && (jump.world!=world() || jump.actor!=Read32(role,0x14)))jump_cancel();
    if(!jump.active && (g_intent.pressed&KEY(PAD_B))) {
        if(Read32(role,g_profile->active_offset)) {Log_Write(ControllerText_Aim_ActiveActionRejectedLog);return true;}
        int selector;bool left_style;
        if(!skill_choice(0,false,&selector,&left_style)) {
            Log_Write(ControllerText_Aim_FirstNativeSkillUnavailableLog);return true;
        }
        WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
        ResolvedSkill resolved;
        /* B只提供落点预览，动作选择仍交给已经用于RT快捷施放的同一解析及资格链。 */
        if(selector==0xFFFF || !Combat_PreviewSkill(role,selector,&origin,&resolved)) {
            Log_Write(ControllerText_Aim_BaseSkillIneligibleLog,selector);return true;
        }
        int method=resolved.method;
        void *record=(void *)(uintptr_t)((This1)g_profile->lookup)((void *)g_profile->methods,method);
        if(!Memory_Readable(record,0x32)) {Log_Write(ControllerText_Aim_MethodUnreadableLog,method);return true;}
        /* 原坐标Runtime使用距离档×64，裸距离0也可能对应有效的缓存档。
         * getter已在四原EXE核对，不以随意固定距离绕过原游戏的上限。 */
        int tier=((This1)g_profile->method_range)(role,method);
        if(tier<=0 || tier>1024) {Log_Write(ControllerText_Aim_InvalidJumpDistanceLog,selector,method,tier);return true;}
        int maximum=tier*64;
        /* 清理此前本插件的走路请求后才建立预览，不能边走边改变起点。 */
        Game_Release();Combat_Suspend();
        /* B是明确的新操作，也应停止刚从物理来源接管的普通走路；不取消活动技能。 */
        if(Read32(role,0x73)==0x0B)((This4)g_profile->install_state)(role,1,0,0,0);
        jump.active=true;jump.world=world();jump.actor=Read32(role,0x14);
        jump.started=g_input.now;jump.expand_ms=(uint32_t)RuntimeConfig_GetInt(CONFIG_AIM_EXPAND_MS);jump.selector=selector;jump.left_style=left_style;jump.maximum=maximum;
        jump.dx=0;jump.dy=1;
        unsigned count=Read32(role,0x2BF)==16 ? 16:8;
        for(unsigned i=0;i<count;++i) {
            float angle=(float)i*6.28318530718f/count;
            WorldPoint probe={origin.x+(int)lroundf(cosf(angle)*256),origin.y+(int)lroundf(sinf(angle)*256)};
            if((unsigned)((This2)g_profile->facing_direction)(role,(int)(uintptr_t)&probe,(int)(uintptr_t)&origin)==Read32(role,0x14B)) {
                jump.dx=cosf(angle);jump.dy=sinf(angle);break;
            }
        }
        Feedback_HoldSkill(selector);
        Log_Write(ControllerText_Aim_JumpPreviewStartedLog,selector,method,tier,maximum,(unsigned long)jump.expand_ms);
    }
    if(!jump.active)return false;
    /* 受击/原活动动作接管时取消预览，不能在硬直结束后自动补一个旧跳跃。 */
    if(Read32(role,g_profile->active_offset)){jump_cancel();return true;}
    /* 松键消费上次预览的缓存点，不能在这一帧根据新杆方向或新时钟重算落点。 */
    if(!(g_intent.held&KEY(PAD_B))) {
        int selector=jump.selector;bool left_style=jump.left_style;WorldPoint point=jump.point;
        unsigned elapsed=jump.preview_elapsed;jump_cancel();
        Combat_RequestSkillPoint(selector,left_style,&point);Combat_Update(role,0);
        Log_Write(ControllerText_Aim_LandingRequestedLog,selector,point.x,point.y,elapsed);
        return true;
    }
    if(g_intent.lx || g_intent.ly)Control_WorldDirection(g_intent.lx,g_intent.ly,&jump.dx,&jump.dy);
    unsigned elapsed=g_input.now-jump.started;if(elapsed>jump.expand_ms)elapsed=jump.expand_ms;jump.preview_elapsed=elapsed;
    /* 按本次开始时保存的扩散耗时匀速到原上限，途中配置变化不改已有预览。 */
    int minimum=jump.maximum<32 ? jump.maximum:32;
    int distance=minimum+(int)((int64_t)(jump.maximum-minimum)*elapsed/jump.expand_ms);
    int64_t goal_x=(int)Read32(role,0x2C)+(int64_t)lroundf(jump.dx*distance);
    int64_t goal_y=(int)Read32(role,0x30)+(int64_t)lroundf(jump.dy*distance);
    if(goal_x<INT32_MIN || goal_x>INT32_MAX || goal_y<INT32_MIN || goal_y>INT32_MAX){jump_cancel();return true;}
    jump.point=(WorldPoint){(int)goal_x,(int)goal_y};
    return true; /* 预览及释放当帧都不走摇杆移动/普攻/调查。 */
}
bool Game_JumpAnchor(POINT *point)
{
    if(!point || !jump.active || !g_profile || g_intent.layer!=LAYER_GAME ||
        !g_input.connected || !g_input.focused || jump.world!=world() ||
        !Game_Player() || jump.actor!=Read32(Game_Player(),0x14))return false;
    if(!g_profile->projection || !Memory_Readable((void *)g_profile->projection_global,0x14))return false;
    /* 使用原投影函数求屏幕0点与两条基向量，再解二维方程；相机/滚屏字段由原函数读取。
     * 不写MouseManager。屏幕指示与正式交回的世界落点使用同一个point。 */
    typedef int (__attribute__((thiscall)) *Projection)(void *,WorldPoint *,int,int);
    Projection project=(Projection)g_profile->projection;void *camera=(void *)g_profile->projection_global;
    WorldPoint base,xaxis,yaxis;project(camera,&base,0,0);project(camera,&xaxis,1,0);project(camera,&yaxis,0,1);
    double a=xaxis.x-base.x,b=yaxis.x-base.x,c=xaxis.y-base.y,d=yaxis.y-base.y;
    double determinant=a*d-b*c;if(fabs(determinant)<0.01)return false;
    double x=jump.point.x-base.x,y=jump.point.y-base.y;
    double sx=(x*d-y*b)/determinant,sy=(a*y-c*x)/determinant;
    if(sx<-65536 || sx>65536 || sy<-65536 || sy>65536)return false;
    point->x=(LONG)lround(sx);point->y=(LONG)lround(sy);return true;
}

void Game_Update(void)
{
    ++native_frames;
    if(SettingsWindow_Active() || !RuntimeConfig_GetInt(CONFIG_CONTROLLER_ENABLED)){Game_Release();Combat_Suspend();return;}
    if (!Memory_Readable(world(),0x5C) || !Read32(world(),0x58)) { Game_Release();Combat_Reset();return; }
    uint32_t menu_buttons=KEY(PAD_UP)|KEY(PAD_DOWN)|KEY(PAD_LEFT)|KEY(PAD_RIGHT);
    if (g_intent.menu_toggle || (g_intent.layer==LAYER_GAME && (g_intent.pressed & menu_buttons))) {
        /* 菜单热键已注入但原版尚未处理的同一帧，也停止世界动作，避免打开菜单时多走一步。 */
        Game_Release(); return;
    }
    if (!g_profile || !g_input.connected || !g_input.focused || g_intent.layer==LAYER_NONE ||
        g_intent.layer==LAYER_MOUSE || g_intent.layer==LAYER_NATIVE ||
        g_intent.layer==LAYER_MENU || Game_Menu()) { Game_Release();Combat_Suspend();return; }
    void *me=player();
    if (!role_valid(me)) { Game_Release();Combat_Reset();return; }
    if (g_intent.layer==LAYER_ACTION_MENU) {
        /* 菜单拥有ABXY/R3及摇杆，只保持仍按住LT的防御，不运行调查/技能/闪避。 */
        Combat_Suspend();
        if (g_input.lt) {guard_owned=true;Guard_Update(me);} else Game_Release();
        return;
    }
    if (owned_world && owned_world!=world()) Game_Release();
    if (g_intent.lx!=0 || g_intent.ly!=0) { facing_x=g_intent.lx; facing_y=g_intent.ly; }
    owned_world=world();
    if(jump_update(me))return;
    shortcuts();
    bool entering=Inspect_Update(me);
    if (g_intent.layer==LAYER_GAME && (g_intent.pressed & KEY(PAD_A))) {
        /* 只有实际交给原交互才移交走近请求；空按A仍须保留松杆停止的拥有权。 */
        if (Inspect_Activate()) {movement_owner=0;move_goal_valid=false;}
        return;
    }
    if (entering) return;
    void *candidate=choose_target(me);
    Combat_Update(me,candidate ? Read32(candidate,0x14):0);
    if (g_intent.layer==LAYER_GUARD || g_intent.layer==LAYER_DUAL || g_intent.layer==LAYER_ACTION_MENU) {
        guard_owned=true;Guard_Update(me);return;
    }
    bool attacking=g_intent.layer==LAYER_GAME && (g_intent.held & (KEY(PAD_X)|KEY(PAD_Y)))!=0;
    bool direction=g_intent.lx!=0 || g_intent.ly!=0;
    if (attacking) {
        /* 战斗模块维护自己的目标、选择解析和重试；普通手柄不再借用鼠标释放入口。 */
        movement_owner=0;
        move_goal_valid=false;
    } else if (direction && !Read32(me,g_profile->active_offset)) {
        static DWORD last_move_log;
        int x,y;
        if (move_lead!=RuntimeConfig_GetInt(CONFIG_MOVE_LEAD)) {
            move_lead=RuntimeConfig_GetInt(CONFIG_MOVE_LEAD);
            Log_Write(ControllerText_Movement_DeadzoneAndLeadLog,move_lead);
        }
        Control_MoveGoal((int)Read32(me,0x2C),(int)Read32(me,0x30),g_intent.lx,g_intent.ly,move_lead,&x,&y);
        int requested_x=x,requested_y=y;
        bool available=move_clip(me,&x,&y);
        if (!available) {
            if (movement_owner || move_goal_valid) Game_Release();
            if (g_input.now-last_move_log>=1000) {
                Log_Write(ControllerText_Movement_TerrainBlockedLog,
                    (long)(int)Read32(me,0x2C),(long)(int)Read32(me,0x30),requested_x,requested_y);
                last_move_log=g_input.now;
            }
            return;
        }
        bool changed=!move_goal_valid || move_goal_x!=x || move_goal_y!=y || move_goal_run!=g_intent.run;
        bool moving=Read32(me,0x73)==0x0B;
        /* 新方向/新目标立即提交，不增加转向缓冲。相同目标在原版仍移动时不重复塞动作；
           若受阻或尚未开始，只隔 120 ms 重试一次，避免帧率决定请求洪泛。 */
        if (changed || (!moving && g_input.now-last_move_submit>=120)) {
            submit(g_intent.run ? 2:1,x,y,0);
            /* 不改变原版寻路与碰撞。远目标的自动跑标志仍用已存在的原生走跑协议校正。 */
            submit(3,g_intent.run ? 1:0,0,0);
            move_goal_valid=true;move_goal_x=x;move_goal_y=y;move_goal_run=g_intent.run;
            last_move_submit=g_input.now;
            movement_owner=Read32(me,0x73)==0x0B ? Read32(me,0x14) : 0;
        }
        if (g_input.now-last_move_log>=1000) {
            Log_Write(ControllerText_Movement_TargetSubmittedLog,
                (long)(int)Read32(me,0x2C),(long)(int)Read32(me,0x30),requested_x,requested_y,x,y,g_intent.run,
                (unsigned long)Read32(me,0x73));
            last_move_log=g_input.now;
        }
    } else if (!direction && (movement_owner || move_goal_valid)) {
        Game_Release();
    }
}

void Game_Keyboard(BYTE *keys)
{
    if (g_intent.layer==LAYER_ACTION_MENU) return;
    if (g_intent.layer==LAYER_NONE || g_intent.layer==LAYER_MOUSE || g_intent.layer==LAYER_NATIVE) return;
    /* 本批页面已消费A/B/方向/START，不再把同一操作桥成Esc或世界热键。
     * 未实现页面保留原有限键盘导航，A仍不写Enter。 */
    /* 小地图属于世界快捷操作。已进入地图时，格子页捕获仍允许R3原Tab入口；
     * 标题/读档没有玩家，不发送。RT等组合层保留自己的绑定，不额外切小地图。 */
    if ((g_intent.layer==LAYER_GAME || g_intent.layer==LAYER_MENU) &&
        (g_intent.pressed & KEY(PAD_R3)) && Memory_Readable(world(),0x5C) &&
        Read32(world(),0x58) && role_valid(player())) keys[VK_TAB]|=0x80;
    if (Menu_CapturesInput()) return;
    if (g_intent.menu_toggle) keys[VK_ESCAPE]|=0x80;
    if (g_intent.layer==LAYER_GAME) {
        static const int buttons[]={PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT};
        static const int keys_vk[]={'C','V','N','B'};
        for (unsigned i=0;i<4;++i) if (g_intent.pressed & KEY(buttons[i])) keys[keys_vk[i]]|=0x80;
    } else if (g_intent.layer==LAYER_MENU) {
        /* 未接通页面仅桥接原版已有的键盘导航，不推测鼠标确认业务。 */
        /* Enter 在游戏内会打开控制台。没有确认当前页面的原生确认事件前，A 不注入键盘。 */
        if (g_intent.pressed & KEY(PAD_B)) keys[VK_ESCAPE]|=0x80;
        static const int buttons[]={PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT};
        static const int arrows[]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT};
        for (unsigned i=0;i<4;++i) if (g_intent.held & KEY(buttons[i])) keys[arrows[i]]|=0x80;
    }
}

SHORT Game_Async(int key, SHORT native)
{
    /* 普通手柄世界操作不接受真实鼠标按钮再次产生攻击/转向。鼠标模式保留原版。 */
    if (g_input.connected && g_input.focused && g_intent.layer!=LAYER_NONE &&
        g_intent.layer!=LAYER_MOUSE && g_intent.layer!=LAYER_NATIVE &&
        (key==VK_LBUTTON || key==VK_RBUTTON || key==VK_MBUTTON)) return 0;
    /* 手柄防御直接提交原生 ON/OFF，并隔离原版真实 Alt 松开的输入生产点。
       物理输入来源恢复原版键盘，不再用虚拟 Alt 保持手柄防御。 */
    if (g_input.connected && g_input.focused && g_intent.layer!=LAYER_NONE &&
        g_intent.layer!=LAYER_NATIVE && g_intent.layer!=LAYER_MOUSE && key==VK_MENU) return 0;
    if (g_input.connected && g_input.focused && g_intent.layer==LAYER_GAME && g_intent.run && key==VK_SHIFT)
        return (SHORT)(native | 0x8000);
    return native;
}
