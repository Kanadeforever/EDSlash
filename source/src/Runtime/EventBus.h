#ifndef BLADESWORD_QOL_EVENT_BUS_H
#define BLADESWORD_QOL_EVENT_BUS_H

/*
 * EventBus 是“共享 Hook”的第二层。
 * HookManager 保证一个物理游戏入口只由一个提供者拥有；提供者进入 Hook 后再通过 EventBus 广播语义事件。
 * 以后手柄模块想知道“进入游戏”“UI 开始绘制”，只订阅事件，不再自己改同一个 CALL/vtable。
 */

typedef enum RuntimeEventId {
    RUNTIME_EVENT_GAMEPLAY_ENTER = 0,
    RUNTIME_EVENT_GAMEPLAY_EXIT = 1,
    RUNTIME_EVENT_UI_DRAW_BEGIN = 2,
    RUNTIME_EVENT_UI_DRAW_END = 3,
    RUNTIME_EVENT_COUNT = 4
} RuntimeEventId;

typedef void (*RuntimeEventCallback)(RuntimeEventId event_id,
                                     void* subject,
                                     unsigned long value1,
                                     unsigned long value2,
                                     void* user_data);

/* 注册一个回调。成功返回 1；事件无效、回调为空或槽位已满时返回 0。 */
int EventBus_Subscribe(RuntimeEventId event_id, RuntimeEventCallback callback, void* user_data);

/* 广播一个事件；按注册顺序调用当前事件的所有回调。 */
void EventBus_Emit(RuntimeEventId event_id, void* subject, unsigned long value1, unsigned long value2);

#endif
