#include "Menu.h"
#include <math.h>
#include <string.h>

/* 一页仅收集本批已核对的按钮，不扫描全游戏注册表或保存子控件裸指针。
 * 每次使用之前重新从该页子链取得对象，销毁/隐藏的按钮自然失去资格。 */
typedef struct { void *object; unsigned id; double x,y; } MenuButton;
typedef struct {
    void *root;
    unsigned id;
    int direction;
    uint32_t next_repeat;
    bool barrier,owned;
} MenuState;
static MenuState state;
static bool installed,frame_captured;
static uintptr_t tables[7],original[7][3];
static const unsigned offsets[3]={4,0x1C,0x30};

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
    /* 这张对话类也被其它页面复用；当前只接通owner仍是原读档页的删除确认。
     * 不能把所有动态2A文本菜单都误认为删除确认或泛调用owner的虚表。 */
    void *owner=ReadPtr(root,0xCC);
    if (table && table==g_profile->menu_message_vtable && Read32(owner,0)==g_profile->menu_load_vtable &&
        Read32(owner,0x28)==0x94) return 4;
    if (table && table==g_profile->menu_talk_vtable) return 5;
    if (table && table==g_profile->menu_text_vtable) return 6;
    return -1;
}
static bool page(void *object,void *hud)
{
    void *resource=ReadPtr(object,0x50);
    if (object!=hud && visible(object) && (kind_of(object)==5 || kind_of(object)==6)) return true;
    return object!=hud && visible(object) && Memory_Readable(resource,12) &&
        ((This1)g_profile->ui_property)(resource,13)==1;
}
void *Menu_Context(unsigned *reason)
{
    *reason=0;
    if (!g_profile) return NULL;
    void *ui=(void *)g_profile->ui,*hud=ReadPtr((void *)g_profile->skill_global,0);
    void *capture=ReadPtr(ui,0x3C);
    if (capture!=hud && visible(capture)) {
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
    for (unsigned n=0;node && node!=ui && n<256;++n) {
        if (!Memory_Readable(node,0xC0)) break;
        if (page(node,hud)) {
            if (kind_of(node)==2 || kind_of(node)==4 || kind_of(node)==6) { *reason=2;return node; }
            if (!first) first=node;
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
    if (first) *reason=2;
    return first;
}
static bool allowed(int kind,unsigned id)
{
    if (kind==0) return (id>=0x1F && id<=0x23) || id==0xA7;
    if (kind==1) return id==0x2E || id==0x30 || id==0x31;
    return kind==2 && (id==0x9A || id==0x9B);
}
static unsigned buttons(void *root,int kind,MenuButton *out)
{
    unsigned count=0;
    void *node=ReadPtr(root,0x9C);
    for (unsigned n=0;node && n<128;++n) {
        if (!Memory_Readable(node,0xC0)) break;
        unsigned id=Read32(node,0x28);
        if (kind==4 && id==0x2A && Read32(node,0xC4)<=1) id=0x200+Read32(node,0xC4);
        if (kind==5 && id==0x2A) id=Read32(node,0xC4);
        int width=(int)Read32(node,0x1C),height=(int)Read32(node,0x20);
        if (count<64 && (kind==5 ? Read32(node,0x28)==0x2A && id!=0xFFFFFFFFu:kind==4 ? id==0x200 || id==0x201:allowed(kind,id)) && ReadPtr(node,0xA4)==root &&
            Read32(node,0x64) && width>0 && height>0) {
            out[count++]=(MenuButton){node,id,(int)Read32(node,0x14)+width/2.0,
                                           (int)Read32(node,0x18)+height/2.0};
        }
        void *next=ReadPtr(node,8);
        if (next==node) break;
        node=next;
    }
    if (kind==5) {
        /* 原子链从末尾倒走；按真实几何排序，默认最上方选项，不能误选最后一项。 */
        for (unsigned i=1;i<count;++i) {
            MenuButton value=out[i];unsigned j=i;
            while (j && (out[j-1].y>value.y || (out[j-1].y==value.y && out[j-1].x>value.x))) {
                out[j]=out[j-1];--j;
            }
            out[j]=value;
        }
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
        ((This1)g_profile->menu_animation_reset)(array+current*32u,0);
        Write32(object,0x40,wanted);
    }
}
static void clear_focus(void)
{
    if (!state.root || !g_profile || !visible(state.root)) return;
    int kind=kind_of(state.root);
    if (kind<0) return;
    if (kind==0 && (int)Read32(state.root,0xC0)!=-1) return;
    MenuButton list[64];unsigned count=buttons(state.root,kind,list);
    for (unsigned i=0;i<count;++i) if (list[i].id==state.id) {
        if (kind==1) ((This2)g_profile->menu_texture)(list[i].object,-1,0);
        else if (kind<4 && Read32(list[i].object,0x40)==1) sprite(list[i].object,0);
        if (ReadPtr(state.root,0xA8)==list[i].object) Write32(state.root,0xA8,0);
    }
}
void Menu_Suspend(void)
{
    unsigned reason;
    /* 中立门只属于菜单：世界里的Y/物理右键交替不能被菜单清理吞掉第一次手柄攻击。
     * 当前/待退出菜单或真实GUI存在才保留门；纯战斗交接继续原成功历史协议。 */
    bool needs_gate=installed && (state.root || state.barrier || Menu_Context(&reason)!=NULL);
    clear_focus();
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
    if (kind<0 || kind==3 || kind==6 || (kind==0 && (int)Read32(current,0xC0)!=-1)) return;
    MenuButton list[64];unsigned count=buttons(current,kind,list);
    void *focused=NULL;
    for (unsigned i=0;i<count;++i) {
        bool selected=list[i].id==state.id;
        if (kind==1) ((This2)g_profile->menu_texture)(list[i].object,selected ? 0:-1,0);
        else if (kind<4) sprite(list[i].object,selected ? 1:0);
        if (selected) focused=list[i].object;
    }
    /* 所选按钮若在本帧被移走/隐藏，立即清空上下文，不能留悬空的A8。 */
    Write32(current,0xA8,(uint32_t)(uintptr_t)focused);
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
static unsigned navigate(MenuButton *list,unsigned count,unsigned current,int dir)
{
    double best=1e30;unsigned result=current;
    for (unsigned i=0;i<count;++i) if (i!=current) {
        double dx=list[i].x-list[current].x,dy=list[i].y-list[current].y;
        double forward=dir==1 ? -dy:dir==2 ? dy:dir==3 ? -dx:dx;
        double side=dir<=2 ? fabs(dx):fabs(dy);
        /* 只选所推半平面，优先同一行/列附近控件；走到边缘保持原位置，不跳到另一端。 */
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
static void hide(void *root) { ((This2)(uintptr_t)Read32(ReadPtr(root,0),0x1C))(root,0,0); }
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
static void load_update(void *root)
{
    unsigned rows=load_rows(root),selected=Read32(root,0xD4);
    if (rows && !state.id) {
        selected=selected<rows ? selected:0;
        ((This1)g_profile->menu_load_select)(root,(int)selected);
        state.id=0x100+selected;
    }
    if (state.barrier || neutral()) { state.direction=0;return; }
    int dir=direction();
    if (dir && (dir!=state.direction || (int32_t)(g_input.now-state.next_repeat)>=0)) {
        if (dir>=3) ((This1)g_profile->menu_load_page)(root,dir==3 ? -4:4);
        rows=load_rows(root);
        if (rows) {
            int next=(int)selected+(dir==1 ? -1:dir==2 ? 1:0);
            if (next<0) next=0;
            if ((unsigned)next>=rows) next=(int)rows-1;
            ((This1)g_profile->menu_load_select)(root,next);state.id=0x100+(unsigned)next;
        }
        state.next_repeat=g_input.now+(dir==state.direction ? 110u:350u);
        Log_Write("[读档焦点] 页起点=%lu 页内槽=%lu 有效槽=%u。",(unsigned long)Read32(root,0xD0),(unsigned long)Read32(root,0xD4),rows);
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
                ((This3)g_profile->menu_load_primary)(root,0,-1,(void *)(intptr_t)-1);break;
            }
            if (ReadPtr(c,8)==c) break;
        }
        state.barrier=true;return;
    }
    if ((g_intent.pressed & KEY(PAD_X)) && rows) {
        void *dialog=ReadPtr((void *)g_profile->menu_message_global,0);
        const char *text=(const char *)(uintptr_t)((This2)g_profile->menu_message_text_lookup)((void *)g_profile->menu_message_text_table,0x13D,1);
        POINT anchor;
        if (Read32(dialog,0)==g_profile->menu_message_vtable && text && Menu_CursorAnchor(&anchor)) {
            typedef int (__attribute__((thiscall)) *Open)(void *,void *,const char *,int,int,int,int);
            ((Open)g_profile->menu_message_open)(dialog,root,text,anchor.x,anchor.y,0,0);
            Log_Write("[读档操作] 打开原删除确认，页=%lu 槽=%lu。",(unsigned long)Read32(root,0xD0),(unsigned long)Read32(root,0xD4));
        } else Log_Write("[读档][拒绝] 原删除确认对象/文本/焦点未就绪。");
        state.barrier=true;return;
    }
    if ((g_intent.pressed & KEY(PAD_A)) && rows && Read32(root,0xD4)<rows) {
        /* 原读取入口仍负责资格、文件有效性、阶段迁移及失败；空页不发请求。 */
        Log_Write("[读档操作] 请求载入页起点=%lu 槽=%lu。",(unsigned long)Read32(root,0xD0),(unsigned long)Read32(root,0xD4));
        ((This1)g_profile->menu_load_submit)(root,-1);state.barrier=true;
    }
}
bool Menu_CursorAnchor(POINT *point)
{
    if (!point || !owns() || !state.owned || !Read32(state.root,0x64)) return false;
    unsigned reason;if (Menu_Context(&reason)!=state.root) return false;
    int kind=kind_of(state.root);
    if (kind==6) {
        int w=(int)Read32(state.root,0xDC),h=(int)Read32(state.root,0xE0);
        if (w<=0 || h<=0) return false;
        point->x=(int)Read32(state.root,0xD4)+w-6;point->y=(int)Read32(state.root,0xD8)+h-6;return true;
    }
    if (kind==3) {
        if (!load_rows(state.root)) return false;
        void *resource=ReadPtr(state.root,0x50);
        int x=((This2)g_profile->template_value)(resource,5,1);
        int y=((This2)g_profile->template_value)(resource,6,1);
        int w=((This2)g_profile->template_value)(resource,7,1);
        int h=((This2)g_profile->template_value)(resource,8,1);
        if (w<=0 || h<=0 || Read32(state.root,0xD4)>=4) return false;
        point->x=x+w-6;point->y=y+h*((int)Read32(state.root,0xD4)+1)-6;return true;
    }
    MenuButton list[64];unsigned count=buttons(state.root,kind,list);
    for (unsigned i=0;i<count;++i) if (list[i].id==state.id) {
        point->x=(LONG)Read32(list[i].object,0x14)+(LONG)Read32(list[i].object,0x1C)-6;
        point->y=(LONG)Read32(list[i].object,0x18)+(LONG)Read32(list[i].object,0x20)-6;
        return true;
    }
    return false;
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
        clear_focus();state.root=root;state.id=kind==5 ? 0xFFFFFFFFu:0;state.direction=0;
        state.owned=true;state.barrier=true;frame_captured=true;
        Log_Write("[菜单路由] 页面=%02lX 类型=%d 来源=手柄；等待旧输入释放。",(unsigned long)Read32(root,0x28),kind);
    }
    if (state.barrier) {
        /* 进入/离开页面和接管时先吸收旧操作；中立帧本身也不执行按钮。 */
        if (neutral()) state.barrier=false;
    }
    if (kind==6) {
        /* A只加速原滚动，效果在原Tick之后施加；不人工结束或跳到下一句。
         * B仍对应原Esc收起语义，原自动滚完后的生命周期继续由游戏完成。 */
        if (!state.barrier && (g_intent.pressed & KEY(PAD_B))) {
            ((This0)g_profile->menu_text_next)(root);state.barrier=true;
        }
        return;
    }
    if (kind==3 && Read32(root,0x64)) { load_update(root);return; }
    MenuButton list[64];unsigned count=kind>=0 ? buttons(root,kind,list):0;
    if (!count || !Read32(root,0x64)) return;
    unsigned selected=0;bool found=false;
    for (unsigned i=0;i<count;++i) if (list[i].id==state.id) {selected=i;found=true;break;}
    if (!found) {
        /* 默认项用业务ID而非鼠标位置，危险按钮不会因为光标停留而成为默认选择。 */
        unsigned preferred=kind==0 ? 0x22:kind==1 ? 0x2E:kind==4 ? 0x200:0x9A;
        for (unsigned i=0;i<count;++i) if (list[i].id==preferred) selected=i;
        state.id=list[selected].id;focus_sound(kind);
    }
    project();
    /* 本体标题激活动画进行时不移动焦点、不改动画、不重复启动业务。 */
    if (state.barrier || neutral() || (kind==0 && (int)Read32(root,0xC0)!=-1)) {
        state.direction=0;return;
    }
    int dir=direction();
    if (dir && (dir!=state.direction || (int32_t)(g_input.now-state.next_repeat)>=0)) {
        selected=navigate(list,count,selected,dir);
        if (state.id!=list[selected].id) focus_sound(kind);
        state.id=list[selected].id;
        state.next_repeat=g_input.now+(dir==state.direction ? 110u:350u);
        Log_Write("[菜单焦点] 页面=%02lX 控件=%02X。",(unsigned long)Read32(root,0x28),state.id);
        project();
    }
    state.direction=dir;
    /* B优先于同帧A，避免同时按下时既确认又关闭。标题根没有可靠的返回父页，因此B留空。 */
    bool cancel=(g_intent.pressed & KEY(PAD_B))!=0 || g_intent.menu_toggle;
    if (cancel) {
        if (kind==5) {
            ((This3)g_profile->menu_talk_cancel)(root,0,0,NULL);state.barrier=true;
        } else if (kind==4) {
            for (unsigned i=0;i<count;++i) if (list[i].id==0x200) {
                Write32(root,0xA8,(uint32_t)(uintptr_t)list[i].object);
                ((This3)g_profile->menu_message_primary)(root,0,0,NULL);break;
            }
            state.barrier=true;
        } else if (kind!=0) { hide(root);state.barrier=true; }
        return;
    }
    if (!(g_intent.pressed & KEY(PAD_A))) return;
    Log_Write("[菜单操作] 页面=%02lX 控件=%02X 确认。",(unsigned long)Read32(root,0x28),state.id);
    if (kind==5) {
        /* 2C4是鼠标按下/释放配对标记，不是业务ready；手柄语义不能要求先伪造鼠标按下。
         * 真正保留的是原显示时间防穿透和外传输入冷却，随后提交独立选项。 */
        bool delayed=g_profile->menu_talk_delay_global && Read32((void *)g_profile->menu_talk_delay_global,0)>0;
        uint32_t delta=Read32((void *)g_profile->game_tick,0)-Read32(root,0x74);
        bool recent=(int32_t)delta>=-10 && (int32_t)delta<=10;
        if (!recent && !delayed) {
            project();((This0)g_profile->menu_talk_select)(root);Menu_Suspend();
        } else Log_Write("[对话] 原显示保护期或外传输入冷却内，本次不提交。");
    } else if (kind==4) {
        project();((This3)g_profile->menu_message_primary)(root,0,0,NULL);
    } else if (kind==0) ((This1)g_profile->menu_title_activate)(root,(int)state.id);
    else if (kind==1) {
        /* 原系统事件仅消费root+A8当前按钮，三个栈参数未使用；传空参数，不造鼠标对象。 */
        project();((This3)g_profile->menu_system_primary)(root,0,0,NULL);
    } else if (state.id==0x9B) hide(root);
    else ((This0)g_profile->menu_confirm_submit)(root);
    /* 即使业务拒绝/原地停留，也必须松开后才能再确认；一次按下只提交一次。 */
    state.barrier=true;
}

static int tick(void *self,unsigned kind)
{
    /* 删除确认原Tick先做GetCursorPos命中再调base Tick。手柄只替换命中生产部分，
     * base仍完整更新显示/子控件，随后用自己的标签焦点决定原文字高亮。 */
    int result=kind==4 && owns() && kind_of(self)==4 ?
        ((This0)g_profile->menu_message_base_tick)(self):((This0)original[kind][0])(self);
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
    if (self==state.root || active) Menu_Suspend();
    return ((This2)original[kind][1])(self,active,mode);
}
static int hover(void *self,unsigned kind,int event,int x,void *y)
{
    if (owns()) {
        /* 手柄拥有菜单时不执行GetCursorPos/picker；自己的焦点可以在光标移到页面外仍保持。 */
        if (self==state.root) project();
        return 0;
    }
    return ((This3)original[kind][2])(self,event,x,y);
}
/* fastcall的第二寄存器参数是占位；后面的栈参数数量与原thiscall严格一致。 */
#define MENU_WRAPPERS(n) static int __attribute__((fastcall)) tick##n(void *s,void *unused) { (void)unused;return tick(s,n); } static int __attribute__((fastcall)) show##n(void *s,void *unused,int active,int mode) { (void)unused;return show(s,n,active,mode); } static int __attribute__((fastcall)) hover##n(void *s,void *unused,int e,int x,void *y) { (void)unused;return hover(s,n,e,x,y); }
MENU_WRAPPERS(0)
MENU_WRAPPERS(1)
MENU_WRAPPERS(2)
MENU_WRAPPERS(3)
MENU_WRAPPERS(4)
MENU_WRAPPERS(5)
MENU_WRAPPERS(6)
static uintptr_t replacement[7][3];
static BYTE saved_talk_picker[5];
static bool picker_installed;
static int __attribute__((fastcall)) talk_picker_hook(void *root,void *unused)
{
    (void)unused;
    if (owns() && kind_of(root)==5) {
        MenuButton list[64];unsigned n=buttons(root,5,list);
        for (unsigned i=0;i<n;++i) if (state.root==root && list[i].id==state.id) return (int)(uintptr_t)list[i].object;
        return 0;
    }
    return ((This0)g_profile->menu_talk_picker)(root);
}
void Menu_Shutdown(void)
{
    Menu_Suspend();
    for (unsigned i=0;i<7;++i) for (unsigned j=0;j<3;++j) {
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
    installed=false;frame_captured=false;memset(&state,0,sizeof state);
}
bool Menu_Initialize(void)
{
    if (installed) return true;
    tables[0]=g_profile->menu_title_vtable;tables[1]=g_profile->menu_system_vtable;
    tables[2]=g_profile->menu_confirm_vtable;tables[3]=g_profile->menu_load_vtable;tables[4]=g_profile->menu_message_vtable;
    tables[5]=g_profile->menu_talk_vtable;tables[6]=g_profile->menu_text_vtable;
    uintptr_t expected[7][3]={
        {g_profile->menu_title_tick,g_profile->menu_title_show,g_profile->menu_title_hover},
        {g_profile->menu_system_tick,g_profile->menu_system_show,g_profile->menu_system_hover},
        {g_profile->menu_confirm_tick,g_profile->menu_confirm_show,g_profile->menu_confirm_hover},
        {g_profile->menu_load_tick,g_profile->menu_load_show,g_profile->menu_load_hover},
        {g_profile->menu_message_tick,g_profile->menu_message_show,g_profile->menu_message_hover},
        {g_profile->menu_talk_tick,g_profile->menu_talk_show,g_profile->menu_talk_hover},
        {g_profile->menu_text_tick,g_profile->menu_text_show,g_profile->menu_text_hover}};
    uintptr_t hooks[7][3]={{(uintptr_t)tick0,(uintptr_t)show0,(uintptr_t)hover0},
        {(uintptr_t)tick1,(uintptr_t)show1,(uintptr_t)hover1},
        {(uintptr_t)tick2,(uintptr_t)show2,(uintptr_t)hover2},
        {(uintptr_t)tick3,(uintptr_t)show3,(uintptr_t)hover3},
        {(uintptr_t)tick4,(uintptr_t)show4,(uintptr_t)hover4},
        {(uintptr_t)tick5,(uintptr_t)show5,(uintptr_t)hover5},
        {(uintptr_t)tick6,(uintptr_t)show6,(uintptr_t)hover6}};
    memcpy(replacement,hooks,sizeof hooks);
    /* 全部槽先验证再写，拒绝被其它补丁替换的入口；不会占用DisplayFix的Draw/picker。 */
    for (unsigned i=0;i<7;++i) for (unsigned j=0;j<3;++j) {
        if (!tables[i] || !expected[i][j] || Read32((void *)tables[i],offsets[j])!=expected[i][j]) return false;
        original[i][j]=expected[i][j];
    }
    /* 业务槽也核对；只对本批精确类调用原入口，不能猜未知类的虚表+3C。 */
    if (!g_profile->menu_system_primary || !g_profile->menu_confirm_submit ||
        Read32((void *)tables[1],0x24)!=g_profile->menu_system_primary ||
        Read32((void *)tables[2],0x3C)!=g_profile->menu_confirm_submit ||
        !g_profile->menu_load_primary || Read32((void *)tables[3],0x24)!=g_profile->menu_load_primary ||
        !g_profile->menu_message_primary || Read32((void *)tables[4],0x24)!=g_profile->menu_message_primary) return false;
    if (!HookManager_Claim(SHARED_HOOK_CONTROLLER_MENU,RUNTIME_MODULE_CONTROLLER)) return false;
    for (unsigned i=0;i<7;++i) for (unsigned j=0;j<3;++j) {
        if (!Memory_Patch((void *)(tables[i]+offsets[j]),&replacement[i][j],4)) {
            Menu_Shutdown();Log_Write("[菜单][停止] 焦点入口安装失败，已撤回本批菜单槽。");return false;
        }
    }
    if (!Memory_Readable((void *)g_profile->menu_talk_picker_call,5)) {Menu_Shutdown();return false;}
    memcpy(saved_talk_picker,(void *)g_profile->menu_talk_picker_call,5);int32_t displacement;
    memcpy(&displacement,saved_talk_picker+1,4);
    if (saved_talk_picker[0]!=0xE8 || g_profile->menu_talk_picker_call+5+displacement!=g_profile->menu_talk_picker) {Menu_Shutdown();return false;}
    BYTE picker_bytes[5]={0xE8};displacement=(int32_t)((uintptr_t)talk_picker_hook-g_profile->menu_talk_picker_call-5);
    memcpy(picker_bytes+1,&displacement,4);picker_installed=Memory_Patch((void *)g_profile->menu_talk_picker_call,picker_bytes,5);
    if (!picker_installed) {Menu_Shutdown();return false;}
    installed=true;state.barrier=true;
    Log_Write("[菜单] 标题/读档/系统/资金确认取消原生入口已安装；十字键/左摇杆导航，A确认，B返回。");
    return true;
}
