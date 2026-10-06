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
    RUNTIME_EVENT_INPUT_FRAME_BEGIN = 4,
    RUNTIME_EVENT_INPUT_SAMPLED = 5,
    RUNTIME_EVENT_INPUT_RESOLVED = 6,
    RUNTIME_EVENT_INPUT_FRAME_END = 7,
    RUNTIME_EVENT_COUNT = 8
} RuntimeEventId;

typedef void (*RuntimeEventCallback)(RuntimeEventId event_id,
                                     void* subject,
                                     unsigned long value1,
                                     unsigned long value2,
                                     void* user_data);

/* 同步事件契约：subject仅在本次回调期间借用，不能保存供下一帧解引用。
 * GAMEPLAY_ENTER：Strategy对象；value1/value2是目标宽/高。
 * GAMEPLAY_EXIT：Strategy对象；value1/value2是退出后的实际宽/高。
 * UI_DRAW_BEGIN/END：CJMMng对象；value1是原绘制上下文，value2为0。
 * INPUT_FRAME_BEGIN/END：原键盘对象（256字节缓冲位于对象+8），不是缓冲首址。
 *   BEGIN两值为0；END的value1是原刷新返回值，value2为0。
 * INPUT_SAMPLED：实际256字节键盘缓冲；value1为GetTickCount毫秒，value2为0。
 * INPUT_RESOLVED：WorldMouseManager对象；两值为0。
 * 这些对象不能互换。总线不读取游戏内存，不凭void*猜类型；提供者负责真实身份。
 * 无效事件或空subject不广播；允许Gameplay退出的实际宽高为0。 */

/* 注册一个回调。成功返回 1；事件无效、回调为空或槽位已满时返回 0。 */
int EventBus_Subscribe(RuntimeEventId event_id, RuntimeEventCallback callback, void* user_data);

/* 广播一个事件；按注册顺序调用当前事件的所有回调。 */
void EventBus_Emit(RuntimeEventId event_id, void* subject, unsigned long value1, unsigned long value2);

#endif
