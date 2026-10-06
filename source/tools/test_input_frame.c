#include "../src/Runtime/Perf.h"
#include <stdio.h>
#include <string.h>
#include "../src/Runtime/InputFrame.c"
/* 使用真实输入提供者，替身只模拟物理安装与原函数，验证参数、阶段顺序和失败隔离。 */
static unsigned checks;
static int order[8],count,claimed,install_ok=1,bad_signature,controller_ready,last_idle;
#define CHECK(test) do {++checks;if(!(test)){printf("公共输入帧检查失败，第%d行\n",__LINE__);return 1;}}while(0)
static int __fastcall native_input(unsigned char *keys,void *unused)
{(void)unused;order[count++]=2;keys[0]=42;return 7;}
int RuntimeWin32_IsReady(void) {return 1;}
int RuntimeWin32_Read(unsigned long address,void *output,unsigned long size)
{
    if (address!=0x400100ul || size!=5) return 0;
    unsigned char bytes[5]={0x33,0xC0,0x8D,0x51,0x08};
    if(bad_signature)bytes[0]=0x90;
    memcpy(output,bytes,5);return 1;
}
int HookManager_Claim(SharedHookId hook,RuntimeModuleId owner)
{claimed=(hook==SHARED_HOOK_INPUT_FRAME && owner==RUNTIME_MODULE_INPUT);return claimed;}
void HookManager_ReleaseOwned(RuntimeModuleId owner)
{if(owner==RUNTIME_MODULE_INPUT)claimed=0;}
int X86Detour_Install(X86Detour *d,unsigned long target,unsigned long replacement,unsigned long size)
{
    if (!install_ok || target!=0x400100ul || !replacement || replacement==5 || size!=5) return 0;
    d->gateway=(unsigned long)native_input;return 1;
}
int ModuleRegistry_IsInitialized(RuntimeModuleId id) {(void)id;return 1;}
int ControllerModule_IsReady(void) {return controller_ready;}
int RuntimeConfig_ApplyFrame(int idle) {last_idle=idle;order[count++]=0;return 1;}
void Runtime_EmitEvent(RuntimeEventId event,void *subject,unsigned long v1,unsigned long v2)
{(void)subject;(void)v1;(void)v2;order[count++]=event==RUNTIME_EVENT_INPUT_FRAME_BEGIN ? 1:3;}
int main(void)
{
    GameProfile profile={.qol={.input_update_rva=0x100}};
    RuntimeContext context={.profile=&profile};unsigned char keys[256]={0};
    CHECK(!RuntimeInput_Initialize(NULL));
    bad_signature=1;CHECK(!RuntimeInput_Initialize(&context) && !claimed);
    bad_signature=0;install_ok=0;CHECK(!RuntimeInput_Initialize(&context) && !claimed);
    install_ok=1;CHECK(RuntimeInput_Initialize(&context) && claimed);
    CHECK(input_frame(keys,NULL)==7 && keys[0]==42);
    CHECK(count==4 && order[0]==0 && order[1]==1 && order[2]==2 && order[3]==3);
    CHECK(last_idle==1); /* 登记成功但尚未就绪，公共提供者仍接收候选。 */
    count=0;controller_ready=1;CHECK(input_frame(keys,NULL)==7 && last_idle==0);
    count=0;controller_ready=0;CHECK(input_frame(keys,NULL)==7 && last_idle==1);
    printf("公共输入提供者、Detour参数、阶段顺序与安装失败检查通过：%u项\n",checks);
    return 0;
}

// 性能替身不改变生产业务或原有测试顺序。
void RuntimePerf_Initialize(void) {}
int64_t RuntimePerf_Begin(void) {return 0;}
void RuntimePerf_End(RuntimePerfId id,int64_t begin) {(void)id;(void)begin;}
void RuntimePerf_FrameEnd(void) {}

int RuntimeLog_Start(void) {return 1;}
