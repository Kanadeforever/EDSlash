#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/Modules/QOL/AutoPickup.c"
/* 调用生产分帧扫描，内存来自宿主数组，绝不读写游戏进程。 */
static unsigned checks,calls,chunk_reads,header_reads;static unsigned long clock_ms;
#define SLOT_BYTES (32768u*6u)
static unsigned char *slots,*second_slots;
static unsigned long action_data[2][2],manager_pointer,table_pointer;
static unsigned long frame_bytes;
#define CHECK(e) do {++checks;if(!(e)){fprintf(stderr,"分帧拾取检查失败 行%d：%s\n",__LINE__,#e);exit(1);}}while(0)
int RuntimeWin32_Read(unsigned long address,void *out,unsigned long size)
{
    MEMORY_BASIC_INFORMATION info={0};
    if(!address || !VirtualQuery((void *)address,&info,sizeof info) || info.State!=MEM_COMMIT ||
       (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) || address+size>(unsigned long)info.BaseAddress+info.RegionSize) return 0;
    if (address>=table_pointer && address<table_pointer+SLOT_BYTES) {frame_bytes+=size;if(size>6)++chunk_reads;}
    if (address==(unsigned long)action_data[0] || address==(unsigned long)action_data[1]) ++header_reads;
    memcpy(out,(void *)address,size);return 1;
}
int RuntimeWin32_Query(unsigned long address,RuntimeMemoryRegion *region)
{CHECK(address==table_pointer);*region=(RuntimeMemoryRegion){table_pointer,SLOT_BYTES,MEM_COMMIT,PAGE_READWRITE};return 1;}
unsigned long RuntimeWin32_TickCount(void){return clock_ms;}
void RuntimeWin32_Log(void *module,const char *text){(void)module;(void)text;}
int64_t RuntimePerf_Begin(void){return 0;}
void RuntimePerf_End(RuntimePerfId id,int64_t begin){CHECK(id==PERF_PICKUP && !begin);}
int ItemClassifier_IsGroundItem(unsigned long object){return object==1;}
int GroundItems_IsReadyForAutomaticPickup(unsigned long object){return object==1;}
int ItemClassifier_GetPickupClass(unsigned long object,PickupItemClass *type){CHECK(object==1);*type=PICKUP_ITEM_MONEY;return 1;}
static int __fastcall pickup(void *action,void *unused,int opcode,int a,int b,int c,int d,int e)
{
    (void)unused;CHECK(action==action_data[0] && opcode==22 && !a && !b && !c && d==1 && e==1);
    CHECK(g_native_pickup_scan_active && AutoPickup_AllowPickupCandidate(1) && !AutoPickup_AllowPickupCandidate(2));
    ++calls;return 1;
}
static void put_slot(unsigned at,unsigned object,unsigned generation)
{
    unsigned char *slot=(unsigned char *)table_pointer+at*6;slot[1]=(unsigned char)generation;
    unsigned long ptr=(unsigned long)action_data[object];memcpy(slot+2,&ptr,4);
}
static void frame(void)
{frame_bytes=0;clock_ms+=16;AutoPickup_AfterInputFrame();CHECK(frame_bytes<=6144+6);CHECK(AutoPickup_AllowPickupCandidate(2));}
int main(void)
{
    GameProfile profile={0};RuntimeContext context={.profile=&profile};
    profile.qol.ground_manager_global_rva=(unsigned long)&manager_pointer-GAME_IMAGE_BASE;
    profile.qol.action_slot_table_global_rva=(unsigned long)&table_pointer-GAME_IMAGE_BASE;
    profile.qol.pickup_action_vtable_rva=0x1234;profile.qol.action_entry_rva=(unsigned long)pickup-GAME_IMAGE_BASE;
    slots=VirtualAlloc(NULL,SLOT_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    second_slots=VirtualAlloc(NULL,SLOT_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(slots && second_slots);
    manager_pointer=123;table_pointer=(unsigned long)slots;
    for(unsigned i=0;i<2;++i){action_data[i][0]=GAME_IMAGE_BASE+0x1234;action_data[i][1]=123;}
    put_slot(32767,0,2);CHECK(AutoPickup_Initialize(&context,AUTO_PICKUP_POLICY_MONEY,0));
    for(unsigned i=0;i<31;++i){frame();CHECK(calls==0);}
    frame();CHECK(calls==1 && chunk_reads==32 && !search_active);
    unsigned chunks=chunk_reads;frame();CHECK(calls==2 && chunk_reads==chunks); /* 已绑定只验登记槽 */
    /* 同地址、同vtable却登记代数改变必须重新扫描，不直接调用旧缓存。 */
    slots[32767*6+1]=3;for(unsigned i=0;i<31;++i){frame();CHECK(calls==2);}
    frame();CHECK(calls==3);
    memset(slots,0,SLOT_BYTES);put_slot(1,0,0);put_slot(2048,1,0);
    for(unsigned i=0;i<3;++i) {frame();}
    CHECK(calls==3 && !search_active); /* 重复候选拒绝 */
    /* 登记表切换后不能沿用上一张表的进度或绑定。 */
    table_pointer=(unsigned long)second_slots;put_slot(5000,0,1);
    for(unsigned i=0;i<31;++i){frame();CHECK(calls==3);}frame();CHECK(calls==4);
    AutoPickup_Disable();chunks=chunk_reads;frame();CHECK(calls==4 && chunk_reads==chunks);
    AutoPickup_ApplySettings(AUTO_PICKUP_POLICY_MONEY,250);frame();CHECK(calls==4);
    clock_ms+=250;frame();CHECK(calls==5);
    manager_pointer=456;frame();CHECK(calls==5); /* 原对象归属不符不可用 */
    VirtualFree(slots,0,MEM_RELEASE);VirtualFree(second_slots,0,MEM_RELEASE);
    printf("分帧拾取预算、唯一性、登记生命周期与过滤回放通过：%u项\n",checks);return 0;
}
