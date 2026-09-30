#include "ItemClassifier.h"
#include "../../Runtime/Win32Bridge.h"

#define GAME_IMAGE_BASE 0x00400000ul
#define GROUND_OBJECT_KIND_OFFSET 0x67ul
#define GROUND_RECORD_OFFSET 0x81ul
#define GROUND_OBJECT_KIND 0x17ul

#define RECORD_VALUE_COUNT_OFFSET 0x04ul
#define RECORD_VALUES_OFFSET 0x08ul
#define RECORD_ITEM_TYPE_COLUMN 2ul

static const GameProfile* g_profile;

static int read_record_column(unsigned long record,
                              unsigned long column,
                              signed long* value)
{
    signed long count;
    unsigned long values;
    unsigned long raw;

    if (record == 0ul || !value) {
        return 0;
    }

    if (!RuntimeWin32_Read(record + RECORD_VALUE_COUNT_OFFSET, &count, 4ul) ||
        count <= 0l ||
        column >= (unsigned long)count) {
        return 0;
    }

    if (!RuntimeWin32_Read(record + RECORD_VALUES_OFFSET, &values, 4ul) ||
        values == 0ul) {
        return 0;
    }

    if (!RuntimeWin32_Read(values + column * 4ul, &raw, 4ul)) {
        return 0;
    }

    *value = (signed long)raw;
    return 1;
}

int ItemClassifier_Initialize(const RuntimeContext* runtime)
{
    if (!runtime || !runtime->profile) {
        return 0;
    }
    g_profile = runtime->profile;
    return 1;
}

int ItemClassifier_IsGroundItem(unsigned long object_address)
{
    unsigned long vtable;
    unsigned long kind;

    if (!g_profile || object_address == 0ul) {
        return 0;
    }

    if (!RuntimeWin32_Read(object_address, &vtable, 4ul) ||
        !RuntimeWin32_Read(object_address + GROUND_OBJECT_KIND_OFFSET, &kind, 4ul)) {
        return 0;
    }

    if (vtable != GAME_IMAGE_BASE + g_profile->qol.ground_item_vtable_rva) {
        return 0;
    }

    return kind == GROUND_OBJECT_KIND ? 1 : 0;
}

int ItemClassifier_GetPickupClass(unsigned long object_address, PickupItemClass* item_class)
{
    unsigned long record;
    signed long item_type;

    if (!item_class || !ItemClassifier_IsGroundItem(object_address)) {
        return 0;
    }

    if (!RuntimeWin32_Read(object_address + GROUND_RECORD_OFFSET, &record, 4ul) ||
        !read_record_column(record, RECORD_ITEM_TYPE_COLUMN, &item_type)) {
        return 0;
    }

    /*
     * 两个已支持主程序的物品 Type 语义在这些类别上一致。
     * 未列出的类型保持 OTHER；“全部”模式仍可以拾取 OTHER。
     */
    if (item_type == 0l) {
        *item_class = PICKUP_ITEM_MONEY;
    } else if (item_type == 10l) {
        *item_class = PICKUP_ITEM_RECOVERY;
    } else if (item_type == 30l || item_type == 35l) {
        *item_class = PICKUP_ITEM_GEM;
    } else if (item_type == 50l) {
        *item_class = PICKUP_ITEM_CHARM;
    } else {
        *item_class = PICKUP_ITEM_OTHER;
    }

    return 1;
}
