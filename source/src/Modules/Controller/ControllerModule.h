#ifndef EDSLASH_CONTROLLER_MODULE_H
#define EDSLASH_CONTROLLER_MODULE_H
#include "../../Runtime/Runtime.h"
/* Controller只提供模块入口；Windows入口统一由src/Main.c拥有。 */
int ControllerModule_Initialize(const RuntimeContext *runtime);
/* 登记只表示采样桥安装；返回1才表示动作桥已经就绪并负责idle安全点。 */
int ControllerModule_IsReady(void);
void ControllerModule_Shutdown(void);
#endif
