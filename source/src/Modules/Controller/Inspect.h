#ifndef EDSLASH_CONTROLLER_INSPECT_H
#define EDSLASH_CONTROLLER_INSPECT_H
#include "Plugin.h"
/* 调查选择与战斗目标分开；保存句柄，原生悬停仅作为视觉投影。 */
bool Inspect_Update(void *role);
/* 返回true表示确实交给原业务；没有合法目标时不交出原手柄移动拥有权。 */
bool Inspect_Activate(void);
void Inspect_Project(void);
void Inspect_Reset(void);
bool Inspect_Initialize(void);
void Inspect_Shutdown(void);
#endif
