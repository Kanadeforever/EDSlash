#ifndef EDSLASH_CONTROLLER_MENU_H
#define EDSLASH_CONTROLLER_MENU_H
#include "Plugin.h"

/* 公共路由只读真实GUI链；返回对象不代表该页已实现手柄操作。 */
void *Menu_Context(unsigned *reason);
bool Menu_BlocksGameplay(void);
bool Menu_CapturesInput(void);
/* 仅返回软件光标绘制锚点，不用于GetCursorPos的输入采样或真实鼠标移动。 */
bool Menu_CursorAnchor(POINT *point);
/* 快捷格使用原动态选择框，返回当前真实格的矩形，不修改投掷装备状态。 */
bool Menu_FocusFrame(RECT *rectangle);
/* 外传标题、快捷格空手与动作菜单使用自身高亮/动态框；持有物品保留原图标。 */
bool Menu_HidesCursor(void);
/* 在采样后处理菜单业务，所以标题没有玩家时也能工作。 */
void Menu_Update(void);
void Menu_Suspend(void);
bool Menu_Initialize(void);
void Menu_Shutdown(void);
/* 原动作菜单拥有独立候选焦点。返回关闭状态，让输入规则屏蔽离开菜单时的旧输入。 */
bool ActionMenu_Update(void);
bool ActionMenu_Active(void);
bool ActionMenu_Owns(void *root);
bool ActionMenu_Anchor(POINT *point);
bool ActionMenu_FocusFrame(RECT *rectangle);
void ActionMenu_Suspend(void);
bool ActionMenu_Initialize(void);
void ActionMenu_Shutdown(void);
#endif
