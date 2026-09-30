#ifndef BLADESWORD_QOL_ITEM_CLASSIFIER_H
#define BLADESWORD_QOL_ITEM_CLASSIFIER_H

#include "../../Runtime/Runtime.h"

typedef enum PickupItemClass {
    PICKUP_ITEM_OTHER = 0,
    PICKUP_ITEM_MONEY = 1,
    PICKUP_ITEM_RECOVERY = 2,
    PICKUP_ITEM_GEM = 3,
    PICKUP_ITEM_CHARM = 4
} PickupItemClass;

int ItemClassifier_Initialize(const RuntimeContext* runtime);

/* 确认对象确实是当前版本的地面物品对象。 */
int ItemClassifier_IsGroundItem(unsigned long object_address);

/* 读取原版物品记录的 Type 列并映射为自动拾取使用的语义类别。 */
int ItemClassifier_GetPickupClass(unsigned long object_address, PickupItemClass* item_class);

#endif
