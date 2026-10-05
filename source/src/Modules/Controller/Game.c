#include "Plugin.h"
#include "Combat.h"
#include "Guard.h"
#include "Feedback.h"
#include "Menu.h"
#include "Inspect.h"
#include "../../Runtime/Perf.h"
#include <math.h>
#include <stdio.h>
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
    unsigned reason=context_reason;
    if (!world() || !Read32(world(),0x58)) reason=3;
    else if (!role_valid(player())) reason=4;
    bool requested=g_input.buttons || g_input.lx!=0 || g_input.ly!=0;
    if (reason==previous_reason && context_id==previous_id &&
        (!requested || g_input.now-previous_time<1000)) return;
    previous_reason=reason;previous_id=context_id;previous_time=g_input.now;
    Log_Write("[输入链] 层=%d 门=%u 界面=%02X 对象=%08lx 玩家=%08lx 鼠标玩家=%08lx 句柄=%08lx 世界58=%08lx 原生帧=%u 按键=%04lx 摇杆=%.2f,%.2f 状态=%lu 活动动作=%08lx。",
        g_intent.layer,reason,context_id,(unsigned long)context_object,(unsigned long)(uintptr_t)player(),
        (unsigned long)Read32(mouse(),0x38),(unsigned long)Read32(manager(),0x0C),
        (unsigned long)Read32(world(),0x58),native_frames,(unsigned long)g_input.buttons,g_input.lx,g_input.ly,
        (unsigned long)Read32(player(),0x73),(unsigned long)Read32(player(),g_profile->active_offset));
}

void Game_Release(void)
{
    Guard_Reset();Inspect_Reset();
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

static void shortcuts(void)
{
    void *hud = global(g_profile->skill_global);
    if (!Memory_Readable(hud,0xC20)) return;
    static const int six[] = {PAD_A,PAD_B,PAD_X,PAD_Y,PAD_LEFT,PAD_RIGHT};
    if (g_intent.layer == LAYER_MEDICINE) {
        for (int i=0;i<6;++i) if (g_intent.pressed & KEY(six[i])) {
            ((This1)g_profile->quick_use)(hud,i);
            Log_Write("[药品快捷] 原版槽位 %d。",i+1);
        }
    } else if (g_intent.layer == LAYER_ITEM) {
        for (int i=0;i<6;++i) if (g_intent.pressed & KEY(six[i])) {
            int slot=(int)Read32(hud,0x208+(unsigned)(i+6)*0xE4);
            if (slot<0 || slot>135) {Log_Write("[投掷快捷] 槽 %d 没有有效物品。",i+1);continue;}
            void *inventory=(void *)(uintptr_t)((This0)g_profile->inventory_get)((void *)g_profile->inventory_root);
            void *item=inventory ? (void *)(uintptr_t)((This1)g_profile->item_at)(inventory,slot):NULL;
            if (!Memory_Readable(item,0x24) || (int)Read32(item,0x1C)<=0) {
                Log_Write("[投掷快捷] 槽 %d 已空或物品用尽。",i+1);continue;
            }
            /* 按原版右手 getter 生成同一个具体物品选择码，但不切换右手、不调用 Y。
               扣数量、弹道和动作资格由原版执行器决定，不能自己提前删掉物品。 */
            int selection=(int)Read32(item,0x20)+10000;
            Combat_Request(selection,ACTION_THROW,false);
            Log_Write("[投掷快捷] 槽 %d 直接请求选择=%d。",i+1,selection);
        }
    } else if (g_intent.layer==LAYER_GUARD) {
        static const int faces[]={PAD_A,PAD_B,PAD_X,PAD_Y};
        for (unsigned i=0;i<4;++i) if (g_intent.pressed&KEY(faces[i])) Feedback_Ultimate(i);
        static const int directions[]={PAD_UP,PAD_RIGHT,PAD_DOWN,PAD_LEFT};
        for (unsigned i=0;i<4;++i) if (g_intent.pressed&KEY(directions[i])) Combat_SelectCombo(i);
    }
    if (g_intent.layer != LAYER_SKILL) return;
    static const int buttons[] = {PAD_A,PAD_B,PAD_X,PAD_Y,PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT,PAD_LB,PAD_RB,PAD_BACK,PAD_START,PAD_L3,PAD_R3};
    static const int defaults[] = {'Q','W','E','R','T','Y','U','I','O','A','S','D',0,0};
    for (int i=0;i<14;++i) if (g_intent.pressed & KEY(buttons[i])) {
        /* 自定义保存稳定技能selector；未自定义时使用原游戏当前角色绑定，默认操作不增加设置步骤。 */
        ConfigBinding binding=RuntimeConfig_GetBinding(
            Runtime_GetContext()->profile->game_id,Read32(Game_Player(),0x348),(unsigned)i+1);
        if (binding.custom) {
            /* Config的right为右手标志，Combat第三参数则是left_style，必须取反。 */
            Combat_Request(binding.selector,ACTION_SKILL,binding.right==0);continue;
        }
        int vk=defaults[i];
        if (!vk) {Log_Write("[技能] 槽%d尚未配置技能。",i+1);continue;}
        void *record=ReadPtr(hud,0xC18);
        bool found=false;
        for (unsigned n=0;record && n<128;++n) {
            if (!Memory_Readable(record,0x20)) break;
            if ((int)Read32(record,0x18)==vk) {
                Combat_Request((int)Read32(record,0x14),ACTION_SKILL,Read32(record,0x1C)!=0);
                found=true;break;
            }
            record=ReadPtr(record,8);
        }
        Log_Write("[技能] 槽 %d，原版热键 %d，%s。",i+1,vk,found ? "已请求施放" : "角色尚无该绑定");
    }
    /* 四套切换已经归入 LT+十字键，RT 右摇杆不再改写 Y 的套组。 */

}

void Game_Update(void)
{
    ++native_frames;
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
    if (owned_world && owned_world!=world()) Game_Release();
    if (g_intent.lx!=0 || g_intent.ly!=0) { facing_x=g_intent.lx; facing_y=g_intent.ly; }
    owned_world=world();
    shortcuts();
    bool entering=Inspect_Update(me);
    if (g_intent.layer==LAYER_GAME && (g_intent.pressed & KEY(PAD_A))) { Inspect_Activate();return; }
    if (entering) return;
    void *candidate=choose_target(me);
    Combat_Update(me,candidate ? Read32(candidate,0x14):0);
    if (g_intent.layer==LAYER_GUARD) { guard_owned=true;Guard_Update(me);return; }
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
            Log_Write("[移动配置] 左摇杆圆形死区，走跑共用前探=%d 格。",move_lead);
        }
        Control_MoveGoal((int)Read32(me,0x2C),(int)Read32(me,0x30),g_intent.lx,g_intent.ly,move_lead,&x,&y);
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
            Log_Write("[移动] 世界=%ld,%ld 地图目标=%d,%d 走跑=%d 提交后状态=%lu。",
                (long)(int)Read32(me,0x2C),(long)(int)Read32(me,0x30),x,y,g_intent.run,
                (unsigned long)Read32(me,0x73));
            last_move_log=g_input.now;
        }
    } else if (!direction && (movement_owner || move_goal_valid)) {
        Game_Release();
    }
}

void Game_Keyboard(BYTE *keys)
{
    if (g_intent.layer==LAYER_NONE || g_intent.layer==LAYER_MOUSE || g_intent.layer==LAYER_NATIVE) return;
    /* 本批页面已消费A/B/方向/START，不再把同一操作桥成Esc或世界热键。
     * 未实现页面保留原有限键盘导航，A仍不写Enter。 */
    if (Menu_CapturesInput()) return;
    if (g_intent.menu_toggle) keys[VK_ESCAPE]|=0x80;
    if (g_intent.layer==LAYER_GAME) {
        static const int buttons[]={PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT,PAD_R3};
        static const int keys_vk[]={'C','V','N','B',VK_TAB};
        for (unsigned i=0;i<5;++i) if (g_intent.pressed & KEY(buttons[i])) keys[keys_vk[i]]|=0x80;
    } else if (g_intent.layer==LAYER_MENU) {
        /* 首版只桥接原版已经支持的键盘导航；格子与几何 Focus 不在此处伪装成已完成。 */
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
