#ifndef EDSLASH_CONTROLLER_MENU_H
#define EDSLASH_CONTROLLER_MENU_H
#include "Plugin.h"

/* 公共路由只读真实GUI链；返回对象不代表该页已实现手柄操作。 */
void *Menu_Context(unsigned *reason);
bool Menu_BlocksGameplay(void);
bool Menu_CapturesInput(void);
/* 仅返回软件光标绘制锚点，不用于GetCursorPos的输入采样或真实鼠标移动。 */
bool Menu_CursorAnchor(POINT *point);
/* 外传标题已有原高亮，只在手柄来源隐藏软件光标，其它页保持原视觉标记。 */
bool Menu_HidesCursor(void);
/* 在采样后处理菜单业务，所以标题没有玩家时也能工作。 */
void Menu_Update(void);
void Menu_Suspend(void);
bool Menu_Initialize(void);
void Menu_Shutdown(void);
#endif
