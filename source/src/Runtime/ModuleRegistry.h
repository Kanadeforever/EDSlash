#ifndef BLADESWORD_QOL_MODULE_REGISTRY_H
#define BLADESWORD_QOL_MODULE_REGISTRY_H

#include "HookManager.h"

/* RuntimeContext 在 Runtime.h 中定义；这里只做前置声明，避免头文件互相包含成环。 */
struct RuntimeContext;

/*
 * 每个正式模块都必须提供同一种 Initialize 入口。
 * 返回 1 表示初始化成功；返回 0 表示模块拒绝初始化或发生结构验证失败。
 */
typedef int (*ModuleInitializeFn)(const struct RuntimeContext* runtime);

typedef struct ModuleDescriptor {
    RuntimeModuleId module_id;
    const char* name;
    ModuleInitializeFn initialize;
} ModuleDescriptor;

/*
 * 按固定顺序尝试初始化全部官方模块。
 * 某一个可选模块失败不会阻止后续模块继续启动；只要 Registry 本身参数合法就返回 1。
 * 这样未来 Controller 不会因为 DisplayFix 某个兼容签名失败而完全失去加载机会。
 */
int ModuleRegistry_InitializeAll(const struct RuntimeContext* runtime);

/* 查询某个模块这次进程中是否已经初始化成功。 */
int ModuleRegistry_IsInitialized(RuntimeModuleId module_id);

#endif
