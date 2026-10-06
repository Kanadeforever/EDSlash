/* 在独立进程调用真实共享管理器，检查模块身份边界。
 * 不链接游戏、Runtime启动器或业务模块，也不安装任何游戏钩子。
 * 返回1表示发现接口拒绝规则缺口，不能把此探针运行成功误写为全部检查通过。 */
#include <stdio.h>
#include "Runtime/HookManager.h"
#include "Runtime/ModuleRegistry.h"
#include "Runtime/EventBus.h"
#include <string.h>

static unsigned checks, callbacks;
static void *expected_subject;
static RuntimeEventId expected_event;
static unsigned long expected_v1,expected_v2;
static int payload_ok;
#define CHECK(x) do {++checks;if(!(x)){printf("共享接口回归失败：%d行\n",__LINE__);return 1;}}while(0)
static void receive(RuntimeEventId event,void *subject,unsigned long v1,unsigned long v2,void *user)
{
    ++callbacks;
    if(event!=expected_event || subject!=expected_subject || v1!=expected_v1 ||
       v2!=expected_v2 || user!=(void *)&checks) payload_ok=0;
}

int main(void)
{
    /* 先验证合法模块确实能声明该槽，再释放，避免后两项受旧声明影响。 */
    int legal=HookManager_Claim(SHARED_HOOK_CONTROLLER_MENU,RUNTIME_MODULE_CONTROLLER);
    HookManager_ReleaseOwned(RUNTIME_MODULE_CONTROLLER);
    /* COUNT是枚举上界，本身不是合法模块；接口应拒绝，不应将它写入拥有者数组。 */
    int outside=HookManager_Claim(SHARED_HOOK_CONTROLLER_MENU,(RuntimeModuleId)RUNTIME_MODULE_COUNT);
    HookManager_ReleaseOwned((RuntimeModuleId)RUNTIME_MODULE_COUNT);
    /* 负数同样不属于合法身份，必须独立检查。清理只影响本探针自己的静态数组。 */
    int negative=HookManager_Claim(SHARED_HOOK_CONTROLLER_MENU,(RuntimeModuleId)-1);
    HookManager_ReleaseOwned((RuntimeModuleId)-1);
    printf("合法模块接受=%d，枚举上界接受=%d，负数接受=%d。\n",legal,outside,negative);
    CHECK(legal && !outside && !negative);
    /* 非法owner或hook不能污染任何槽；重复声明幂等，其他owner不能抢占。 */
    for (int owner=RUNTIME_MODULE_NONE;owner<=RUNTIME_MODULE_COUNT+1;++owner) {
        int valid=owner>RUNTIME_MODULE_NONE && owner<RUNTIME_MODULE_COUNT;
        CHECK(HookManager_Claim(SHARED_HOOK_INPUT_FRAME,(RuntimeModuleId)owner)==valid);
        CHECK(HookManager_GetOwner(SHARED_HOOK_INPUT_FRAME)==(valid ? (RuntimeModuleId)owner:RUNTIME_MODULE_NONE));
        HookManager_ReleaseOwned((RuntimeModuleId)owner);
    }
    CHECK(!HookManager_Claim((SharedHookId)-1,RUNTIME_MODULE_QOL));
    CHECK(!HookManager_Claim(SHARED_HOOK_COUNT,RUNTIME_MODULE_QOL));
    CHECK(HookManager_GetOwner((SharedHookId)-1)==RUNTIME_MODULE_NONE);
    CHECK(HookManager_GetOwner(SHARED_HOOK_COUNT)==RUNTIME_MODULE_NONE);
    CHECK(HookManager_Claim(SHARED_HOOK_INPUT_FRAME,RUNTIME_MODULE_INPUT));
    CHECK(HookManager_Claim(SHARED_HOOK_INPUT_FRAME,RUNTIME_MODULE_INPUT));
    CHECK(!HookManager_Claim(SHARED_HOOK_INPUT_FRAME,RUNTIME_MODULE_QOL));
    HookManager_ReleaseOwned((RuntimeModuleId)-1);
    HookManager_ReleaseOwned(RUNTIME_MODULE_COUNT);
    HookManager_ReleaseOwned(RUNTIME_MODULE_QOL);
    CHECK(HookManager_GetOwner(SHARED_HOOK_INPUT_FRAME)==RUNTIME_MODULE_INPUT);
    HookManager_ReleaseOwned(RUNTIME_MODULE_INPUT);
    CHECK(HookManager_GetOwner(SHARED_HOOK_INPUT_FRAME)==RUNTIME_MODULE_NONE);

    /* 每类对象分别提供不同地址，逐事件验证原样转发；总线不能加8或猜对象类型。 */
    unsigned char keyboard[264]={0},mouse[32]={0},strategy[32]={0},jmm[32]={0};
    void *subjects[RUNTIME_EVENT_COUNT]={strategy,strategy,jmm,jmm,keyboard,keyboard+8,mouse,keyboard};
    unsigned long values1[RUNTIME_EVENT_COUNT]={854,640,1234,1234,0,5678,0,7};
    unsigned long values2[RUNTIME_EVENT_COUNT]={480,480,0,0,0,0,0,0};
    payload_ok=1;
    CHECK(!EventBus_Subscribe((RuntimeEventId)-1,receive,0));
    CHECK(!EventBus_Subscribe(RUNTIME_EVENT_COUNT,receive,0));
    CHECK(!EventBus_Subscribe(RUNTIME_EVENT_INPUT_SAMPLED,0,0));
    for (int event=0;event<RUNTIME_EVENT_COUNT;++event) {
        CHECK(EventBus_Subscribe((RuntimeEventId)event,receive,&checks));
        expected_event=(RuntimeEventId)event;expected_subject=subjects[event];
        expected_v1=values1[event];expected_v2=values2[event];
        EventBus_Emit(expected_event,expected_subject,expected_v1,expected_v2);
        CHECK(callbacks==(unsigned)event+1 && payload_ok);
        EventBus_Emit(expected_event,0,0,0);
        CHECK(callbacks==(unsigned)event+1);
    }
    EventBus_Emit((RuntimeEventId)-1,keyboard,0,0);
    EventBus_Emit(RUNTIME_EVENT_COUNT,keyboard,0,0);
    CHECK(callbacks==RUNTIME_EVENT_COUNT);
    printf("共享owner范围、幂等/隔离/释放与八事件参数契约通过：%u项。\n",checks);
    return 0;
}
