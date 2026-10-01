#ifndef EDSLASH_GUARD_H
#define EDSLASH_GUARD_H
#include "Plugin.h"

/* 成对验证并安装受控角色防御扣费和方向闪避入口，不改 NPC 或物品消耗。 */
void Guard_ApplySettings(void);
bool Guard_Initialize(void);
void Guard_Shutdown(void);
void Guard_Update(void *role);
void Guard_Reset(void);
/* 供宿主回放调用真实逻辑；原版浮点参数按 thiscall 的栈布局传递。 */
void Guard_Periodic(void *role,float amount);
void Guard_RunCost(void *role,float amount);
int Guard_Hit(void *role,int index);
/* 在原版攻击受击回调执行完后，用实际生命减少确认命中，再给真正的攻击者恢复体力。 */
void Guard_RecoverHit(void *victim,void *attacker,float health_before,bool was_enemy);
#endif
