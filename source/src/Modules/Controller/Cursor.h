#ifndef EDSLASH_CONTROLLER_CURSOR_H
#define EDSLASH_CONTROLLER_CURSOR_H
#include "Plugin.h"
/* 只隔离软件光标绘制，不改变鼠标输入、窗口光标坐标或任何HitRect。 */
bool Cursor_Initialize(void);
void Cursor_Shutdown(void);
#endif
