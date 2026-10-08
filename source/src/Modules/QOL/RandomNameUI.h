#ifndef EDSLASH_RANDOM_NAME_UI_H
#define EDSLASH_RANDOM_NAME_UI_H
#include "../../Runtime/Runtime.h"
/* 核心和原名称框之间的适配层；所有接口在游戏输入线程使用。 */
int RandomNameUI_Initialize(const RuntimeContext *runtime);
int RandomNameUI_Request(void *page);
void RandomNameUI_AfterInputFrame(void);
void RandomNameUI_End(void);
#endif
