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
typedef enum { ACTION_LEFT,ACTION_COMBO,ACTION_SKILL,ACTION_THROW,ACTION_ULTIMATE,ACTION_RIGHT } ActionSource;
/* 快捷请求直接携带选择和来源，不修改右手装备或伪造 Y/鼠标按键。 */
void Combat_Request(int selection,ActionSource source,bool left_style);
bool Combat_ComboAvailable(unsigned index);
void Combat_SelectCombo(unsigned index);
void Combat_RequestPoint(int selection,const WorldPoint *point);
/* 和RT同一种快捷技能请求，仅追加显式世界点；不另设跳跃执行来源。 */
void Combat_RequestSkillPoint(int selection,bool left_style,const WorldPoint *point);
/* 预览复用快捷技能的解析及起手资格；只读当前历史，不建立/取消原动作。 */
bool Combat_PreviewSkill(void *role,int selection,const WorldPoint *point,ResolvedSkill *out);

bool Skill_Resolve(void *role,int selection,const WorldPoint *point,const ActionHistory *history,ResolvedSkill *out);
/* 只由实际成功建立的玩家闪避通知；失败请求不得保留进度。 */
void Combat_DodgeStarted(void *role);
/* 原版归零分支的资格查询：非闪避保留状态完整调用原查询。 */
int __fastcall Combat_ComboResetHook(void *hud,void *unused,int selection);
void Combat_Reset(void);
void Combat_Suspend(void);
uint32_t Combat_Target(void);
void Combat_Update(void *role);
bool Combat_OwnsHistory(void);
bool Combat_AllowsMouseRetry(void);
void Combat_Record(int selector,int direction);
void Combat_End(void);
/* 原版鼠标与手柄分别保持自己的历史，只在真实来源交接时同步已成功动作的业务历史。 */
void Combat_ExportHistory(void);
void Combat_ImportHistory(void);
void Combat_RecordExtra(int selector,int direction,int extra);
/* 创建通知携带真实Runtime，命中锁才可拒绝旧攻击会话的迟到弹道。 */
void Combat_RecordRuntime(int selector,int direction,int extra,void *runtime);
#endif
