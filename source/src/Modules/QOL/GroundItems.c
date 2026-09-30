#include "GroundItems.h"
#include "../../Runtime/Win32Bridge.h"

#define GAME_IMAGE_BASE 0x00400000ul

#define GROUND_RECORD_OFFSET 0x81ul
#define GROUND_VERTICAL_OFFSET 0x63ul
#define GROUND_FLIGHT_TICK_OFFSET 0x7Dul

#define TRACK_BUCKET_COUNT 64ul
#define TRACK_BUCKET_WAYS 4ul
#define TRACK_REAPPEAR_GAP_MS 750ul

typedef void (__fastcall *ShowItemNameFn)(void* object, void* unused_edx, int mode);

typedef struct GroundStamp {
    unsigned long object;
    unsigned long record;
    unsigned long first_seen_ms;
    unsigned long last_seen_ms;
    unsigned long flight_tick;
} GroundStamp;

static const GameProfile* g_profile;
static ShowItemNameFn g_show_item_name;
static int g_always_show_names;
static unsigned long g_drop_delay_ms;
static GroundStamp g_ground_stamps[TRACK_BUCKET_COUNT][TRACK_BUCKET_WAYS];

static ShowItemNameFn show_name_from_address(unsigned long address)
{
    union {
        unsigned long address;
        ShowItemNameFn function;
    } value;

    value.address = address;
    return value.function;
}

static unsigned long stamp_bucket(unsigned long object)
{
    unsigned long mixed;

    /*
     * 游戏对象通常至少按 4 字节对齐，直接用低位会让很多对象挤进相同桶。
     * 混合高低地址后再取 64 个桶，四路组相联足以覆盖同一帧的大量掉落。
     */
    mixed = (object >> 4) ^ (object >> 11) ^ (object >> 19);
    return mixed & (TRACK_BUCKET_COUNT - 1ul);
}

static GroundStamp* choose_stamp(unsigned long object,
                                 unsigned long now,
                                 unsigned long record,
                                 unsigned long flight_tick)
{
    GroundStamp* bucket;
    GroundStamp* selected;
    unsigned long way;
    unsigned long oldest_age;

    bucket = g_ground_stamps[stamp_bucket(object)];
    selected = &bucket[0];
    oldest_age = 0ul;

    for (way = 0ul; way < TRACK_BUCKET_WAYS; ++way) {
        GroundStamp* current = &bucket[way];
        unsigned long age;

        if (current->object == object) {
            return current;
        }
        if (current->object == 0ul) {
            return current;
        }

        age = now - current->last_seen_ms;
        if (age >= oldest_age) {
            oldest_age = age;
            selected = current;
        }
    }

    /*
     * 一个桶四个位置都被不同对象占用时，替换最久没有更新的一项。
     * 这只会让被替换对象重新计时，最坏结果是“晚一点自动拾取”，不会提前拾取。
     */
    (void)record;
    (void)flight_tick;
    return selected;
}

static void observe_lifetime(unsigned long object)
{
    unsigned long record;
    unsigned long flight_tick;
    unsigned long now;
    GroundStamp* stamp;
    int new_lifetime;

    if (g_drop_delay_ms == 0ul || object == 0ul) {
        return;
    }

    if (!RuntimeWin32_Read(object + GROUND_RECORD_OFFSET, &record, 4ul) ||
        !RuntimeWin32_Read(object + GROUND_FLIGHT_TICK_OFFSET, &flight_tick, 4ul)) {
        return;
    }

    now = RuntimeWin32_TickCount();
    stamp = choose_stamp(object, now, record, flight_tick);
    new_lifetime = 0;

    if (stamp->object != object) {
        new_lifetime = 1;
    } else if (stamp->record != record) {
        new_lifetime = 1;
    } else if (flight_tick < stamp->flight_tick) {
        new_lifetime = 1;
    } else if ((now - stamp->last_seen_ms) > TRACK_REAPPEAR_GAP_MS) {
        new_lifetime = 1;
    }

    if (new_lifetime) {
        stamp->object = object;
        stamp->record = record;
        stamp->first_seen_ms = now;
    }

    stamp->last_seen_ms = now;
    stamp->flight_tick = flight_tick;
}

static int waited_long_enough(unsigned long object)
{
    GroundStamp* bucket;
    unsigned long way;
    unsigned long now;

    if (g_drop_delay_ms == 0ul) {
        return 1;
    }

    now = RuntimeWin32_TickCount();
    bucket = g_ground_stamps[stamp_bucket(object)];

    for (way = 0ul; way < TRACK_BUCKET_WAYS; ++way) {
        const GroundStamp* stamp = &bucket[way];

        if (stamp->object != object) {
            continue;
        }

        /*
         * 当前对象如果已经一段时间没有经过 GroundItem Update，
         * 就不能继续使用旧生命周期时间，避免游戏复用对象地址时提前放行。
         */
        if ((now - stamp->last_seen_ms) > TRACK_REAPPEAR_GAP_MS) {
            return 0;
        }

        return (now - stamp->first_seen_ms) >= g_drop_delay_ms ? 1 : 0;
    }

    return 0;
}

static int original_drop_motion_finished(unsigned long object)
{
    unsigned long flight_tick;
    unsigned long vertical_bits;
    unsigned long tick_limit;
    signed long vertical;

    if (!g_profile || object == 0ul) {
        return 0;
    }

    if (!RuntimeWin32_Read(object + GROUND_FLIGHT_TICK_OFFSET, &flight_tick, 4ul) ||
        !RuntimeWin32_Read(object + GROUND_VERTICAL_OFFSET, &vertical_bits, 4ul) ||
        !RuntimeWin32_Read(GAME_IMAGE_BASE + g_profile->qol.drop_flight_tick_limit_rva,
                           &tick_limit,
                           4ul)) {
        return 0;
    }

    /*
     * 游戏初始化后的动画上限是一个很小的正数。
     * 值为 0 或明显异常时保持失败开放到“暂不自动拾取”，不猜测动画状态。
     */
    if (tick_limit == 0ul || tick_limit > 10000ul) {
        return 0;
    }

    vertical = (signed long)vertical_bits;
    return flight_tick >= tick_limit && vertical == 0l ? 1 : 0;
}

int GroundItems_Initialize(const RuntimeContext* runtime,
                           int always_show_names,
                           unsigned long drop_delay_ms)
{
    if (!runtime || !runtime->profile) {
        return 0;
    }

    g_profile = runtime->profile;
    g_always_show_names = always_show_names ? 1 : 0;
    g_drop_delay_ms = drop_delay_ms;
    g_show_item_name = show_name_from_address(
        GAME_IMAGE_BASE + runtime->profile->qol.show_item_name_rva);

    return g_show_item_name ? 1 : 0;
}

void GroundItems_AfterUpdate(void* object)
{
    union {
        void* pointer;
        unsigned long address;
    } value;

    if (!object) {
        return;
    }

    value.pointer = object;
    observe_lifetime(value.address);

    /*
     * 名称仍由游戏自己的 ShowItemName 创建和绘制。
     * 模块不修改文字、颜色、字体或背景样式，只改变“每次更新都请求显示”的触发条件。
     */
    if (g_always_show_names && g_show_item_name) {
        g_show_item_name(object, (void*)0, 1);
    }
}

int GroundItems_IsReadyForAutomaticPickup(unsigned long object_address)
{
    if (!original_drop_motion_finished(object_address)) {
        return 0;
    }
    return waited_long_enough(object_address);
}
