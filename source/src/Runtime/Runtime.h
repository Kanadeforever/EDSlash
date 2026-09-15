#ifndef BLADESWORD_QOL_RUNTIME_H
#define BLADESWORD_QOL_RUNTIME_H

#include "GameProfile.h"
#include "EventBus.h"
#include "HookManager.h"

/*
 * RuntimeContext 是整个大修模组共享的“当前进程上下文”。
 * 所有模块拿到的是同一个结构，因此不会出现 DisplayFix 认为是本体、手柄模块又自己判断成外传的情况。
 */
typedef struct RuntimeContext {
    void* self_module;
    const GameProfile* profile;
} RuntimeContext;

/* 只由唯一 Main.c 调用。成功识别并初始化所有当前模块返回 1。 */
int Runtime_Initialize(void* self_module);

/* 返回只读的全局 RuntimeContext；初始化之前 profile 可能为空。 */
const RuntimeContext* Runtime_GetContext(void);

/* 模块使用的事件 API。这里包一层，避免模块直接依赖 EventBus 的内部实现。 */
int Runtime_Subscribe(RuntimeEventId event_id, RuntimeEventCallback callback, void* user_data);
void Runtime_EmitEvent(RuntimeEventId event_id, void* subject, unsigned long value1, unsigned long value2);

#endif
