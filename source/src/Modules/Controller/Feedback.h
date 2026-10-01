#ifndef EDSLASH_FEEDBACK_H
#define EDSLASH_FEEDBACK_H
#include "Combat.h"
/* 只更换右键图标绘制参数，不更换角色永久装备或连招套组。 */
bool Feedback_Initialize(void);
void Feedback_Shutdown(void);
void Feedback_Start(int selection,ActionSource source);
void Feedback_End(void);
/* 原Runtime结束只是进入动画收尾，不能立即撤掉整次动作图标。 */
void Feedback_RuntimeEnded(void);
bool Feedback_Selection(int *selection,int *icon);
void Feedback_Ultimate(unsigned slot);
#endif
