#ifndef EDSLASH_SETTINGS_WINDOW_H
#define EDSLASH_SETTINGS_WINDOW_H
#include "Runtime.h"
#include "Focus.h"
#include <stdint.h>
/* 窗口独占输入，外部只需初始化、送入手柄帧和查询捕获；业务和原绘制封装在内部。 */
int SettingsWindow_Initialize(const RuntimeContext *runtime);
int SettingsWindow_Active(void);
/* 原设置页的文字入口：只接受当前可见AA页，不伪造原子控件。 */
int SettingsWindow_NativeEntryRect(void *page,RuntimeFocusRect *rectangle);
int SettingsWindow_OpenNative(void *page);
int SettingsWindow_ShowPointer(void);
int SettingsWindow_Pad(uint32_t held,uint32_t pressed,int lt,int rt,float lx,float ly,float rx,float ry,uint32_t now);
void SettingsWindow_Close(void);
int SettingsWindow_Wheel(int delta);
#endif
