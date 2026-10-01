#ifndef EDSLASH_INPUT_FRAME_H
#define EDSLASH_INPUT_FRAME_H
#include "Runtime.h"
/* 统一外层输入帧只安装一次。键盘采样、原场景解析仍保留各自真实调用位置。 */
int RuntimeInput_Initialize(const RuntimeContext *runtime);
int RuntimeInput_IsReady(void);
#endif
