#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/Modules/QOL/PickupNotice.c"
/* 只在自有32位宿主里把原生函数换成替身，不加载游戏、不挂接游戏进程。 */
static unsigned checks,draws;static unsigned long clock_ms;
static unsigned char world_data[0x80],manager_data[0x80],role_data[0x300],bag_data[0x400];
static unsigned char item_data[0x40],ground_data[0xA0],definition_data[0x20],hud_data[0x100];
static unsigned long world_pointer,hud_pointer,columns[4];static char item_name[200];
static RuntimeEventCallback draw_callback;
#define CHECK(e) do {++checks;if(!(e)){fprintf(stderr,"拾取提示检查失败 行%d：%s\n",__LINE__,#e);exit(1);}}while(0)
static void put(void *base,unsigned offset,unsigned long value){memcpy((unsigned char *)base+offset,&value,4);}
int RuntimeWin32_Read(unsigned long address,void *out,unsigned long size)
{
    MEMORY_BASIC_INFORMATION info;
    if(!address || !VirtualQuery((void *)address,&info,sizeof info) || info.State!=MEM_COMMIT ||
       (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) || address+size>(unsigned long)info.BaseAddress+info.RegionSize) return 0;
    memcpy(out,(void *)address,size);return 1;
}
int RuntimeWin32_IsReadable(unsigned long address,unsigned long size)
{ unsigned char byte;return RuntimeWin32_Read(address,&byte,1) && RuntimeWin32_Read(address+size-1,&byte,1); }
unsigned long RuntimeWin32_TickCount(void){return clock_ms;}
void RuntimeWin32_Log(void *module,const char *text){(void)module;(void)text;}
void RuntimeWin32_LogNumber(void *module,const char *label,unsigned long value){(void)module;(void)label;CHECK(value>0);}
int Runtime_Subscribe(RuntimeEventId event,RuntimeEventCallback callback,void *user)
{ CHECK(event==RUNTIME_EVENT_UI_DRAW_END && !user);draw_callback=callback;return 1; }
static int __fastcall get_player(void *manager,void *unused)
{ CHECK(manager==manager_data);(void)unused;return (int)role_data; }
static int __fastcall get_bag(void *root,void *unused)
{ (void)root;(void)unused;return (int)bag_data; }
static int __fastcall get_item(void *bag,void *unused,int index)
{ CHECK(bag==bag_data && index>=0 && index<136);(void)unused;return index==0 ? (int)item_data:0; }
static int __fastcall get_name(void *record,void *unused,int column)
{ CHECK(record==definition_data && column==0);(void)unused;return (int)item_name; }
static int __fastcall draw_text(void *hud,void *unused,unsigned long context,const char *text,int x,int y,int mode)
{
    CHECK(hud==hud_data && context==123 && x==16 && y>=64 && y<=174 && mode==0);(void)unused;
    CHECK((unsigned char)text[0]==0xCA && strlen(text)<176);++draws;return 1;
}
static void emit_draw(void){draw_callback(RUNTIME_EVENT_UI_DRAW_END,NULL,123,0,NULL);}
int main(void)
{
    NoticeProfile profile={0};
    profile.string_get=(unsigned long)get_name;profile.text_draw=(unsigned long)draw_text;
    profile.inventory_get=(unsigned long)get_bag;profile.item_at=(unsigned long)get_item;
    profile.player_get=(unsigned long)get_player;profile.world_global=(unsigned long)&world_pointer;
    profile.hud_global=(unsigned long)&hud_pointer;g_profile=&profile;g_enabled=1;
    Runtime_Subscribe(RUNTIME_EVENT_UI_DRAW_END,draw_notices,NULL);
    world_pointer=(unsigned long)world_data;hud_pointer=(unsigned long)hud_data;
    put(world_data,0x30,(unsigned long)manager_data);put(ground_data,0x81,(unsigned long)definition_data);
    put(definition_data,4,4);put(definition_data,8,(unsigned long)columns);columns[1]=42;columns[2]=10;
    put(item_data,0x18,42);put(item_data,0x1C,2);strcpy(item_name,"item");
    PickupNoticeSnapshot snapshot;
    PickupNotice_Before((unsigned long)ground_data,(unsigned long)role_data,&snapshot);CHECK(snapshot.valid);
    PickupNotice_After(&snapshot);CHECK(notice_count==0); /* 未入包或背包满不误报。 */
    put(item_data,0x1C,5);PickupNotice_After(&snapshot);CHECK(notice_count==1);
    CHECK(strstr(notices[0].text,"item x3")!=NULL);emit_draw();CHECK(draws==1);
    columns[2]=0;put(bag_data,0x20,100);
    PickupNotice_Before((unsigned long)ground_data,(unsigned long)role_data,&snapshot);
    put(bag_data,0x20,125);PickupNotice_After(&snapshot);CHECK(notice_count==2);
    CHECK(strstr(notices[1].text,"x25")!=NULL);
    PickupNotice_Before((unsigned long)ground_data,(unsigned long)item_data,&snapshot);CHECK(!snapshot.valid);
    /* 名字在原版删除地面对象之前已经复制，之后不再读取已销毁的地面对象。 */
    PickupNotice_Before((unsigned long)ground_data,(unsigned long)role_data,&snapshot);
    memset(ground_data,0,sizeof ground_data);put(bag_data,0x20,126);PickupNotice_After(&snapshot);CHECK(notice_count==3);
    for(unsigned n=0;n<10;++n){snapshot.before=126+n;put(bag_data,0x20,127+n);PickupNotice_After(&snapshot);}
    CHECK(notice_count==6);draws=0;emit_draw();CHECK(draws==6);
    clock_ms=5000;draws=0;emit_draw();CHECK(draws==0 && notice_count==0);
    clock_ms=0xFFFFFFF0ul;snapshot.before=136;put(bag_data,0x20,137);PickupNotice_After(&snapshot);
    clock_ms=10;emit_draw();CHECK(notice_count==1); /* 无符号时差跨回绕仍未过期。 */
    world_pointer=(unsigned long)hud_data;emit_draw();CHECK(notice_count==0);world_pointer=(unsigned long)world_data;
    put(ground_data,0x81,(unsigned long)definition_data);
    for(unsigned n=0;n<160;n+=2){item_name[n]=(char)0xB2;item_name[n+1]=(char)0xE2;}item_name[160]=0;
    PickupNotice_Before((unsigned long)ground_data,(unsigned long)role_data,&snapshot);
    CHECK(snapshot.valid && strlen(snapshot.name)==126); /* GBK半字节截断被拒绝。 */
    PickupNotice_Disable();snapshot.before=137;put(bag_data,0x20,138);PickupNotice_After(&snapshot);CHECK(notice_count==0);
    printf("拾取成功判定、文字绘制、过期/切图/有界队列检查通过：%u 项\n",checks);return 0;
}
