#ifndef EDSLASH_SETTINGS_WINDOW_H
#define EDSLASH_SETTINGS_WINDOW_H
#include "Runtime.h"
#include <stdint.h>
/* 窗口独占输入，外部只需初始化、送入手柄帧和查询捕获；业务和原绘制封装在内部。 */
int SettingsWindow_Initialize(const RuntimeContext *runtime);
int SettingsWindow_Active(void);
int SettingsWindow_ShowPointer(void);
int SettingsWindow_Pad(uint32_t held,uint32_t pressed,int lt,int rt,float lx,float ly,float rx,float ry,uint32_t now);
void SettingsWindow_Close(void);
int SettingsWindow_Wheel(int delta);
#endif
