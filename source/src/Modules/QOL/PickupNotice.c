#include "PickupNotice.h"
#include "../../Runtime/Win32Bridge.h"

/* 所有入口均在两份非Steam基线逐条核对。最后三项是已有管理器全局，读取不会改写游戏。 */
typedef struct NoticeProfile {
    unsigned long string_get,text_draw,inventory_get,item_at,player_get;
    unsigned long inventory_root,world_global,hud_global;
    unsigned char signatures[5][12];
} NoticeProfile;
static const NoticeProfile profiles[2]={
    {0x004D00B0ul,0x004B1DF0ul,0x004892D0ul,0x0047F320ul,0x00477790ul,0x005585C0ul,0x005585C4ul,0x0055BBB0ul, {{0x8B,0x44,0x24,0x04,0x8B,0x51,0x04,0x3B,0xC2,0x7D,0x16,0x85},{0x6A,0xFF,0x68,0x18,0x54,0x52,0x00,0x64,0xA1,0x00,0x00,0x00},{0xA1,0xC4,0x85,0x55,0x00,0x85,0xC0,0x74,0x1B,0x8B,0x48,0x30},{0x8B,0x44,0x24,0x04,0x8B,0x84,0x81,0xA4,0x00,0x00,0x00,0x83},{0x8B,0x41,0x0C,0x50,0xE8,0xB7,0x95,0xFA,0xFF,0x83,0xC4,0x04}}},
    {0x004E4D90ul,0x004C5140ul,0x004987F0ul,0x0048E130ul,0x004861A0ul,0x00589540ul,0x00589544ul,0x0058D164ul, {{0x8B,0x44,0x24,0x04,0x8B,0x51,0x04,0x3B,0xC2,0x7D,0x16,0x85},{0x6A,0xFF,0x68,0x68,0xC8,0x54,0x00,0x64,0xA1,0x00,0x00,0x00},{0xA1,0x44,0x95,0x58,0x00,0x85,0xC0,0x74,0x1B,0x8B,0x48,0x30},{0x8B,0x44,0x24,0x04,0x8B,0x84,0x81,0xA4,0x00,0x00,0x00,0x83},{0x8B,0x41,0x0C,0x50,0xE8,0x97,0x31,0xFA,0xFF,0x83,0xC4,0x04}}}
};
typedef int (__fastcall *Native0)(void *,void *);
typedef int (__fastcall *Native1)(void *,void *,int);
typedef int (__fastcall *NativeText)(void *,void *,unsigned long,const char *,int,int,int);
static const NoticeProfile *g_profile;
static void *g_self_module;
static int g_enabled;
/* 有界队列不分配游戏内存，避免连捡大量金币时挤爆提示，也不把缓存字符串交给游戏长期持有。 */
static struct { unsigned long world,collector,created;char text[176]; } notices[6];
static unsigned long notice_count;
/* freestanding主插件没有CRT；按字节复制有界提示，避免编译器为结构赋值引入memcpy导入。 */
static void copy_notice(unsigned long to,unsigned long from)
{
    volatile unsigned char *target=(volatile unsigned char *)&notices[to];
    const volatile unsigned char *source=(const volatile unsigned char *)&notices[from];
    for (unsigned long i=0ul;i<sizeof notices[0];++i) target[i]=source[i];
}

static unsigned long read32(unsigned long address)
{ unsigned long value=0ul;RuntimeWin32_Read(address,&value,4ul);return value; }
static Native0 native0(unsigned long address)
{ union { unsigned long address;Native0 fn; } value;value.address=address;return value.fn; }
static Native1 native1(unsigned long address)
{ union { unsigned long address;Native1 fn; } value;value.address=address;return value.fn; }
static NativeText native_text(unsigned long address)
{ union { unsigned long address;NativeText fn; } value;value.address=address;return value.fn; }
static unsigned long player(void)
{
    unsigned long world=read32(g_profile->world_global),manager=read32(world+0x30ul);
    return manager ? (unsigned long)native0(g_profile->player_get)((void *)manager,(void *)0):0ul;
}
static unsigned long inventory(void)
{ return (unsigned long)native0(g_profile->inventory_get)((void *)g_profile->inventory_root,(void *)0); }

static int total(unsigned long bag,unsigned long id,unsigned long *value)
{
    unsigned long sum=0ul;
    if (!bag || !value) return 0;
    /* 背包逻辑槽共136个；新占槽与原堆叠数量增长都能被同一个总量比较识别。 */
    for (int i=0;i<136;++i) {
        unsigned long item=(unsigned long)native1(g_profile->item_at)((void *)bag,(void *)0,i);
        if (!item) continue;
        if (!RuntimeWin32_IsReadable(item,0x24ul)) return 0;
        if (read32(item+0x18ul)==id) {
            unsigned long count=read32(item+0x1Cul);
            if (count>0x7FFFFFFFul || sum>0x7FFFFFFFul-count) return 0;
            sum+=count;
        }
    }
    *value=sum;return 1;
}

void PickupNotice_Before(unsigned long ground,unsigned long collector,PickupNoticeSnapshot *out)
{
    if (!out) return;
    out->valid=0;
    if (!g_enabled || !ground || collector!=player()) return;
    unsigned long definition=read32(ground+0x81ul),columns=read32(definition+4ul),values=read32(definition+8ul);
    if (!definition || columns<3ul || columns>4096ul || !values) return;
    out->inventory=inventory();out->world=read32(g_profile->world_global);out->collector=collector;
    if (!out->inventory) return;
    out->item_id=read32(values+4ul);
    unsigned long type=read32(values+8ul);out->money=type<=9ul;
    if (out->money) out->before=read32(out->inventory+0x20ul);
    else if (!total(out->inventory,out->item_id,&out->before)) return;
    unsigned long name=(unsigned long)native1(g_profile->string_get)((void *)definition,(void *)0,0);
    unsigned long n=0ul;
    for (;n<127ul;++n) {
        char c=0;
        if (!name || !RuntimeWin32_Read(name+n,&c,1ul)) return;
        out->name[n]=c;if (!c) break;
    }
    /* GBK字符可能是两个字节。截断时只保留完整字符，不能让残半字节破坏原版字体读取。 */
    if (n==127ul) {
        unsigned long complete=0ul;
        while (complete<n) {
            unsigned long size=(unsigned char)out->name[complete]>=0x81u ? 2ul:1ul;
            if (complete+size>n) break;
            complete+=size;
        }
        n=complete;out->name[n]=0;
    }
    if (!n) return;
    out->valid=1;
}

void PickupNotice_After(const PickupNoticeSnapshot *before)
{
    if (!g_enabled || !before || !before->valid || before->collector!=player() ||
        before->world!=read32(g_profile->world_global) || before->inventory!=inventory()) return;
    unsigned long after=0ul;
    if (before->money) after=read32(before->inventory+0x20ul);
    else if (!total(before->inventory,before->item_id,&after)) return;
    if (after<=before->before) return;
    /* 原入口始终返回1，实际库存增量才是真成功；背包满、过滤、脚本未入包都不会误报。 */
    if (notice_count==6ul) {
        for (unsigned long i=1ul;i<6ul;++i) copy_notice(i-1ul,i);
        --notice_count;
    }
    unsigned long index=notice_count++,n=0ul;
    notices[index].world=before->world;notices[index].collector=before->collector;
    notices[index].created=RuntimeWin32_TickCount();
    /* 原版字体接收GBK，以下字节是“拾取：”，源码文件本身仍为UTF-8。 */
    const unsigned char prefix[]={0xCAu,0xB0u,0xC8u,0xA1u,0xA3u,0xBAu};
    for (unsigned long i=0ul;i<sizeof prefix;++i) notices[index].text[n++]=(char)prefix[i];
    for (unsigned long i=0ul;before->name[i] && i<127ul;++i) notices[index].text[n++]=before->name[i];
    notices[index].text[n++]=' ';notices[index].text[n++]='x';
    char digits[10];unsigned long used=0ul,value=after-before->before;
    do {digits[used++]=(char)('0'+value%10ul);value/=10ul;} while(value && used<10ul);
    while(used) notices[index].text[n++]=digits[--used];
    notices[index].text[n]=0;
    RuntimeWin32_LogNumber(g_self_module,"[QoL][拾取提示] 实际新增数量=",after-before->before);
}

static void draw_notices(RuntimeEventId event,void *subject,unsigned long context,unsigned long unused,void *user)
{
    (void)subject;(void)unused;(void)user;
    if (!g_enabled || event!=RUNTIME_EVENT_UI_DRAW_END || !context) return;
    unsigned long now=RuntimeWin32_TickCount(),world=read32(g_profile->world_global),role=player();
    unsigned long keep=0ul;
    for (unsigned long i=0ul;i<notice_count;++i) {
        if (notices[i].world==world && notices[i].collector==role && now-notices[i].created<5000ul)
            copy_notice(keep++,i);
    }
    notice_count=keep;
    unsigned long hud=read32(g_profile->hud_global);
    if (!hud || !RuntimeWin32_IsReadable(hud,0x64ul)) return;
    /* 复用Runtime绘制结束事件和原版字体；不抢DisplayFix的物理Draw入口。 */
    for (unsigned long i=0ul;i<notice_count;++i)
        native_text(g_profile->text_draw)((void *)hud,(void *)0,context,notices[i].text,16,64+(int)i*22,0);
}
int PickupNotice_Initialize(const RuntimeContext *runtime)
{
    if (!runtime || !runtime->profile) return 0;
    if (runtime->profile->game_id==GAME_ID_DAOJIAN) g_profile=&profiles[0];
    else if(runtime->profile->game_id==GAME_ID_WAIZHUAN) g_profile=&profiles[1];
    else return 0;
    unsigned long addresses[5]={g_profile->string_get,g_profile->text_draw,g_profile->inventory_get,
                               g_profile->item_at,g_profile->player_get};
    for (unsigned long i=0ul;i<5ul;++i) {
        unsigned char actual[12];
        if (!RuntimeWin32_Read(addresses[i],actual,12ul)) return 0;
        for (unsigned long j=0ul;j<12ul;++j) if(actual[j]!=g_profile->signatures[i][j]) return 0;
    }
    if (!Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_END,draw_notices,(void *)0)) return 0;
    g_self_module=runtime->self_module;notice_count=0ul;g_enabled=1;
    RuntimeWin32_Log(g_self_module,"[QoL] 成功拾取提示已接入原版字体和公共UI绘制事件。");return 1;
}
void PickupNotice_Disable(void) { g_enabled=0;notice_count=0ul; }
