#ifndef BLADESWORD_QOL_GROUND_ITEMS_H
#define BLADESWORD_QOL_GROUND_ITEMS_H

#include "../../Runtime/Runtime.h"

/*
 * 地面物品模块只负责两个与单个掉落对象直接相关的职责：
 * 1. 原版 Update 完成后按配置再次调用游戏自己的名称显示函数；
 * 2. 记录掉落对象的出现时间，并判断它是否已经落地且达到自动拾取等待时间。
 */
int GroundItems_Initialize(const RuntimeContext* runtime,
                           int always_show_names,
                           unsigned long drop_delay_ms);

void GroundItems_AfterUpdate(void* object);

/* 符合“已落地 + 等待时间已到”时返回 1。 */
int GroundItems_IsReadyForAutomaticPickup(unsigned long object_address);

#endif
