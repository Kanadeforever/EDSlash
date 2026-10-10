#include "ControllerText.h"
#include "Menu.h"
#include "../../Runtime/SettingsWindow.h"
#include "../QOL/QOLModule.h"
#include <math.h>
#include <string.h>

/* 一页仅收集本批已核对的按钮，不扫描全游戏注册表或保存子控件裸指针。
 * 每次使用之前重新从该页子链取得对象，销毁/隐藏的按钮自然失去资格。 */
typedef struct { void *object; unsigned id; double x,y; } MenuButton;
typedef struct {
    void *root;
    unsigned id;
    int direction;
    uint32_t next_repeat,hold_since;
    bool barrier,owned;
    /* 0=下方可选技能，1=上方已编排连招；区域由业务类型识别，不按屏幕猜测。 */
    unsigned skill_region;
    unsigned grid_slot[7];
    bool grid_buttons;
} MenuState;
static MenuState state;
/* 原角色提交会在打开第二层后继续读取第一层A8，调用完成前不能清空。 */
static void *character_submission;
/* 模态确认暂时接管输入时，保存已核对的物品页及其格号。
 * 返回时必须再次验证原登记对象与显示状态，不能复用已销毁的指针。 */
static MenuState resume_grid;
static bool installed,frame_captured;
enum { MENU_KINDS=19, QUEST_KIND=7, SKILL_KIND=8, BAG_KIND=9, STORAGE_KIND=10,
       SHOP_KIND=11,CRAFT_KIND=12,INLAY_KIND=13,CHARM_KIND=14,QUICK_KIND=15, SETTINGS_KIND=16, CHARACTER_KIND=17, NEWGAME_KIND=18,
       COMBO_NODE_BASE=0x300, GRID_CELL_BASE=0x400, QUICK_CELL_BASE=0x500 };
static struct {void *container;uint32_t item;int origin;} carried;
static uintptr_t tables[MENU_KINDS],original[MENU_KINDS][3];
static const unsigned offsets[3]={4,0x1C,0x30};
static const unsigned grid_ids[7]={0x14,0xB4,0x56,0x3D,0x3E,0x5B,0};
static const unsigned slot_first[7]={0,0,0,0x41,0x49,0x5E,QUICK_CELL_BASE};
static const unsigned slot_count[7]={50,50,50,7,12,5,12};

/* 只有这七个编号拥有库存格号；设置等普通页面不能索引格子数组。 */
static bool grid_kind(int kind) { return kind>=BAG_KIND && kind<=QUICK_KIND; }
static bool visible(void *object)
{
    /* 只读标志，不能调用会顺便写B8 latch的原版active查询。 */
    return Memory_Readable(object,0xC0) && (Read32(object,0x64) || Read32(object,0x68));
}
static int kind_of(void *root)
{
    uintptr_t table=Read32(root,0);
    if (table && table==g_profile->menu_title_vtable && Read32(root,0x28)==0x19) return 0;
    if (table && table==g_profile->menu_system_vtable && Read32(root,0x28)==0x2D) return 1;
    if (table && table==g_profile->menu_confirm_vtable && Read32(root,0x28)==0x98) return 2;
    if (table && table==g_profile->menu_load_vtable && Read32(root,0x28)==0x94) return 3;
    /* 通用确认框把确认结果交回原owner；不仅交易/删档，乾坤袋和镶嵌也复用它。
     * 只接纳明确核对过的owner虚表和编号，未知业务仍不调用其回调。 */
    void *owner=ReadPtr(root,0xCC);
    bool known_owner=!owner;
    uintptr_t owner_tables[9]={g_profile->menu_load_vtable,g_profile->menu_bag_vtable,
        g_profile->menu_storage_vtable,g_profile->menu_shop_vtable,g_profile->menu_craft_vtable,
        g_profile->menu_inlay_vtable,g_profile->menu_charm_vtable,g_profile->menu_character_vtable,g_profile->menu_newgame_vtable};
    unsigned owner_ids[9]={0x94,0x14,0xB4,0x56,0x3D,0x3E,0x5B,0x1A,0x9C};
    for (unsigned i=0;i<9 && owner;++i)
        if (owner_tables[i] && Read32(owner,0)==owner_tables[i] && Read32(owner,0x28)==owner_ids[i]) known_owner=true;
    if (table && table==g_profile->menu_message_vtable && known_owner) return 4;
    if (table && table==g_profile->menu_talk_vtable) return 5;
    if (table && table==g_profile->menu_text_vtable) return 6;
    if (table && table==g_profile->menu_quest_vtable && Read32(root,0x28)==0x63) return QUEST_KIND;
    if (table && table==g_profile->menu_skill_vtable && Read32(root,0x28)==0x7D) return SKILL_KIND;
    if (table && table==g_profile->menu_bag_vtable && Read32(root,0x28)==0x14) return BAG_KIND;
    if (table && table==g_profile->menu_storage_vtable && Read32(root,0x28)==0xB4) return STORAGE_KIND;
    uintptr_t extra[4]={g_profile->menu_shop_vtable,g_profile->menu_craft_vtable,g_profile->menu_inlay_vtable,g_profile->menu_charm_vtable};
    for (unsigned i=0;i<4;++i) if (table && table==extra[i] && Read32(root,0x28)==grid_ids[i+2]) return SHOP_KIND+(int)i;
    if (table && table==g_profile->menu_hud_vtable && root==ReadPtr((void *)g_profile->skill_global,0)) return QUICK_KIND;
    if (table && table==g_profile->menu_settings_vtable && Read32(root,0x28)==0xAA) return SETTINGS_KIND;
    if(table && table==g_profile->menu_character_vtable && Read32(root,0x28)==0x1A)return CHARACTER_KIND;
    if(table && table==g_profile->menu_newgame_vtable && Read32(root,0x28)==0x9C)return NEWGAME_KIND;
    return -1;
}
static void *grid_root(unsigned index)
{
    if (index>=7) return NULL;
    if (index==6) {
        /* HUD本身常驻；仅有物品面板打开才可进入快捷栏，不能在世界里挡住角色输入。 */
        bool panel=false;for (unsigned i=0;i<6;++i) if (grid_root(i)) {panel=true;break;}
        void *hud=ReadPtr((void *)g_profile->skill_global,0);
        return panel && visible(hud) && Read32(hud,0x64) && kind_of(hud)==QUICK_KIND ? hud:NULL;
    }
    void *root=(void *)(uintptr_t)((This1)g_profile->get_jm)((void *)g_profile->ui, NULL,(int)grid_ids[index]);
    if (!visible(root) || !Read32(root,0x64) || kind_of(root)!=BAG_KIND+(int)index) return NULL;
    /* 原base Tick会在依附页关闭时收起辅助面板。采样发生在Tick前，
     * 所以先只读复核同样的两条依附关系，避免过期面板挡住场景A。 */
    for (unsigned offset=0xAC;offset<=0xB0;offset+=4) {
        uint32_t id=Read32(root,offset);
        if (id==UINT32_MAX) continue;
        void *dependency=(void *)(uintptr_t)((This1)g_profile->get_jm)((void *)g_profile->ui, NULL,(int)id);
        if (dependency && !visible(dependency)) return NULL;
    }
    return root;
}
static bool page(void *object,void *hud)
{
    if (ActionMenu_Owns(object)) return false;
    void *resource=ReadPtr(object,0x50);
    int kind=kind_of(object);
    if (grid_kind(kind) && grid_root((unsigned)(kind-BAG_KIND))!=object) return false;
    if (object!=hud && visible(object) && (kind==5 || kind==6 || kind==SETTINGS_KIND || kind==CHARACTER_KIND || kind==NEWGAME_KIND || grid_kind(kind))) return true;
    return object!=hud && visible(object) && Memory_Readable(resource,12) &&
        ((This1)g_profile->ui_property)(resource, NULL,13)==1;
}
void *Menu_Context(unsigned *reason)
{
    *reason=0;
    if (!g_profile) return NULL;
    void *ui=(void *)g_profile->ui,*hud=ReadPtr((void *)g_profile->skill_global,0);
    void *capture=ReadPtr(ui,0x3C);
    if (ActionMenu_Owns(capture)) capture=NULL;
    int capture_kind=kind_of(capture);
    bool stale_grid=grid_kind(capture_kind) && grid_root((unsigned)(capture_kind-BAG_KIND))!=capture;
    if (capture!=hud && visible(capture) && !stale_grid) {
        /* 数字文本等子对象可能是capture，向父级归一化；异常父链最多走8层。
         * 找不到已核对父页时仍返回原capture阻塞世界，不能擅自忽略未知弹窗。 */
        void *root=capture;
        for (unsigned n=0;n<8 && root;++n) {
            if (kind_of(root)>=0) { *reason=1;return root; }
            void *parent=ReadPtr(root,0xA4);
            if (!visible(parent) || parent==root) break;
            root=parent;
        }
        *reason=1;return capture;
    }
    /* 同时打开页面时，模态确认优先于普通顶层焦点。读取自己游标，绝不改CJMMng迭代器。 */
    void *first=NULL,*node=ReadPtr(ui,0x18),*tail=ReadPtr(ui,0x1C);
    bool selected_grid=false;
    for (unsigned n=0;node && node!=ui && n<256;++n) {
        if (!Memory_Readable(node,0xC0)) break;
        if (page(node,hud)) {
            if (kind_of(node)==2 || kind_of(node)==4 || kind_of(node)==6) { *reason=2;return node; }
            if (!first) first=node;
            if (node==state.root && grid_kind(kind_of(node))) selected_grid=true;
        }
        if (node==tail) break;
        void *next=ReadPtr(node,0x0C);
        if (next==node) break;
        node=next;
    }
    /* 已核对的真实当前路由可优先；鼠标指向HUD或隐藏对象不会挤掉活跃菜单。 */
    void *routed=ReadPtr(ui,0x40);
    bool controller=installed && g_input.connected && g_input.focused &&
        g_intent.layer!=LAYER_NONE && g_intent.layer!=LAYER_MOUSE && g_intent.layer!=LAYER_NATIVE;
    if (!controller && page(routed,hud)) { *reason=3;return routed; }
    /* 确认框消失后，优先回到仍登记且显示的原操作页；不得越过模态页。 */
    int resume_kind=kind_of(resume_grid.root);
    if (controller && grid_kind(resume_kind) && grid_root((unsigned)(resume_kind-BAG_KIND))==resume_grid.root)
        {*reason=2;return resume_grid.root;}
    /* 已显示的背包/仓库可独立选焦点，不改原链顺序，也不能盖过上面已返回的模态页。 */
    bool inlay_session=grid_root(INLAY_KIND-BAG_KIND)!=NULL;
    bool permitted_grid=!inlay_session || kind_of(state.root)==BAG_KIND || kind_of(state.root)==INLAY_KIND || kind_of(state.root)==QUICK_KIND;
    if (controller && state.owned && permitted_grid && grid_kind(kind_of(state.root)) &&
        (selected_grid || grid_root((unsigned)(kind_of(state.root)-BAG_KIND))==state.root)) {*reason=2;return state.root;}
    /* 已知物品面板也可能不是普通顶层页，使用原登记对象补充路由，不依赖鼠标根页属性13。 */
    /* 镶嵌同时显示装备说明等辅助窗口；未知的非模态说明页不能抢走已接通的操作区。
     * 真实capture和确认框仍已在前面优先返回，不能用此规则越过它们。 */
    if (!first || (controller && kind_of(first)<0))
        for (unsigned i=0;i<7;++i) {void *root=grid_root(i);if (root) {*reason=2;return root;}}
    if (first) *reason=2;
    return first;
}
/* 学习页类型栏编号按两作原Tick规则分别取当前角色的四行，不借另一作编号。 */
static bool skill_category(unsigned id)
{
    void *player=(void *)(uintptr_t)((This0)g_profile->inventory_get)((void *)g_profile->inventory_root, NULL);
    if(!Memory_Readable(player,0x34C))return false;
    unsigned role=Read32(player,0x348),group;
    if(g_profile->game_id==1)group=role==30 ? 1:role==40 ? 2:0;
    else if(role==4)group=0;else if(role==30)group=1;else if(role==40)group=2;else if(role==0xDF)group=3;else if(role==1)group=4;else return false;
    unsigned first=(g_profile->game_id==1 ? 0xCF:0xE3)+group*4;
    return id>=first && id<first+4;
}
static bool allowed(int kind,unsigned id)
{
    if (kind==0) return (id>=0x1F && id<=0x23) || id==0xA7;
    if (kind==1) return id==0x2E || id==0x30 || id==0x31;
    if (kind==SKILL_KIND) return (id>=0x7E && id<=0x93) || skill_category(id);
    if(kind==CHARACTER_KIND)return id==0x24 || id==0x25 || id==0xA8 || (g_profile->game_id==2 && (id==0xE1 || id==0xE2));
    if(kind==NEWGAME_KIND)return id==0x9F || id==0xA0 || id==0xA1;
    if (kind==SETTINGS_KIND) return id>=0xAB && id<=0xB2;
    if (kind==BAG_KIND) return id==0x15 || id==0x16 || id==0x17 || id==0x5A;
    if (kind==STORAGE_KIND) return id==0xB5 || id==0xB6;
    if (kind==SHOP_KIND) return id>=0x57 && id<=0x59;
    if (kind==CRAFT_KIND) return id==0x3F || id==0x40;
    if (kind==INLAY_KIND) return id==0x48;
    return kind==2 && (id==0x9A || id==0x9B);
}
static unsigned buttons(void *root,int kind,MenuButton *out)
{
    unsigned count=0;
    void *skill_role=kind==SKILL_KIND ? Game_Player():NULL;
    if (kind==QUICK_KIND) {
        /* 原12个嵌入槽以E4为步长，药/投掷分类由原主操作判断，插件不复制类别规则。 */
        if (!Memory_Readable(root,0xC20)) return 0;
        for (unsigned i=0;i<12;++i) {
            BYTE *slot=(BYTE *)root+0x13C+i*0xE4;
            int w=(int)Read32(slot,0x1C),h=(int)Read32(slot,0x20);
            /* HUD自己直接画十二槽，不看内嵌CJm的64标志。用该标志会把真实快捷格全滤掉。 */
            if (w>0 && h>0) out[count++]=(MenuButton){slot,QUICK_CELL_BASE+i,
                (int)Read32(slot,0x14)+w/2.0,(int)Read32(slot,0x18)+h/2.0};
        }
        return count;
    }
    void *node=ReadPtr(root,0x9C);
    for (unsigned n=0;node && n<128;++n) {
        if (!Memory_Readable(node,0xC0)) break;
        unsigned id=Read32(node,0x28);
        if (kind==4 && id==0x2A && Read32(node,0xC4)<=1) id=0x200+Read32(node,0xC4);
        if (kind==5 && id==0x2A) id=Read32(node,0xC4);
        int width=(int)Read32(node,0x1C),height=(int)Read32(node,0x20);
        bool skill_allowed=kind!=SKILL_KIND ||
            (Read32(root,0xC0)==0x7F ? (id>=0x84 && id<=0x93) || skill_category(id):
             Read32(root,0xC0)==0x80 && !state.skill_region && id>=0x81 && id<=0x8F);
        if (kind==SKILL_KIND && id>=0x84 && id<=0x8F) {
            /* 十二个技能占位控件即使没有技能也可能保留显示标志和矩形。
             * 原页以资源字段19读取技能组；连招候选还必须通过当前角色已学资格。 */
            void *resource=ReadPtr(node,0x50);
            int selector=Memory_Readable(resource,12) ? ((This1)g_profile->ui_property)(resource, NULL,19):-1;
            void *group=selector>=0 && selector<65535 ? (void *)(uintptr_t)((This1)g_profile->lookup)((void *)g_profile->skill_groups, NULL,selector):NULL;
            if (!Memory_Readable(group,0x26)) skill_allowed=false;
            else if (Read32(root,0xC0)==0x80) {
                if (!skill_role || ((This1)g_profile->skill_eligibility)(skill_role, NULL,selector)==-1) skill_allowed=false;
            }
        }
        bool grid_allowed=!grid_kind(kind) || state.grid_buttons;
        bool special_slot=grid_kind(kind) && kind>=CRAFT_KIND && id>=slot_first[kind-BAG_KIND] &&
            id<slot_first[kind-BAG_KIND]+slot_count[kind-BAG_KIND] && !state.grid_buttons;
        if (special_slot) grid_allowed=true;
        if (count<64 && skill_allowed && grid_allowed && (kind==5 ? Read32(node,0x28)==0x2A && id!=0xFFFFFFFFu:kind==4 ? id==0x200 || id==0x201:special_slot || allowed(kind,id)) && ReadPtr(node,0xA4)==root &&
            Read32(node,0x64) && width>0 && height>0) {
            out[count++]=(MenuButton){node,id,(int)Read32(node,0x14)+width/2.0,
                                           (int)Read32(node,0x18)+height/2.0};
        }
        void *next=ReadPtr(node,8);
        if (next==node) break;
        node=next;
    }
    if (grid_kind(kind) && kind<=SHOP_KIND && !state.grid_buttons && Memory_Readable(ReadPtr(root,0x50),12)) {
        /* 原网格是10列×5行、每格24像素；资源位置叠加当前页面位置，兼容原宽屏布局。 */
        void *resource=ReadPtr(root,0x50);
        int x=(int)Read32(root,0x14)+((This2)g_profile->template_value)(resource, NULL,5,1);
        int y=(int)Read32(root,0x18)+((This2)g_profile->template_value)(resource, NULL,6,1);
        for (unsigned i=0;i<50;++i) out[count++]=(MenuButton){NULL,GRID_CELL_BASE+i,
            x+(int)(i%10)*24+12.0,y+(int)(i/10)*24+12.0};
    }
    if (kind==SKILL_KIND && state.skill_region && Read32(root,0xC0)==0x80 && Read32(root,0xC4)<4) {
        /* 连招条没有独立GUI子对象。只读原列表，按原绘制的六列44×48间距生成导航点。
         * 虚拟编号只存在插件里，绝不能把它当对象指针塞给游戏。 */
        void *manager=ReadPtr((void *)g_profile->skill_global,0);
        void *sequence=Memory_Readable(manager,0xC0) ?
            (void *)(uintptr_t)((This1)g_profile->combo_get)(manager, NULL,(int)Read32(root,0xC4)):NULL;
        unsigned total=Read32(sequence,0),capacity=Read32((void *)g_profile->menu_skill_combo_capacity,0);
        void *resource=ReadPtr(root,0x50);
        if (Memory_Readable(sequence,16) && capacity<=64 && total<=capacity && Memory_Readable(resource,12)) {
            int x=(int)Read32(root,0x14)+((This2)g_profile->template_value)(resource, NULL,5,3);
            int y=(int)Read32(root,0x18)+((This2)g_profile->template_value)(resource, NULL,6,3);
            void *link=ReadPtr(sequence,4);
            for (unsigned i=0;i<(total ? total:1) && count<64;++i) {
                /* 空套组仍有可见的第一个位置作为区域提示，但不会当成可删除节点。 */
                if (total && !Memory_Readable(link,12)) break;
                out[count++]=(MenuButton){NULL,COMBO_NODE_BASE+i,x+(int)(i%6)*44+21.5,y+(int)(i/6)*48+23.5};
                if (total) link=ReadPtr(link,0);
            }
        }
    }
    if (kind==5 || kind==SKILL_KIND || kind==SETTINGS_KIND || kind==CHARACTER_KIND || kind==NEWGAME_KIND || grid_kind(kind)) {
        /* 原子链从末尾倒走；按真实几何排序，默认最上方选项，不能误选最后一项。 */
        for (unsigned i=1;i<count;++i) {
            MenuButton value=out[i];unsigned j=i;
            while (j && (out[j-1].y>value.y || (out[j-1].y==value.y && out[j-1].x>value.x))) {
                out[j]=out[j-1];--j;
            }
            out[j]=value;
        }
    }
    if(kind==SETTINGS_KIND && count<64) {
        RuntimeFocusRect r;if(SettingsWindow_NativeEntryRect(root,&r))out[count++]=(MenuButton){NULL,0x600,(r.left+r.right)/2.0,(r.top+r.bottom)/2.0};
    }
    return count;
}
static void sprite(void *object,unsigned wanted)
{
    unsigned current=Read32(object,0x40),count=Read32(object,0x44);
    BYTE *array=ReadPtr(object,0x48);
    /* 和原标题hover相同：先重置离开的动画，再改变精灵编号。
     * 防止损坏的资源数量溢出；不重置已经正确的精灵，动画才能自然推进。 */
    if (count>64 || wanted>=count || current>=count || !Memory_Readable(array,count*32u)) return;
    if (current!=wanted) {
        ((This1)g_profile->menu_animation_reset)(array+current*32u, NULL,0);
        Write32(object,0x40,wanted);
    }
}
static void clear_focus(void)
{
    if (!state.root || !g_profile || !visible(state.root)) return;
    int kind=kind_of(state.root);
    if (kind<0 || (kind==CHARACTER_KIND && state.root==character_submission)) return;
    if (kind==0 && (int)Read32(state.root,0xC0)!=-1) return;
    MenuButton list[64];unsigned count=buttons(state.root,kind,list);
    for (unsigned i=0;i<count;++i) if (list[i].id==state.id) {
        if (kind==1) ((This2)g_profile->menu_texture)(list[i].object, NULL,-1,0);
        else if (kind<4 && Read32(list[i].object,0x40)==1) sprite(list[i].object,0);
        if (ReadPtr(state.root,0xA8)==list[i].object) Write32(state.root,0xA8,0);
    }
    if (grid_kind(kind)) {
        if (kind<=SHOP_KIND) Write32(state.root,kind==BAG_KIND ? 0xFC:0xC0,UINT32_MAX);
        Write32(state.root,0xBC,0);
    }
}
void Menu_Suspend(void)
{
    unsigned reason;
    /* 中立门只属于菜单：世界里的Y/物理右键交替不能被菜单清理吞掉第一次手柄攻击。
     * 当前/待退出菜单或真实GUI存在才保留门；纯战斗交接继续原成功历史协议。 */
    bool needs_gate=installed && (state.root || state.barrier || Menu_Context(&reason)!=NULL);
    SettingsWindow_NativeEntryFocus(NULL,0);
    clear_focus();
    memset(&resume_grid,0,sizeof resume_grid);
    memset(&carried,0,sizeof carried);carried.origin=-1;
    memset(&state,0,sizeof state);
    state.barrier=needs_gate;
}
bool Menu_BlocksGameplay(void) { return installed && state.barrier; }
bool Menu_CapturesInput(void) { return installed && frame_captured; }
static bool owns(void)
{
    return installed && g_input.connected && g_input.focused && g_intent.layer==LAYER_MENU;
}
static bool neutral(void)
{
    /* 包括摇杆和扳机，防止关页后持续推杆变成世界移动，或按住RT泄漏快捷技能。 */
    return !g_input.buttons && !g_input.lt && !g_input.rt &&
        g_input.lx==0 && g_input.ly==0 && g_input.rx==0 && g_input.ry==0;
}
static void project(void)
{
    if (!state.owned || !owns() || !visible(state.root) || !Read32(state.root,0x64)) return;
    unsigned reason;void *current=Menu_Context(&reason);
    if (current!=state.root) return;
    int kind=kind_of(current);
    SettingsWindow_NativeEntryFocus(current,kind==SETTINGS_KIND && state.id==0x600);
    if (kind<0 || kind==3 || kind==6 || (kind==0 && (int)Read32(current,0xC0)!=-1)) return;
    MenuButton list[64];unsigned count=buttons(current,kind,list);
    void *focused=NULL;
    for (unsigned i=0;i<count;++i) {
        bool selected=list[i].id==state.id;
        if (kind==1) ((This2)g_profile->menu_texture)(list[i].object, NULL,selected ? 0:-1,0);
        else if (kind<4) sprite(list[i].object,selected ? 1:0);
        if (selected) focused=list[i].object;
    }
    /* 所选按钮若在本帧被移走/隐藏，立即清空上下文，不能留悬空的A8。 */
    /* HUD的A8只允许主按钮，不是内嵌快捷格。原确认函数在交换后还会对A8
     * 读取按钮资源/分派主菜单；快捷格放进去会误读资源指针甚至崩溃。
     * 快捷格的焦点始终保存在state.id，动态框/物品图样从此状态取位置。 */
    Write32(current,0xA8,kind==QUICK_KIND ? 0:(uint32_t)(uintptr_t)focused);
    if (grid_kind(kind)) {
        /* 字段是该页原详情与业务的共同格号；按钮区域写-1，避免点页签时顺便操作旧格。 */
        unsigned index=state.id>=GRID_CELL_BASE && state.id<GRID_CELL_BASE+50 ? state.id-GRID_CELL_BASE:UINT32_MAX;
        if (kind<=SHOP_KIND) Write32(current,kind==BAG_KIND ? 0xFC:0xC0,index);
        /* 原BC来自物理UI路由，原物品详情以它为门；投影本页手柄焦点而不改全局路由。
         * 原base Tick会重算BC，所以Tick后也要重新投影；离开焦点页时清回0。 */
        Write32(current,0xBC,1);
        if (index<50) state.grid_slot[kind-BAG_KIND]=index;
        if (kind>=CRAFT_KIND && state.id>=slot_first[kind-BAG_KIND] &&
            state.id<slot_first[kind-BAG_KIND]+slot_count[kind-BAG_KIND])
            state.grid_slot[kind-BAG_KIND]=state.id-slot_first[kind-BAG_KIND];
    }
}
static int direction(void)
{
    unsigned mask=g_intent.held;
    bool up=(mask & KEY(PAD_UP))!=0,down=(mask & KEY(PAD_DOWN))!=0;
    bool left=(mask & KEY(PAD_LEFT))!=0,right=(mask & KEY(PAD_RIGHT))!=0;
    if (up!=down) return up ? 1:2;
    if (left!=right) return left ? 3:4;
    /* 左摇杆只产生导航方向；右摇杆留给后续页面。小于阈值不会重复夺走焦点。 */
    float x=g_input.lx,y=g_input.ly;
    if (fabsf(x)<0.55f && fabsf(y)<0.55f) return 0;
    return fabsf(y)>=fabsf(x) ? (y<0 ? 1:2):(x<0 ? 3:4);
}
static unsigned navigate(MenuButton *list,unsigned count,unsigned current,int dir,bool aligned)
{
    double best=1e30;unsigned result=current;
    for (unsigned i=0;i<count;++i) if (i!=current) {
        double dx=list[i].x-list[current].x,dy=list[i].y-list[current].y;
        double forward=dir==1 ? -dy:dir==2 ? dy:dir==3 ? -dx:dx;
        double side=dir<=2 ? fabs(dx):fabs(dy);
        /* 只选所推半平面，优先同一行/列附近控件；走到边缘保持原位置，不跳到另一端。 */
        /* 技能图标按视觉行/列导航：边缘没有同列/同行邻项就停留。
         * 允许控件轻微错位（宽/高四分之一），不跨到侧列凑一个斜向候选。
         * 其它非网格页面仍保留通用几何导航，不改变系统/确认框语义。 */
        if(aligned) {
            void *object=list[current].object;
            double span=object ? Read32(object,dir<=2 ? 0x1C:0x20):(dir<=2 ? 44:48);
            if(side>span*0.25)continue;
        }
        double score=forward+side*3.0;
        if (forward>0.5 && score<best) { best=score;result=i; }
    }
    return result;
}
static void focus_sound(int kind)
{
    if (kind==0 && g_profile->menu_sound) {
        typedef int (__cdecl *Play)(int,int,int,int);
        ((Play)g_profile->menu_sound)(0x93,0,100,0);
    }
}
static void hide(void *root) { ((This2)(uintptr_t)Read32(ReadPtr(root,0),0x1C))(root, NULL,0,0); }
static void *grid_container(void)
{
    /* 原Player容器不是场景Role；用已经核对的getter取得，不把Role偏移套到物品池上。 */
    void *container=(void *)(uintptr_t)((This0)g_profile->inventory_get)((void *)g_profile->inventory_root, NULL);
    return Memory_Readable(container,0x2C8) ? container:NULL;
}
static bool grid_cancel(void)
{
    void *container=grid_container();
    if (!container || Read32(container,0x2C4)==UINT32_MAX) return false;
    /* B只放回物品，不关闭页面。原位置被填入其它物品时不能交换出另一件，改找背包空位。 */
    int slot=-1;
    if (carried.container==container && carried.item==Read32(container,0x2C4) && carried.origin>=0 &&
        carried.origin<136 && !((This1)g_profile->item_at)(container, NULL,carried.origin)) slot=carried.origin;
    if (slot>=62 && slot<86) {
        /* 特殊槽放回必须经过原类型门和属性更新，不能用普通背包交换绕过镶嵌/护身石业务。 */
        unsigned index=slot<69 ? 3:slot<81 ? 4:5;
        unsigned id=slot_first[index]+(unsigned)(slot-(index==3 ? 62:index==4 ? 69:81));
        void *root=grid_root(index),*node=ReadPtr(root,0x9C);
        for (unsigned n=0;node && n<128;++n) {
            if (!Memory_Readable(node,0xC0)) break;
            if (Read32(node,0x28)==id && ReadPtr(node,0xA4)==root && Read32(node,0x64)) {
                void *previous=ReadPtr(root,0xA8);Write32(root,0xA8,(uint32_t)(uintptr_t)node);
                uintptr_t operation=index==3 ? g_profile->menu_craft_primary:index==4 ? g_profile->menu_inlay_primary:g_profile->menu_charm_primary;
                ((This3)operation)(root, NULL,0,0,NULL);
                if (ReadPtr(root,0xA8)==node) Write32(root,0xA8,(uint32_t)(uintptr_t)previous);
                break;
            }
            void *next=ReadPtr(node,8);if (next==node) break;node=next;
        }
        if (Read32(container,0x2C4)==UINT32_MAX) {memset(&carried,0,sizeof carried);return true;}
        slot=-1;
    }
    if (slot<0) slot=((This0)g_profile->menu_bag_empty)(container, NULL);
    if (slot>=0 && slot<136 && !((This1)g_profile->item_at)(container, NULL,slot))
        ((This1)g_profile->menu_item_swap)(container, NULL,slot);
    if (Read32(container,0x2C4)==UINT32_MAX) memset(&carried,0,sizeof carried);
    else Log_Write(ControllerText_Menu_HeldItemReturnSlotMissingLog);
    return true;
}
static void grid_switch(void *root)
{
    int kind=kind_of(root);
    void *next=NULL;unsigned target=(unsigned)kind;
    for (unsigned i=1;i<7;++i) {
        unsigned index=((unsigned)(kind-BAG_KIND)+i)%7;
        /* 镶嵌会话只导航道具箱与镶嵌槽，原版同时展示的其它页仅作说明。 */
        if (grid_root(INLAY_KIND-BAG_KIND) && index!=0 && index!=INLAY_KIND-BAG_KIND && index!=6) continue;
        next=grid_root(index);if (next) {target=BAG_KIND+index;break;}
    }
    /* X只在已显示的两种区域间转移插件焦点，不调用Show擅自打开仓库。
     * 下帧路由还会从真实链验证此页，销毁或不在链上的对象不能持续取得焦点。 */
    if (next && next!=root) {
        clear_focus();state.root=next;state.grid_buttons=false;
        state.id=(target<=SHOP_KIND ? GRID_CELL_BASE:slot_first[target-BAG_KIND])+state.grid_slot[target-BAG_KIND];
        state.direction=0;state.barrier=true;
        project();Log_Write(ControllerText_Menu_ItemPanelFocusChangedLog,grid_ids[target-BAG_KIND]);
    }
}
static void *quest_list(void *root)
{
    void *list=(void *)(uintptr_t)((This1)g_profile->get_jm)((void *)g_profile->ui, NULL,0x66);
    /* 原选择编排会遍历文本链。先核对原子对象、行高、条数及链节点，避免空表/损坏表。
     * 此核对只在导航时进行，不把长列表扫描放入每帧悬停投影。 */
    if (!Memory_Readable(list,0xFC) || ReadPtr(list,0xA4)!=root || !Read32(list,0x64) ||
        !Read32(list,0xF0) || Read32(list,0xF0)>4096 || Read32(list,0xE0)>4096) return NULL;
    unsigned total=Read32(list,0xE0);
    if (!total) return NULL;
    void *node=ReadPtr(list,0xE4);
    for (unsigned i=0;i<total;++i) {
        if (!Memory_Readable(node,12) || !Memory_Readable(ReadPtr(node,8),8)) return NULL;
        void *next=ReadPtr(node,0);
        if (next==node) return NULL;
        node=next;
    }
    return list;
}
static void quest_update(void *root)
{
    if (state.barrier || !Read32(root,0x64)) return;
    /* 返回先处理，A没有独立的“执行任务”业务，不制造一个确认动作。 */
    if ((g_intent.pressed & KEY(PAD_B)) || g_intent.menu_toggle) {hide(root);state.barrier=true;return;}
    bool left=(g_intent.pressed & KEY(PAD_LB))!=0,right=(g_intent.pressed & KEY(PAD_RB))!=0;
    if (left!=right) {
        unsigned current=Read32(root,0xC0);
        if (current>=0x78 && current<=0x7B) {
            unsigned target=0x78+((current-0x78+(left ? 3:1))%4);
            /* 原分类helper会记住各分类位置、重建列表、更新文字和原选中外观。 */
            ((This2)g_profile->menu_quest_switch)(root, NULL,(int)target,-1);
            Log_Write(ControllerText_Menu_JournalCategoryChangedLog,target);
            state.direction=0;state.barrier=true;
        }
        return;
    }
    int dir=direction();
    if (dir) {
        if (dir!=state.direction || (int32_t)(g_input.now-state.next_repeat)>=0) {
            void *list=quest_list(root);
            if (list) {
                int index=(int)Read32(list,0xF4),total=(int)Read32(list,0xE0);
                if (index<0) index=0;
                if (index>=total) index=total-1;
                int previous=index,step=1;
                if (dir>=3) {
                    /* 列表上下逐项，左右按实际可见行数翻一屏；分类仍交LB/RB。
                     * 只调用原选择编排，滚动和任务详情同步仍由原游戏处理。 */
                    int height=(int)Read32(list,0x20);
                    if (height>0) step=height/(int)Read32(list,0xF0);
                    if (step<1) step=1;
                    if (step>total) step=total;
                }
                index+=(dir==1 || dir==3) ? -step:step;
                if (index<0) index=0;
                if (index>=total) index=total-1;
                if (index!=previous) ((This1)g_profile->menu_quest_select)(root, NULL,index);
                Log_Write(ControllerText_Menu_JournalCurrentEntryLog,(unsigned long)Read32(root,0xC0),
                    (unsigned long)Read32(list,0xF4));
            }
            state.next_repeat=g_input.now+(dir==state.direction ? 110u:350u);
        }
        state.direction=dir;
    } else state.direction=0;
}
static unsigned load_rows(void *root)
{
    unsigned total=Read32(root,0xC0),start=Read32(root,0xD0);
    if (total>4096 || start>=total) return 0;
    /* 原选择器遍历全记录链；在调用它前核对链，避免损坏记录使原函数解引用空节点。 */
    void *node=ReadPtr(root,0xC4);
    for (unsigned i=0;i<total;++i) {
        if (!Memory_Readable(node,12) || !Memory_Readable(ReadPtr(node,8),0x38)) return 0;
        node=ReadPtr(node,0);
    }
    return total-start<4 ? total-start:4;
}
static bool recover_load_page(void *root)
{
    unsigned total=Read32(root,0xC0),start=Read32(root,0xD0);
    if (!total || total>4096 || start<total || start>4096 || start%4) return false;
    unsigned target=(total-1)/4*4;
    /* 删除尾页最后一项后，原D0可能仍在记录范围外。仍调用原翻页，不直接写分页字段。
     * 有界倒退到最近有效页；原函数拒绝或无变化就停止，不能在帧里无限循环。 */
    for (unsigned i=0;i<1024 && Read32(root,0xD0)>target;++i) {
        unsigned before=Read32(root,0xD0);
        ((This1)g_profile->menu_load_page)(root, NULL,-4);
        unsigned after=Read32(root,0xD0);
        if (after>=before) break;
    }
    unsigned rows=load_rows(root);
    if (!rows) return false;
    ((This1)g_profile->menu_load_select)(root, NULL,(int)rows-1);
    state.id=0x100+rows-1;return true;
}
static void load_update(void *root)
{
    recover_load_page(root);
    unsigned rows=load_rows(root),selected=Read32(root,0xD4);
    if (rows && !state.id) {
        selected=selected<rows ? selected:0;
        ((This1)g_profile->menu_load_select)(root, NULL,(int)selected);
        state.id=0x100+selected;
    }
    if (state.barrier || neutral()) { state.direction=0;return; }
    int dir=direction();
    if (dir!=state.direction) state.hold_since=g_input.now;
    if (dir && (dir!=state.direction || (int32_t)(g_input.now-state.next_repeat)>=0)) {
        int next=(int)selected+(dir==1 ? -1:dir==2 ? 1:0);
        unsigned before=Read32(root,0xD0);
        /* 上下越过本页边缘就接到相邻页；左右仍保留原来的显式翻页。
         * 先走原分页入口，再核对是否真的换页；第一页/最后一页不循环跳转。 */
        if ((rows && (dir>=3 || (dir==1 && selected==0) || (dir==2 && selected+1>=rows))) ||
            (!rows && Read32(root,0xC0)<=4096 && Read32(root,0xD0)>0 && (dir==1 || dir==3))) {
            bool backward=dir==1 || dir==3;
            ((This1)g_profile->menu_load_page)(root, NULL,backward ? -4:4);
            if (Read32(root,0xD0)!=before && dir<=2) next=backward ? 3:0;
        }
        rows=load_rows(root);
        if (rows) {
            if (next<0) next=0;
            if ((unsigned)next>=rows) next=(int)rows-1;
            ((This1)g_profile->menu_load_select)(root, NULL,next);state.id=0x100+(unsigned)next;
        }
        state.next_repeat=g_input.now+(dir==state.direction ? 110u:350u);
        Log_Write(ControllerText_Menu_LoadSlotFocusLog,(unsigned long)Read32(root,0xD0),(unsigned long)Read32(root,0xD4),rows);
    }
    state.direction=dir;
    if ((g_intent.pressed & KEY(PAD_B)) || g_intent.menu_toggle) {
        /* 返回要经过原主操作才会重新显示标题；绝不能只隐藏读档页。
         * 两个坐标取页外无效位置，防止原事件末尾顺便选择其它存档槽。 */
        void *c=ReadPtr(root,0x9C);
        for (unsigned guard=0;c && guard<32;++guard,c=ReadPtr(c,8)) {
            if (!Memory_Readable(c,0xC0)) break;
            if (Read32(c,0x28)==0x97 && ReadPtr(c,0xA4)==root && Read32(c,0x64)) {
                Write32(root,0xA8,(uint32_t)(uintptr_t)c);
                ((This3)g_profile->menu_load_primary)(root, NULL,0,-1,(void *)(intptr_t)-1);break;
            }
            if (ReadPtr(c,8)==c) break;
        }
        state.barrier=true;return;
    }
    if ((g_intent.pressed & KEY(PAD_X)) && rows) {
        void *dialog=ReadPtr((void *)g_profile->menu_message_global,0);
        const char *text=(const char *)(uintptr_t)((This2)g_profile->menu_message_text_lookup)((void *)g_profile->menu_message_text_table, NULL,0x13D,1);
        POINT anchor;
        if (Read32(dialog,0)==g_profile->menu_message_vtable && text && Menu_CursorAnchor(&anchor)) {
            typedef int (__fastcall *Open)(void *, void *,void *,const char *,int,int,int,int);
            ((Open)g_profile->menu_message_open)(dialog, NULL,root,text,anchor.x,anchor.y,0,0);
            Log_Write(ControllerText_Menu_DeleteConfirmationOpenedLog,(unsigned long)Read32(root,0xD0),(unsigned long)Read32(root,0xD4));
        } else Log_Write(ControllerText_Menu_DeleteConfirmationUnavailableLog);
        state.barrier=true;return;
    }
    if ((g_intent.pressed & KEY(PAD_A)) && rows && Read32(root,0xD4)<rows) {
        /* 原读取入口仍负责资格、文件有效性、阶段迁移及失败；空页不发请求。 */
        Log_Write(ControllerText_Menu_LoadSlotRequestedLog,(unsigned long)Read32(root,0xD0),(unsigned long)Read32(root,0xD4));
        ((This1)g_profile->menu_load_submit)(root, NULL,-1);state.barrier=true;
    }
}
static bool frame_page(int kind)
{
    /* 框仅属于物品格/特殊槽和技能图标区；按钮/确认项不是格子，保留原指针。 */
    return (grid_kind(kind) && !state.grid_buttons) || kind==SKILL_KIND || (kind==NEWGAME_KIND && (state.id==0xA0 || state.id==0xA1));
}
bool Menu_HidesCursor(void)
{
    if (ActionMenu_Active()) return true;
    if(owns() && kind_of(state.root)==6)return true;
    RECT rectangle;
    if (owns() && Menu_FocusFrame(&rectangle)) {
        void *container=grid_container();
        /* 空手只画动态框；持有时原软件光标分支画物品图标，保留中心锚点。 */
        return !container || Read32(container,0x2C4)==UINT32_MAX;
    }
    const RuntimeContext *runtime=Runtime_GetContext();unsigned reason;
    return owns() && runtime && runtime->profile && runtime->profile->game_id==GAME_ID_WAIZHUAN &&
        kind_of(Menu_Context(&reason))==0;
}
bool Menu_FocusFrame(RECT *rectangle)
{
    unsigned reason;
    if (!rectangle || !owns() || !state.owned || !frame_page(kind_of(state.root)) ||
        Menu_Context(&reason)!=state.root || !Read32(state.root,0x64)) return false;
    int kind=kind_of(state.root);MenuButton list[64];unsigned count=buttons(state.root,kind,list);
    for(unsigned i=0;i<count;++i)if(list[i].id==state.id) {
        if(!list[i].object) {
            /* 背包/仓库/购买虚拟格为24像素，原资源坐标已由buttons按当前页计算。 */
            if(kind==SKILL_KIND) {
                /* 连招虚拟格原可见43×47，中心含半像素；边界先从原中心还原再内收1。 */
                int x=(int)(list[i].x-21.5),y=(int)(list[i].y-23.5);
                *rectangle=(RECT){x+1,y+1,x+42,y+46};return true;
            }
            int x=(int)list[i].x,y=(int)list[i].y;
            *rectangle=(RECT){x-11,y-11,x+11,y+11};return true;
        }
        void *slot=list[i].object;int x=(int)Read32(slot,0x14),y=(int)Read32(slot,0x18);
        int w=(int)Read32(slot,0x1C),h=(int)Read32(slot,0x20);
        if(kind==SKILL_KIND && !(state.id>=0x81 && state.id<=0x93))return false;
        /* 实物格/技能图标只画内框，不把关闭/页签/金额等按钮误判为方形格子。 */
        *rectangle=(RECT){x+1,y+1,x+w-1,y+h-1};return true;
    }
    return false;
}
bool Menu_CursorAnchor(POINT *point)
{
    if (ActionMenu_Anchor(point)) return true;
    if (!point || !owns() || !state.owned || !Read32(state.root,0x64)) return false;
    unsigned reason;if (Menu_Context(&reason)!=state.root) return false;
    int kind=kind_of(state.root);
    if (kind==QUEST_KIND) {
        /* 和原列表当前位置同源，坐标只用于画焦点标记，不会写真实鼠标。 */
        void *list=(void *)(uintptr_t)((This1)g_profile->get_jm)((void *)g_profile->ui, NULL,0x66);
        unsigned index=Read32(list,0xF4),total=Read32(list,0xE0),height=Read32(list,0xF0);
        if (!Memory_Readable(list,0xFC) || ReadPtr(list,0xA4)!=state.root || !Read32(list,0x64) ||
            !total || total>4096 || index>=total || !height || height>4096) return false;
        /* 原position helper只是累加index次行高；用同样字段一次算出，避免每帧遍历长链。 */
        int64_t bottom64=(int)Read32(list,0x18)+(int)Read32(list,0xDC)+(int64_t)(index+1)*height-6;
        if (bottom64<INT32_MIN || bottom64>INT32_MAX) return false;
        int bottom=(int)bottom64;
        int limit=(int)Read32(list,0x18)+(int)Read32(list,0x20)-6;
        point->x=(int)Read32(list,0x14)+(int)Read32(list,0x1C)-6;
        point->y=bottom<limit ? bottom:limit;
        return point->y>=(int)Read32(list,0x18);
    }
    if(kind==NEWGAME_KIND && state.id==0x9F) {
        void *difficulty=(void *)(uintptr_t)((This1)g_profile->get_jm)((void *)g_profile->ui, NULL,0x9F);
        void *reverse=(void *)(uintptr_t)((This1)g_profile->get_jm)((void *)g_profile->ui, NULL,0xB3);
        if(ReadPtr(difficulty,0xA4)!=state.root || !Read32(difficulty,0x64))return false;
        int right=(int)Read32(difficulty,0x14)+(int)Read32(difficulty,0x1C);
        int bottom=(int)Read32(difficulty,0x18)+(int)Read32(difficulty,0x20);
        if(ReadPtr(reverse,0xA4)==state.root && Read32(reverse,0x64)) {
            int edge=(int)Read32(reverse,0x14)+(int)Read32(reverse,0x1C);
            if(edge>right)right=edge;
        }
        point->x=right-6;point->y=bottom-6;return true;
    }
    if (kind==6) {
        int w=(int)Read32(state.root,0xDC),h=(int)Read32(state.root,0xE0);
        if (w<=0 || h<=0) return false;
        point->x=(int)Read32(state.root,0xD4)+w-6;point->y=(int)Read32(state.root,0xD8)+h-6;return true;
    }
    if (kind==3) {
        if (!load_rows(state.root)) return false;
        void *resource=ReadPtr(state.root,0x50);
        int x=((This2)g_profile->template_value)(resource, NULL,5,1);
        int y=((This2)g_profile->template_value)(resource, NULL,6,1);
        int w=((This2)g_profile->template_value)(resource, NULL,7,1);
        int h=((This2)g_profile->template_value)(resource, NULL,8,1);
        if (w<=0 || h<=0 || Read32(state.root,0xD4)>=4) return false;
        point->x=x+w-6;point->y=y+h*((int)Read32(state.root,0xD4)+1)-6;return true;
    }
    MenuButton list[64];unsigned count=buttons(state.root,kind,list);
    for (unsigned i=0;i<count;++i) if (list[i].id==state.id) {
        if(kind==SETTINGS_KIND && state.id==0x600) {
            RuntimeFocusRect r;if(!SettingsWindow_NativeEntryRect(state.root,&r))return false;
            point->x=r.right-6;point->y=r.bottom-6;return true;
        }
        if (!list[i].object && grid_kind(kind)) {
            void *container=grid_container();bool holding=container && Read32(container,0x2C4)!=UINT32_MAX;
            point->x=(LONG)list[i].x+(holding ? 0:6);point->y=(LONG)list[i].y+(holding ? 0:6);return true;
        }
        if (frame_page(kind)) {
            void *container=grid_container();
            if (container && Read32(container,0x2C4)!=UINT32_MAX) {
                point->x=(LONG)list[i].x;point->y=(LONG)list[i].y;return true;
            }
        }
        if (!list[i].object && kind==SKILL_KIND) {
            point->x=(LONG)(list[i].x+21.5)-6;point->y=(LONG)(list[i].y+23.5)-6;return true;
        }
        point->x=(LONG)Read32(list[i].object,0x14)+(LONG)Read32(list[i].object,0x1C)-6;
        point->y=(LONG)Read32(list[i].object,0x18)+(LONG)Read32(list[i].object,0x20)-6;
        return true;
    }
    return false;
}
/* 原版设置只操作已存在的控件。滑块经原setter同时更新数值和滑块位置，
 * 再执行原实时应用入口；不复制音量/显示参数，也不另写set.ini。 */
static void settings_update(void *root)
{
    MenuButton list[64];unsigned count=buttons(root,SETTINGS_KIND,list);
    if (!count || !Read32(root,0x64)) return;
    unsigned selected=0;
    for (unsigned i=0;i<count;++i) if (list[i].id==state.id) selected=i;
    state.id=list[selected].id;project();
    if (state.barrier || neutral()) {state.direction=0;return;}
    /* 返回先于调值/确认，原close负责标题/游戏内不同返回链及生命周期保存。 */
    if ((g_intent.pressed & KEY(PAD_B)) || g_intent.menu_toggle) {
        ((This0)g_profile->menu_settings_close)(root, NULL);state.barrier=true;return;
    }
    int dir=direction();
    if (dir && (dir!=state.direction || (int32_t)(g_input.now-state.next_repeat)>=0)) {
        unsigned id=state.id;
        if (dir>=3 && id>=0xAB && id<=0xAD) {
            void *slider=list[selected].object;
            if (Memory_Readable(slider,0xE0) && Read32(slider,0xD8)==100 && Read32(slider,0xDC)<=100) {
                int value=(int)Read32(slider,0xDC)+(dir==4 ? 1:-1);
                ((This1)g_profile->menu_settings_slider_set)(slider, NULL,value);
                /* MouseMove会先把坐标交给A8子控件；这里不模拟鼠标拖动，
                 * 临时清A8，仅复用后半段实时应用，随后重新投影手柄焦点。 */
                Write32(root,0xA8,0);
                ((This3)g_profile->menu_settings_apply)(root, NULL,0,0,NULL);
                if (state.root==root && Read32(root,0x64)) project();
            }
        } else {
            unsigned next=navigate(list,count,selected,dir,dir>=3);
            bool horizontal=dir>=3;
            bool same_group=next!=selected && ((id>=0xAE && id<=0xAF && list[next].id>=0xAE && list[next].id<=0xAF) ||
                (id>=0xB0 && id<=0xB1 && list[next].id>=0xB0 && list[next].id<=0xB1));
            /* 上下只移动焦点；左右只在同组选项内选择，不能误执行返回按钮。 */
            /* 入口是额外按钮：从下面选项向右可进入，移动焦点不打开窗口或改原选项。 */
            if(horizontal && !same_group && dir==4 && id!=0x600)
                for(unsigned i=0;i<count;++i)if(list[i].id==0x600){next=i;break;}
            if(horizontal && id==0x600 && dir==3)next=navigate(list,count,selected,dir,false);
            bool entry_move=list[next].id==0x600 || id==0x600;
            if (!horizontal || same_group || entry_move) {
                state.id=list[next].id;project();
                if (horizontal && same_group && state.id!=id) ((This3)g_profile->menu_settings_primary)(root, NULL,0,0,NULL);
            }
        }
        /* 两秒后提高频率，不增大每步数值，避免跨过想要的音量/明暗值。 */
        state.next_repeat=g_input.now+(dir!=state.direction ? 350u:g_input.now-state.hold_since>=2000 ? 35u:110u);
    }
    state.direction=dir;
    if (g_intent.pressed & KEY(PAD_A)) {
        if(state.id==0x600){SettingsWindow_OpenNative(root);state.barrier=true;return;}
        /* 滑块由左右调值；A不把虚构的(0,0)当落点交给滑块。 */
        if (state.id>=0xAE && state.id<=0xB2) {
            project();((This3)g_profile->menu_settings_primary)(root, NULL,0,0,NULL);
        }
        state.barrier=true;
    }
}
static void newgame_update(void *root)
{
    MenuButton list[64];unsigned count=buttons(root,NEWGAME_KIND,list);if(!count || !Read32(root,0x64))return;
    bool found=false;for(unsigned i=0;i<count;++i)if(list[i].id==state.id)found=true;
    if(!found)state.id=list[0].id;
    project();
    if(state.barrier || neutral()){state.direction=0;return;}
    if((g_intent.pressed & KEY(PAD_B)) || g_intent.menu_toggle) {
        for(unsigned i=0;i<count;++i)if(list[i].id==0xA1){state.id=0xA1;project();((This3)g_profile->menu_newgame_primary)(root, NULL,0,0,NULL);break;}
        state.barrier=true;return;
    }
    int dir=direction();
    if(dir && (dir!=state.direction || (int32_t)(g_input.now-state.next_repeat)>=0)) {
        if(dir<=2) {
            unsigned preferred=dir==1 ? 0x9F:0xA0;
            for(unsigned i=0;i<count;++i)if(list[i].id==preferred)state.id=preferred;
        } else if(state.id==0x9F) {
            /* 原9F点击传-1为向后档位，B3传1为反向；隐藏档位资格仍由原函数决定。 */
            ((This1)g_profile->menu_newgame_cycle)(root, NULL,dir==4 ? 1:-1);
        } else {
            unsigned preferred=dir==3 ? 0xA0:0xA1;
            for(unsigned i=0;i<count;++i)if(list[i].id==preferred)state.id=preferred;
        }
        state.next_repeat=g_input.now+(dir==state.direction ? 110u:350u);project();
    }
    state.direction=dir;
    /* Y只请求QOL填写名字，不模拟R，也不会顺便提交创建。 */
    if(g_intent.pressed & KEY(PAD_Y)){QOLModule_RandomName(root);state.barrier=true;return;}
    if(g_intent.pressed & KEY(PAD_A)) {
        /* 难度由左右调整；确定继续原名称/重复存档/非法名校验，不能绕过空名门。 */
        if(state.id==0xA0 || state.id==0xA1){project();((This3)g_profile->menu_newgame_primary)(root, NULL,0,0,NULL);}
        state.barrier=true;
    }
}
void Menu_Update(void)
{
    frame_captured=false;
    if (!installed) return;
    if (!owns()) {
        if (state.owned) {
            /* 正常关页并回中后转入GAME不是新的来源切换。不能再次立起中立门，
             * 否则GAME/MENU会来回跳，玩家永远无法恢复战斗。失焦/原生接管仍需门。 */
            bool finished=!state.root && !state.barrier && g_input.connected && g_input.focused &&
                g_intent.layer!=LAYER_NONE && g_intent.layer!=LAYER_NATIVE &&
                g_intent.layer!=LAYER_MOUSE;
            Menu_Suspend();
            if (finished) state.barrier=false;
        }
        return;
    }
    unsigned reason;void *root=Menu_Context(&reason);
    int kind=root ? kind_of(root):-1;
    frame_captured=state.barrier || kind>=0;
    if (root!=state.root || !state.owned) {
        /* 原确认回调关闭弹窗后，继续原页的格子/按钮区域，而不是重选背包首格。 */
        bool restore=grid_kind(kind) && root==resume_grid.root;
        clear_focus();
        if (restore) {state=resume_grid;memset(&resume_grid,0,sizeof resume_grid);}
        state.root=root;if (!restore) state.id=kind==5 ? 0xFFFFFFFFu:0;state.direction=0;
        state.owned=true;state.barrier=true;frame_captured=true;
        Log_Write(ControllerText_Menu_RoutingChangedLog,(unsigned long)Read32(root,0x28),kind);
    }
    if (state.barrier) {
        /* 进入/离开页面和接管时先吸收旧操作；中立帧本身也不执行按钮。 */
        if (neutral()) state.barrier=false;
    }
    if (kind==6) {
        /* A只加速原滚动，效果在原Tick之后施加；不人工结束或跳到下一句。
         * B仍对应原Esc收起语义，原自动滚完后的生命周期继续由游戏完成。 */
        if (!state.barrier && (g_intent.pressed & KEY(PAD_B))) {
            ((This0)g_profile->menu_text_next)(root, NULL);state.barrier=true;
        }
        return;
    }
    if (kind==3 && Read32(root,0x64)) { load_update(root);return; }
    if (kind==QUEST_KIND) {quest_update(root);return;}
    if (kind==SETTINGS_KIND) {settings_update(root);return;}
    if (kind==NEWGAME_KIND) {newgame_update(root);return;}
    if (grid_kind(kind) && !state.barrier) {
        if ((g_intent.pressed & KEY(PAD_B)) || g_intent.menu_toggle) {
            if (!grid_cancel()) {if (kind==QUICK_KIND) grid_switch(root);else hide(root);}
            state.barrier=true;return;
        }
        if (g_intent.pressed & KEY(PAD_X)) {grid_switch(root);return;}
        /* LB/RB仅属于日志/招式页，其它菜单不切窗口或买卖模式。 */
        if (kind==BAG_KIND && !state.grid_buttons && (g_intent.pressed & KEY(PAD_BACK))) {
            void *container=grid_container();
            if (container && state.id>=GRID_CELL_BASE && state.id<GRID_CELL_BASE+50) {
                unsigned slot=state.id-GRID_CELL_BASE;
                int item=(int)Read32(container,0xA4+slot*4);
                bool held=Read32(container,0x2C4)!=UINT32_MAX;
                /* 原丢弃入口参数是物品编号而非格号；-1代表持有物。
                 * 游戏在玩家脚下生成原地面物，再原样更新库存、数量和持有状态。 */
                if (held || item>=0) ((This1)g_profile->menu_item_drop)(container, NULL,held ? -1:item);
            }
            state.barrier=true;return;
        }
        if (kind!=QUICK_KIND && (g_intent.pressed & KEY(PAD_Y))) {
            state.grid_buttons=!state.grid_buttons;
            state.id=state.grid_buttons ? 0:(kind<=SHOP_KIND ? GRID_CELL_BASE:slot_first[kind-BAG_KIND])+state.grid_slot[kind-BAG_KIND];
            MenuButton candidates[64];
            if (state.grid_buttons && !buttons(root,kind,candidates)) {
                state.grid_buttons=false;state.id=slot_first[kind-BAG_KIND]+state.grid_slot[kind-BAG_KIND];
            }
            state.direction=0;state.barrier=true;return;
        }
    }
    if (kind==SKILL_KIND && !state.barrier && !(g_intent.pressed & KEY(PAD_B)) && !g_intent.menu_toggle) {
        bool left=(g_intent.pressed & KEY(PAD_LB))!=0,right=(g_intent.pressed & KEY(PAD_RB))!=0;
        if (left!=right) {
            ((This1)g_profile->menu_skill_switch)(root, NULL,left ? 0x7F:0x80);
            Log_Write(ControllerText_Menu_SkillPageChangedLog,left ? ControllerText_Menu_SkillLearningLabel:ControllerText_Menu_ComboEditorLabel);
            state.skill_region=0;state.id=0;state.direction=0;state.barrier=true;return;
        }
        if (Read32(root,0xC0)==0x80) {
            if (g_intent.pressed & KEY(PAD_X)) {
                /* X只切换焦点区域，绝不调用原辅助删除。必须松开后才允许下一次确认。 */
                state.skill_region^=1;state.id=0;state.direction=0;state.barrier=true;
                Log_Write(ControllerText_Menu_SkillFocusRegionChangedLog,state.skill_region ? ControllerText_Menu_ComboListRegionLabel:ControllerText_Menu_AvailableSkillsRegionLabel);
                return;
            }
            if ((g_intent.pressed & KEY(PAD_Y)) && Read32(root,0xC4)<4) {
                ((This1)g_profile->menu_skill_slot)(root, NULL,(int)((Read32(root,0xC4)+1)%4));
                Log_Write(ControllerText_Menu_EditingComboGroupLog,(unsigned long)Read32(root,0xC4)+1);
                /* 下方技能不因切套组改变，保留其业务ID；上方节点保留同一位置。
                 * 若新组更短，下面统一合法项校正会选末项，不清零回首项。 */
                state.direction=0;state.barrier=true;project();return;
            }
        }
    }
    MenuButton list[64];unsigned count=kind>=0 ? buttons(root,kind,list):0;
    if (!count || !Read32(root,0x64)) return;
    unsigned selected=0;bool found=false;
    for (unsigned i=0;i<count;++i) if (list[i].id==state.id) {selected=i;found=true;break;}
    if (!found) {
        /* 默认项用业务ID而非鼠标位置，危险按钮不会因为光标停留而成为默认选择。 */
        unsigned preferred=kind==0 ? 0x22:kind==4 ? 0x200:kind==SKILL_KIND ? (Read32(root,0xC0)==0x7F ? list[0].id:0x84):
            grid_kind(kind) && !state.grid_buttons ? (kind<=SHOP_KIND ? GRID_CELL_BASE:slot_first[kind-BAG_KIND])+state.grid_slot[kind-BAG_KIND]:0x9A;
        /* 类型说明栏可以查看，但学习页初始焦点仍取视觉左上的技能图标。 */
        if(kind==SKILL_KIND && Read32(root,0xC0)==0x7F)
            for(unsigned i=0;i<count;++i)if(list[i].id>=0x84 && list[i].id<=0x93){preferred=list[i].id;break;}
        if(kind==1){
            /* 系统项ID顺序不代表视觉顺序；最上方的可见有效按钮才是默认第一项。 */
            unsigned first=0;
            for(unsigned i=1;i<count;++i)if(list[i].y<list[first].y || (list[i].y==list[first].y && list[i].x<list[first].x))first=i;
            preferred=list[first].id;
        }
        for (unsigned i=0;i<count;++i) if (list[i].id==preferred) selected=i;
        /* 上方连招编号代表位置：删除中间项后后继已经补到原位置；只有删末项才失效。
         * 这时选新的末项，不走默认首项。空套组只有编号300的空位标记，仍可安全停留。 */
        if (kind==SKILL_KIND && state.skill_region && state.id>=COMBO_NODE_BASE) selected=count-1;
        state.id=list[selected].id;focus_sound(kind);
    }
    project();
    /* 本体标题激活动画进行时不移动焦点、不改动画、不重复启动业务。 */
    if (state.barrier || neutral() || (kind==0 && (int)Read32(root,0xC0)!=-1)) {
        state.direction=0;return;
    }
    int dir=direction();
    if (dir && (dir!=state.direction || (int32_t)(g_input.now-state.next_repeat)>=0)) {
        selected=navigate(list,count,selected,dir,kind==SKILL_KIND);
        if (state.id!=list[selected].id) focus_sound(kind);
        state.id=list[selected].id;
        state.next_repeat=g_input.now+(dir==state.direction ? 110u:350u);
        Log_Write(ControllerText_Menu_ControlFocusedLog,(unsigned long)Read32(root,0x28),state.id);
        project();
    }
    state.direction=dir;
    /* B优先于同帧A，避免同时按下时既确认又关闭。标题根没有可靠的返回父页，因此B留空。 */
    bool cancel=(g_intent.pressed & KEY(PAD_B))!=0 || g_intent.menu_toggle;
    if (cancel) {
        if (kind==5) {
            ((This3)g_profile->menu_talk_cancel)(root, NULL,0,0,NULL);state.barrier=true;
        } else if (kind==4) {
            for (unsigned i=0;i<count;++i) if (list[i].id==0x200) {
                Write32(root,0xA8,(uint32_t)(uintptr_t)list[i].object);
                ((This3)g_profile->menu_message_primary)(root, NULL,0,0,NULL);break;
            }
            state.barrier=true;
        } else if(kind==CHARACTER_KIND) {
            void *title=(void *)(uintptr_t)((This1)g_profile->get_jm)((void *)g_profile->ui, NULL,0x19);
            if(kind_of(title)==0){((This2)(uintptr_t)Read32(ReadPtr(title,0),0x1C))(title, NULL,1,0);hide(root);}
            state.barrier=true;
        } else if (kind!=0) { hide(root);state.barrier=true; }
        return;
    }
    if (!(g_intent.pressed & KEY(PAD_A))) return;
    Log_Write(ControllerText_Menu_ControlConfirmedLog,(unsigned long)Read32(root,0x28),state.id);
    if (kind==5) {
        /* 2C4是鼠标按下/释放配对标记，不是业务ready；手柄语义不能要求先伪造鼠标按下。
         * 真正保留的是原显示时间防穿透和外传输入冷却，随后提交独立选项。 */
        bool delayed=g_profile->menu_talk_delay_global && Read32((void *)g_profile->menu_talk_delay_global,0)>0;
        uint32_t delta=Read32((void *)g_profile->game_tick,0)-Read32(root,0x74);
        bool recent=(int32_t)delta>=-10 && (int32_t)delta<=10;
        if (!recent && !delayed) {
            project();((This0)g_profile->menu_talk_select)(root, NULL);Menu_Suspend();
        } else Log_Write(ControllerText_Menu_DialogueCooldownRejectedLog);
    } else if (grid_kind(kind)) {
        void *container=grid_container();uint32_t before=Read32(container,0x2C4);
        bool cell=kind<=SHOP_KIND ? state.id>=GRID_CELL_BASE && state.id<GRID_CELL_BASE+50:
            state.id>=slot_first[kind-BAG_KIND] && state.id<slot_first[kind-BAG_KIND]+slot_count[kind-BAG_KIND];
        int origin=kind==BAG_KIND && cell ? (int)(state.id-GRID_CELL_BASE):kind==STORAGE_KIND && cell ? (int)(state.id-GRID_CELL_BASE)+86:
            kind==CRAFT_KIND && cell ? (int)state.id-0x41+62:kind==INLAY_KIND && cell ? (int)state.id-0x49+69:
            kind==CHARM_KIND && cell ? (int)state.id - 0x5E + 81:
            kind==QUICK_KIND && cell ? (int)(state.id-QUICK_CELL_BASE)+50:-1;
        project();
        uintptr_t operations[6]={g_profile->menu_bag_primary,g_profile->menu_storage_primary,g_profile->menu_shop_primary,
            g_profile->menu_craft_primary,g_profile->menu_inlay_primary,g_profile->menu_charm_primary};
        if (kind==QUICK_KIND) {
            /* 原函数先调HUD hover，再按坐标处理快捷格，最后处理A8主按钮。
             * hover包装也会重新project，因此全过程均保持A8为空；不移动鼠标，
             * 物品分类/库存/交换仍由原函数完成，不自己写快捷绑定。 */
            MenuButton slots[64];unsigned total=buttons(root,kind,slots);
            for (unsigned i=0;i<total;++i) if (slots[i].id==state.id)
                ((This3)g_profile->menu_hud_primary)(root, NULL,0,(int)slots[i].x,(void *)(intptr_t)(int)slots[i].y);
        } else ((This3)operations[kind-BAG_KIND])(root, NULL,0,0,NULL);
        /* 实际持有编号改变才记录来源，原堆叠未完成时保持原来源；不直接写库存或物品数量。 */
        if (container && grid_container()==container) {
            uint32_t after=Read32(container,0x2C4);
            if (after==UINT32_MAX) memset(&carried,0,sizeof carried);
            else if (cell && before!=after && origin>=0) {carried.container=container;carried.item=after;carried.origin=origin;}
        }
    } else if (kind==SKILL_KIND) {
        /* 原入口检查技能点/已学状态/连招容量；这里只提供独立焦点，不改技能或列表数据。 */
        project();((This3)g_profile->menu_skill_primary)(root, NULL,0,0,NULL);
        /* 原删除立即完成，所以本帧就校正焦点，不等下一帧重选而让图样短暂消失。
         * 先复核该页还归手柄且未关闭；原回调若换页/取消拥有权，就不能再投影旧页。 */
        if (state.root==root && owns() && Read32(root,0x64) && state.skill_region &&
            Read32(root,0xC0)==0x80 && state.id>=COMBO_NODE_BASE) {
            MenuButton remaining[64];unsigned total=buttons(root,SKILL_KIND,remaining);
            bool retained=false;
            for (unsigned i=0;i<total;++i) if (remaining[i].id==state.id) retained=true;
            if (total && !retained) state.id=remaining[total-1].id;
            project();
        }
    } else if(kind==CHARACTER_KIND) {
        project();void *selected_character=ReadPtr(root,0xA8),*previous=character_submission;
        character_submission=root;
        ((This3)g_profile->menu_character_primary)(root, NULL,0,0,NULL);
        character_submission=previous;
        /* 返回后才撤掉旧焦点；仍是同一页/同一选中对象才写，不能覆盖其它回调的新值。 */
        if(character_submission!=root && kind_of(root)==CHARACTER_KIND && ReadPtr(root,0xA8)==selected_character)Write32(root,0xA8,0);
    } else if (kind==4) {
        project();((This3)g_profile->menu_message_primary)(root, NULL,0,0,NULL);
    } else if (kind==0) ((This1)g_profile->menu_title_activate)(root, NULL,(int)state.id);
    else if (kind==1) {
        /* 原系统事件仅消费root+A8当前按钮，三个栈参数未使用；传空参数，不造鼠标对象。 */
        project();((This3)g_profile->menu_system_primary)(root, NULL,0,0,NULL);
    } else if (state.id==0x9B) hide(root);
    else ((This0)g_profile->menu_confirm_submit)(root, NULL);
    /* 即使业务拒绝/原地停留，也必须松开后才能再确认；一次按下只提交一次。 */
    state.barrier=true;
}

static int tick(void *self,unsigned kind)
{
    /* 删除确认原Tick先做GetCursorPos命中再调base Tick。手柄只替换命中生产部分，
     * base仍完整更新显示/子控件，随后用自己的标签焦点决定原文字高亮。 */
    if(kind==CHARACTER_KIND && self==state.root)project();
    int result=kind==4 && owns() && kind_of(self)==4 ?
        ((This0)g_profile->menu_message_base_tick)(self, NULL):((This0)original[kind][0])(self, NULL);
    /* 原版Tick可能因鼠标不在根页上而清A8；随后重新投影独立焦点，不改全局鼠标路由。 */
    if (self==state.root) project();
    /* 原Space在每次Tick额外F4减去2×F0。只改原滚动位移，不加速脚本或强制关文字。
     * 原Tick可能自动关闭/换页，返回后重新核对拥有权，不能改写已经关闭的文本。 */
    if (kind==6 && self==state.root && owns() && !state.barrier && Read32(self,0x64) &&
        (g_intent.held & KEY(PAD_A)) && !Input_PhysicalDown(VK_SPACE)) {
        int64_t position=(int32_t)Read32(self,0xF4),speed=(int32_t)Read32(self,0xF0);
        if (speed>0 && speed<=1024) {
            position-=speed*2;
            if (position<INT32_MIN) position=INT32_MIN;
            Write32(self,0xF4,(uint32_t)(int32_t)position);
        }
    }
    return result;
}
static int show(void *self,unsigned kind,int active,int mode)
{
    /* 捕捉同一常驻对象的关闭/重开，不能只靠指针变化识别新的一页。 */
    bool modal=active && (kind==2 || kind==4);
    if (modal && state.owned && grid_kind(kind_of(state.root))) {
        /* Show会同步进入原回调，先备份，随后清除旧高亮和输入重复计时。 */
        MenuState previous=state;Menu_Suspend();resume_grid=previous;
    } else if (active && grid_kind(kind) && (resume_grid.root || (state.owned && grid_kind(kind_of(state.root))))) {
        /* 原交易/镶嵌回调可能再次Show已打开的道具箱，不能因此丢掉商品页焦点。 */
        state.barrier=true;state.direction=0;
    } else if ((self==state.root && grid_kind(kind)) || (active && kind!=2 && kind!=4)) Menu_Suspend();
    else if (self==state.root) {clear_focus();state.root=NULL;state.barrier=true;}
    return ((This2)original[kind][1])(self, NULL,active,mode);
}
static int hover(void *self,unsigned kind,int event,int x,void *y)
{
    if (owns()) {
        /* 手柄拥有菜单时不执行GetCursorPos/picker；自己的焦点可以在光标移到页面外仍保持。 */
        if (self==state.root) project();
        return 0;
    }
    return ((This3)original[kind][2])(self, NULL,event,x,y);
}
/* fastcall的第二寄存器参数是占位；后面的栈参数数量与原thiscall严格一致。 */
#define MENU_WRAPPERS(n) static int __fastcall tick##n(void *s,void *unused) { (void)unused;return tick(s,n); } static int __fastcall show##n(void *s,void *unused,int active,int mode) { (void)unused;return show(s,n,active,mode); } static int __fastcall hover##n(void *s,void *unused,int e,int x,void *y) { (void)unused;return hover(s,n,e,x,y); }
MENU_WRAPPERS(0)
MENU_WRAPPERS(1)
MENU_WRAPPERS(2)
MENU_WRAPPERS(3)
MENU_WRAPPERS(4)
MENU_WRAPPERS(5)
MENU_WRAPPERS(6)
MENU_WRAPPERS(7)
MENU_WRAPPERS(8)
MENU_WRAPPERS(9)
MENU_WRAPPERS(10)
MENU_WRAPPERS(11)
MENU_WRAPPERS(12)
MENU_WRAPPERS(13)
MENU_WRAPPERS(14)
MENU_WRAPPERS(15)
MENU_WRAPPERS(16)
MENU_WRAPPERS(17)
MENU_WRAPPERS(18)
static uintptr_t replacement[MENU_KINDS][3];
static BYTE saved_talk_picker[5];
static bool picker_installed;
static BYTE saved_skill_calls[3][5];
static bool skill_calls_installed[3];
static uintptr_t skill_call_address[3],skill_call_target[3],skill_call_hook[3];
static BYTE saved_shop_positions[3][6];static bool shop_positions_installed[3];
static BOOL (WINAPI *original_menu_position)(LPPOINT);
static BOOL WINAPI menu_position_hook(LPPOINT point)
{
    /* 商店确认框的这两处只取摆放位置，不用它来选商品；手柄从独立焦点取得锚点。 */
    if (owns()) {
        if (Menu_CursorAnchor(point)) return TRUE;
        unsigned reason;void *root=Menu_Context(&reason);
        point->x=(int)Read32(root,0x14)+(int)Read32(root,0x1C)/2;
        point->y=(int)Read32(root,0x18)+(int)Read32(root,0x20)/2;return TRUE;
    }
    return original_menu_position(point);
}
static int __fastcall skill_base_hook(void *root,void *unused)
{
    (void)unused;
    int result=((This0)g_profile->menu_message_base_tick)(root, NULL);
    /* 原base更新可以清掉悬停；必须在原技能详情刷新之前重新投影手柄焦点。 */
    if (owns() && state.root==root && kind_of(root)==SKILL_KIND) project();
    return result;
}
static int __fastcall combo_hit_hook(void *root,void *unused)
{
    (void)unused;
    if (owns() && kind_of(root)==SKILL_KIND) {
        MenuButton list[64];unsigned count=buttons(root,SKILL_KIND,list);
        for (unsigned i=0;i<count;++i) if (state.root==root && list[i].id==state.id &&
            list[i].id>=COMBO_NODE_BASE) {
            void *manager=ReadPtr((void *)g_profile->skill_global,0);
            void *sequence=Memory_Readable(manager,0xC0) ?
                (void *)(uintptr_t)((This1)g_profile->combo_get)(manager, NULL,(int)Read32(root,0xC4)):NULL;
            unsigned index=list[i].id-COMBO_NODE_BASE;
            return index<Read32(sequence,0) ? (int)index:-1;
        }
        return -1;
    }
    /* 物理来源仍调用原GetCursorPos命中函数，不改变原鼠标编辑方式。 */
    return ((This0)g_profile->menu_skill_combo_hit)(root, NULL);
}
static bool patch_menu_call(uintptr_t address,uintptr_t target,uintptr_t hook,BYTE *saved)
{
    if (!Memory_Readable((void *)address,5)) return false;
    memcpy(saved,(void *)address,5);int32_t displacement;
    memcpy(&displacement,saved+1,4);
    if (saved[0]!=0xE8 || address+5+displacement!=target) return false;
    BYTE bytes[5]={0xE8};displacement=(int32_t)(hook-address-5);
    memcpy(bytes+1,&displacement,4);
    return Memory_Patch((void *)address,bytes,5);
}
static int __fastcall talk_picker_hook(void *root,void *unused)
{
    (void)unused;
    if (owns() && kind_of(root)==5) {
        MenuButton list[64];unsigned n=buttons(root,5,list);
        for (unsigned i=0;i<n;++i) if (state.root==root && list[i].id==state.id) return (int)(uintptr_t)list[i].object;
        return 0;
    }
    return ((This0)g_profile->menu_talk_picker)(root, NULL);
}
void Menu_Shutdown(void)
{
    Menu_Suspend();
    for (unsigned i=0;i<MENU_KINDS;++i) for (unsigned j=0;j<3;++j) {
        void *slot=(void *)(tables[i]+offsets[j]);
        /* 只恢复仍指向自己的槽；其它插件后来的改写不能被本模块覆盖。 */
        if (tables[i] && Read32(slot,0)==replacement[i][j])
            Memory_Patch(slot,&original[i][j],4);
    }
    if (picker_installed) {
        BYTE bytes[5]={0xE8};int32_t rel=(int32_t)((uintptr_t)talk_picker_hook-g_profile->menu_talk_picker_call-5);
        memcpy(bytes+1,&rel,4);
        if (!memcmp((void *)g_profile->menu_talk_picker_call,bytes,5)) Memory_Patch((void *)g_profile->menu_talk_picker_call,saved_talk_picker,5);
        picker_installed=false;
    }
    for (unsigned i=0;i<3;++i) if (skill_calls_installed[i]) {
        BYTE bytes[5]={0xE8};int32_t rel=(int32_t)(skill_call_hook[i]-skill_call_address[i]-5);
        memcpy(bytes+1,&rel,4);
        if (Memory_Readable((void *)skill_call_address[i],5) && !memcmp((void *)skill_call_address[i],bytes,5))
            Memory_Patch((void *)skill_call_address[i],saved_skill_calls[i],5);
        skill_calls_installed[i]=false;
    }
    uintptr_t positions[3]={g_profile->menu_shop_position_call1,g_profile->menu_shop_position_call2,g_profile->menu_inlay_position_call};
    for (unsigned i=0;i<3;++i) if (shop_positions_installed[i]) {
        BYTE bytes[6]={0xE8,0,0,0,0,0x90};int32_t rel=(int32_t)((uintptr_t)menu_position_hook-positions[i]-5);
        memcpy(bytes+1,&rel,4);
        if (!memcmp((void *)positions[i],bytes,6)) Memory_Patch((void *)positions[i],saved_shop_positions[i],6);
        shop_positions_installed[i]=false;
    }
    installed=false;frame_captured=false;memset(&state,0,sizeof state);
}
bool Menu_Initialize(void)
{
    if (installed) return true;
    tables[0]=g_profile->menu_title_vtable;tables[1]=g_profile->menu_system_vtable;
    tables[2]=g_profile->menu_confirm_vtable;tables[3]=g_profile->menu_load_vtable;tables[4]=g_profile->menu_message_vtable;
    tables[5]=g_profile->menu_talk_vtable;tables[6]=g_profile->menu_text_vtable;
    tables[7]=g_profile->menu_quest_vtable;tables[8]=g_profile->menu_skill_vtable;
    tables[9]=g_profile->menu_bag_vtable;tables[10]=g_profile->menu_storage_vtable;
    tables[11]=g_profile->menu_shop_vtable;tables[12]=g_profile->menu_craft_vtable;
    tables[13]=g_profile->menu_inlay_vtable;tables[14]=g_profile->menu_charm_vtable;tables[15]=g_profile->menu_hud_vtable;tables[16]=g_profile->menu_settings_vtable;tables[17]=g_profile->menu_character_vtable;tables[18]=g_profile->menu_newgame_vtable;
    uintptr_t expected[MENU_KINDS][3]={
        {g_profile->menu_title_tick,g_profile->menu_title_show,g_profile->menu_title_hover},
        {g_profile->menu_system_tick,g_profile->menu_system_show,g_profile->menu_system_hover},
        {g_profile->menu_confirm_tick,g_profile->menu_confirm_show,g_profile->menu_confirm_hover},
        {g_profile->menu_load_tick,g_profile->menu_load_show,g_profile->menu_load_hover},
        {g_profile->menu_message_tick,g_profile->menu_message_show,g_profile->menu_message_hover},
        {g_profile->menu_talk_tick,g_profile->menu_talk_show,g_profile->menu_talk_hover},
        {g_profile->menu_text_tick,g_profile->menu_text_show,g_profile->menu_text_hover},
        {g_profile->menu_quest_tick,g_profile->menu_quest_show,g_profile->menu_quest_hover},
        {g_profile->menu_skill_tick,g_profile->menu_skill_show,g_profile->menu_skill_hover},
        {g_profile->menu_bag_tick,g_profile->menu_bag_show,g_profile->menu_bag_hover},
        {g_profile->menu_storage_tick,g_profile->menu_storage_show,g_profile->menu_storage_hover},
        {g_profile->menu_shop_tick,g_profile->menu_shop_show,g_profile->menu_shop_hover},
        {g_profile->menu_craft_tick,g_profile->menu_craft_show,g_profile->menu_craft_hover},
        {g_profile->menu_inlay_tick,g_profile->menu_inlay_show,g_profile->menu_inlay_hover},
        {g_profile->menu_charm_tick,g_profile->menu_charm_show,g_profile->menu_charm_hover},
        {g_profile->menu_hud_tick,g_profile->menu_hud_show,g_profile->menu_hud_hover},
        {g_profile->menu_settings_tick,g_profile->menu_settings_show,g_profile->menu_settings_hover},
        {g_profile->menu_character_tick,g_profile->menu_character_show,g_profile->menu_character_hover},
        {g_profile->menu_newgame_tick,g_profile->menu_newgame_show,g_profile->menu_newgame_hover}};
    uintptr_t hooks[MENU_KINDS][3]={{(uintptr_t)tick0,(uintptr_t)show0,(uintptr_t)hover0},
        {(uintptr_t)tick1,(uintptr_t)show1,(uintptr_t)hover1},
        {(uintptr_t)tick2,(uintptr_t)show2,(uintptr_t)hover2},
        {(uintptr_t)tick3,(uintptr_t)show3,(uintptr_t)hover3},
        {(uintptr_t)tick4,(uintptr_t)show4,(uintptr_t)hover4},
        {(uintptr_t)tick5,(uintptr_t)show5,(uintptr_t)hover5},
        {(uintptr_t)tick6,(uintptr_t)show6,(uintptr_t)hover6},
        {(uintptr_t)tick7,(uintptr_t)show7,(uintptr_t)hover7},
        {(uintptr_t)tick8,(uintptr_t)show8,(uintptr_t)hover8},
        {(uintptr_t)tick9,(uintptr_t)show9,(uintptr_t)hover9},
        {(uintptr_t)tick10,(uintptr_t)show10,(uintptr_t)hover10},
        {(uintptr_t)tick11,(uintptr_t)show11,(uintptr_t)hover11},
        {(uintptr_t)tick12,(uintptr_t)show12,(uintptr_t)hover12},
        {(uintptr_t)tick13,(uintptr_t)show13,(uintptr_t)hover13},
        {(uintptr_t)tick14,(uintptr_t)show14,(uintptr_t)hover14},
        {(uintptr_t)tick15,(uintptr_t)show15,(uintptr_t)hover15},
        {(uintptr_t)tick16,(uintptr_t)show16,(uintptr_t)hover16},
        {(uintptr_t)tick17,(uintptr_t)show17,(uintptr_t)hover17},
        {(uintptr_t)tick18,(uintptr_t)show18,(uintptr_t)hover18}};
    memcpy(replacement,hooks,sizeof hooks);
    /* 全部槽先验证再写，拒绝被其它补丁替换的入口；不会占用DisplayFix的Draw/picker。 */
    for (unsigned i=0;i<MENU_KINDS;++i) for (unsigned j=0;j<3;++j) {
        if (!tables[i] || !expected[i][j] || Read32((void *)tables[i],offsets[j])!=expected[i][j]) return false;
        original[i][j]=expected[i][j];
    }
    /* 业务槽也核对；只对本批精确类调用原入口，不能猜未知类的虚表+3C。 */
    if (!g_profile->menu_system_primary || !g_profile->menu_confirm_submit ||
        Read32((void *)tables[1],0x24)!=g_profile->menu_system_primary ||
        Read32((void *)tables[2],0x3C)!=g_profile->menu_confirm_submit ||
        !g_profile->menu_load_primary || Read32((void *)tables[3],0x24)!=g_profile->menu_load_primary ||
        !g_profile->menu_message_primary || Read32((void *)tables[4],0x24)!=g_profile->menu_message_primary ||
        !g_profile->menu_quest_primary || Read32((void *)tables[7],0x24)!=g_profile->menu_quest_primary ||
        !g_profile->menu_skill_primary || Read32((void *)tables[8],0x24)!=g_profile->menu_skill_primary ||
        !g_profile->menu_skill_secondary || Read32((void *)tables[8],0x2C)!=g_profile->menu_skill_secondary ||
        !g_profile->menu_bag_primary || Read32((void *)tables[9],0x24)!=g_profile->menu_bag_primary ||
        !g_profile->menu_storage_primary || Read32((void *)tables[10],0x24)!=g_profile->menu_storage_primary ||
        !g_profile->inventory_get || !g_profile->item_at || !g_profile->menu_item_swap || !g_profile->menu_bag_empty) return false;
    if (!g_profile->menu_settings_primary || Read32((void *)tables[16],0x24)!=g_profile->menu_settings_primary ||
        !g_profile->menu_settings_close || !g_profile->menu_settings_apply || !g_profile->menu_settings_slider_set) return false;
    if(Read32((void *)tables[17],0x24)!=g_profile->menu_character_primary ||
        Read32((void *)tables[18],0x24)!=g_profile->menu_newgame_primary || !g_profile->menu_newgame_cycle)return false;
    uintptr_t primaries[4]={g_profile->menu_shop_primary,g_profile->menu_craft_primary,g_profile->menu_inlay_primary,g_profile->menu_charm_primary};
    for (unsigned i=0;i<4;++i) if (!primaries[i] || Read32((void *)tables[i+11],0x24)!=primaries[i]) return false;
    if (!HookManager_Claim(SHARED_HOOK_CONTROLLER_MENU,RUNTIME_MODULE_CONTROLLER)) return false;
    for (unsigned i=0;i<MENU_KINDS;++i) for (unsigned j=0;j<3;++j) {
        if (!Memory_Patch((void *)(tables[i]+offsets[j]),&replacement[i][j],4)) {
            Menu_Shutdown();Log_Write(ControllerText_Menu_FocusHooksInstallFailedLog);return false;
        }
    }
    if (!Memory_Readable((void *)g_profile->menu_talk_picker_call,5)) {Menu_Shutdown();return false;}
    memcpy(saved_talk_picker,(void *)g_profile->menu_talk_picker_call,5);int32_t displacement;
    memcpy(&displacement,saved_talk_picker+1,4);
    if (saved_talk_picker[0]!=0xE8 || g_profile->menu_talk_picker_call+5+displacement!=g_profile->menu_talk_picker) {Menu_Shutdown();return false;}
    BYTE picker_bytes[5]={0xE8};displacement=(int32_t)((uintptr_t)talk_picker_hook-g_profile->menu_talk_picker_call-5);
    memcpy(picker_bytes+1,&displacement,4);picker_installed=Memory_Patch((void *)g_profile->menu_talk_picker_call,picker_bytes,5);
    if (!picker_installed) {Menu_Shutdown();return false;}
    skill_call_address[0]=g_profile->menu_skill_base_call;
    skill_call_address[1]=g_profile->menu_skill_combo_call1;skill_call_address[2]=g_profile->menu_skill_combo_call2;
    skill_call_target[0]=g_profile->menu_message_base_tick;
    skill_call_target[1]=skill_call_target[2]=g_profile->menu_skill_combo_hit;
    skill_call_hook[0]=(uintptr_t)skill_base_hook;
    skill_call_hook[1]=skill_call_hook[2]=(uintptr_t)combo_hit_hook;
    for (unsigned i=0;i<3;++i) {
        skill_calls_installed[i]=patch_menu_call(skill_call_address[i],skill_call_target[i],skill_call_hook[i],saved_skill_calls[i]);
        if (!skill_calls_installed[i]) {Menu_Shutdown();return false;}
    }
    uintptr_t positions[3]={g_profile->menu_shop_position_call1,g_profile->menu_shop_position_call2,g_profile->menu_inlay_position_call};
    for (unsigned i=0;i<3;++i) {
        if (!Memory_Readable((void *)positions[i],6)) {Menu_Shutdown();return false;}
        memcpy(saved_shop_positions[i],(void *)positions[i],6);uintptr_t iat;
        memcpy(&iat,saved_shop_positions[i]+2,4);
        if (saved_shop_positions[i][0]!=0xFF || saved_shop_positions[i][1]!=0x15 || iat!=g_profile->cursor_position_iat ||
            !Memory_Readable((void *)iat,4)) {Menu_Shutdown();return false;}
        original_menu_position=*(BOOL (WINAPI **)(LPPOINT))iat;
        if (!original_menu_position) {Menu_Shutdown();return false;}
        BYTE bytes[6]={0xE8,0,0,0,0,0x90};int32_t rel=(int32_t)((uintptr_t)menu_position_hook-positions[i]-5);
        memcpy(bytes+1,&rel,4);shop_positions_installed[i]=Memory_Patch((void *)positions[i],bytes,6);
        if (!shop_positions_installed[i]) {Menu_Shutdown();return false;}
    }
    installed=true;state.barrier=true;
    Log_Write(ControllerText_Menu_HooksInstalledLog);
    return true;
}
