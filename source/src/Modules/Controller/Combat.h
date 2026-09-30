#ifndef EDSLASH_COMBAT_H
#define EDSLASH_COMBAT_H
#include "Plugin.h"

/* 这是手柄自己的语义数据，不模拟 MouseManager 布局，也不包含鼠标坐标。 */
typedef struct { int x,y; } WorldPoint;
typedef struct {
    unsigned count;
    int selectors[64];
    uint32_t start_tick, end_tick;
    int direction;
    bool ended;
} ActionHistory;
typedef struct {
    int method;
    int selector;
    bool sequence;
} ResolvedSkill;

bool Skill_Resolve(void *role,int selection,const WorldPoint *point,const ActionHistory *history,ResolvedSkill *out);
void Combat_Reset(void);
void Combat_Suspend(void);
uint32_t Combat_Target(void);
void Combat_Update(void *role,uint32_t candidate);
bool Combat_OwnsHistory(void);
bool Combat_AllowsMouseRetry(void);
void Combat_Record(int selector,int direction);
void Combat_End(void);
#endif
