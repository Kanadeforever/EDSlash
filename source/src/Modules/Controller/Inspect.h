#ifndef EDSLASH_CONTROLLER_INSPECT_H
#define EDSLASH_CONTROLLER_INSPECT_H
#include "Plugin.h"
/* 调查选择与战斗目标分开；保存句柄，原生悬停仅作为视觉投影。 */
bool Inspect_Update(void *role);
void Inspect_Activate(void);
void Inspect_Project(void);
void Inspect_Reset(void);
bool Inspect_Initialize(void);
void Inspect_Shutdown(void);
#endif
