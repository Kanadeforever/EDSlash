#ifndef EDSLASH_TARGET_H
#define EDSLASH_TARGET_H
#include "Combat.h"

/* 命中票据在原受击业务前保存身份，返回后只接受同一攻击会话的实际伤害。 */
typedef struct {
    void *world,*victim;
    uint32_t actor,victim_handle,generation;
    bool eligible;
} TargetHit;

bool Target_Rebuilt(void);
bool Target_Initialize(void);
void Target_Shutdown(void);
void Target_Reset(void);
void Target_Suspend(void);
/* 在每次输入采样后处理松手，预览或交互提前返回也不能保留旧命中会话。 */
void Target_SampleInput(void);
void Target_Update(void *role);
uint32_t Target_Handle(void);
WorldPoint Target_Point(void *role,bool directional);
bool Target_IsDirectional(int method);
void Target_RecordRuntime(void *runtime);
void Target_RecordPending(int opcode,int method,int a2,int a3);
TargetHit Target_BeginHit(void *victim,void *attacker,void *runtime,bool was_enemy);
void Target_EndHit(const TargetHit *hit,float health_before);
/* 中断只结束锁定会话，不取消原版动作或改变连招历史。 */
void Target_Interrupted(void);
#endif
