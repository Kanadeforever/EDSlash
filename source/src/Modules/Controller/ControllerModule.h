#ifndef EDSLASH_CONTROLLER_MODULE_H
#define EDSLASH_CONTROLLER_MODULE_H
#include "../../Runtime/Runtime.h"
/* Controller只提供模块入口；Windows入口统一由src/Main.c拥有。 */
int ControllerModule_Initialize(const RuntimeContext *runtime);
void ControllerModule_Shutdown(void);
#endif
