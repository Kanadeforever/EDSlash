#ifndef BLADESWORD_QOL_AUTO_PICKUP_H
#define BLADESWORD_QOL_AUTO_PICKUP_H

#include "../../Runtime/Runtime.h"

typedef enum AutoPickupPolicy {
    AUTO_PICKUP_POLICY_OFF = 0,
    AUTO_PICKUP_POLICY_MONEY = 1,
    AUTO_PICKUP_POLICY_MONEY_RECOVERY = 2,
    AUTO_PICKUP_POLICY_USEFUL = 3,
    AUTO_PICKUP_POLICY_ALL = 4
} AutoPickupPolicy;

int AutoPickup_Initialize(const RuntimeContext* runtime,
                          AutoPickupPolicy policy,
                          unsigned long interval_ms);

/* 游戏原输入刷新完成后调用，用于在主线程按设定周期触发一次原生拾取动作。 */
void AutoPickup_ApplySettings(AutoPickupPolicy policy,unsigned long interval_ms);
void AutoPickup_AfterInputFrame(void);

/* 安装失败回滚时先停自动扫描；即使输入 Hook 未能撤销也只执行原版输入。 */
void AutoPickup_Disable(void);

/*
 * 物品进入原版 PickupEntry 前调用。
 * 返回 1 表示继续原版拾取；返回 0 表示这一次自动扫描忽略该物品。
 * 玩家手动按 Z 时本模块没有活动扫描标记，因此始终返回 1。
 */
int AutoPickup_AllowPickupCandidate(unsigned long ground_item);

#endif
