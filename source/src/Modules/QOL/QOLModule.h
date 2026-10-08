#ifndef BLADESWORD_QOL_QOL_MODULE_H
#define BLADESWORD_QOL_QOL_MODULE_H

#include "../../Runtime/Runtime.h"

/* 地面物品名称与自动拾取统一由 QoL 模块初始化。 */
/* 手柄只投递本帧随机请求，窗口资格/编码/字形/原文本写入归QOL。 */
int QOLModule_RandomName(void *page);
int QOLModule_Initialize(const RuntimeContext* runtime);

#endif
