#ifndef EDSLASH_RUNTIME_FOCUS_H
#define EDSLASH_RUNTIME_FOCUS_H
#include "Runtime.h"
#include <stdint.h>

typedef struct RuntimeFocusRect {int left,top,right,bottom;} RuntimeFocusRect;
typedef struct RuntimeFocusRequest {
    RuntimeFocusRect rectangle;
    int icon_phase; /* 方形动作图标要求在原图标之后、说明文字之前画框。 */
    unsigned priority; /* 真正模态/当前业务由提供者决定；多个模块只画最高优先级一项。 */
} RuntimeFocusRequest;
typedef int (*RuntimeFocusProvider)(RuntimeFocusRequest *request,void *user);
/* 游戏身份和原函数签名由Runtime校验，失败只停提示，不影响原菜单操作。 */
int RuntimeFocus_Initialize(const RuntimeContext *runtime);
/* 注册/查询/注销在游戏线程使用，提供者只返回本帧矩形，不得保留游戏对象。 */
int RuntimeFocus_Register(RuntimeModuleId owner,RuntimeFocusProvider provider,void *user);
void RuntimeFocus_Unregister(RuntimeModuleId owner);
/* 是否在最近UI绘制帧成功绘制该模块当前焦点，用于素材未就绪时保留旧反馈。 */
int RuntimeFocus_WasDrawn(RuntimeModuleId owner);
/* 供异常记录器查询当前共享绘制阶段；不在绘制中返回NULL。 */
const char *RuntimeFocus_Stage(void);
/* 原图标Draw返回后调用；仅绘制与原图标坐标匹配的候选，不移动或重新画文字。 */
void RuntimeFocus_DrawIcon(unsigned long surface,int x,int y);
#endif
