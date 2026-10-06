#ifndef EDSLASH_CONTROLLER_CURSOR_H
#define EDSLASH_CONTROLLER_CURSOR_H
#include "Plugin.h"
/* 隔离软件光标绘制并复用原动态框，不改变鼠标输入、窗口光标坐标或任何HitRect。 */
bool Cursor_Initialize(void);
void Cursor_Shutdown(void);
#endif
