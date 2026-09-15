#include "EventBus.h"

/*
 * 每类事件暂时最多 16 个订阅者。
 * 这是一个老 Win32 游戏的内置模块系统，不需要动态内存；固定数组反而更容易保证生命周期和失败行为。
 */
#define EVENT_BUS_MAX_SUBSCRIBERS 16u

typedef struct EventSubscriber {
    RuntimeEventCallback callback;
    void* user_data;
} EventSubscriber;

static EventSubscriber g_subscribers[RUNTIME_EVENT_COUNT][EVENT_BUS_MAX_SUBSCRIBERS];
static unsigned long g_subscriber_count[RUNTIME_EVENT_COUNT];

int EventBus_Subscribe(RuntimeEventId event_id, RuntimeEventCallback callback, void* user_data)
{
    unsigned long index;

    if ((unsigned long)event_id >= (unsigned long)RUNTIME_EVENT_COUNT || !callback) {
        return 0;
    }

    index = g_subscriber_count[event_id];
    if (index >= EVENT_BUS_MAX_SUBSCRIBERS) {
        return 0;
    }

    g_subscribers[event_id][index].callback = callback;
    g_subscribers[event_id][index].user_data = user_data;
    g_subscriber_count[event_id] = index + 1u;
    return 1;
}

void EventBus_Emit(RuntimeEventId event_id, void* subject, unsigned long value1, unsigned long value2)
{
    unsigned long i;
    unsigned long count;

    if ((unsigned long)event_id >= (unsigned long)RUNTIME_EVENT_COUNT) {
        return;
    }

    /*
     * 先把 count 拍下来。这样即使某个回调未来间接注册了新订阅者，新的回调也从“下一次事件”开始生效，
     * 不会在本轮广播中途突然改变循环边界。
     */
    count = g_subscriber_count[event_id];
    for (i = 0u; i < count; ++i) {
        RuntimeEventCallback callback = g_subscribers[event_id][i].callback;
        if (callback) {
            callback(event_id, subject, value1, value2, g_subscribers[event_id][i].user_data);
        }
    }
}
