#include "Runtime/Runtime.h"

/*
 * Main.c 是整个 EDSlash.asi 唯一允许拥有 Windows/ASI 入口的源码文件。
 * 本体和外传后端都只是普通模块，不能再定义自己的 DllMain / InitializeASI。
 */

typedef unsigned long DWORD;
typedef int BOOL;
typedef void* HINSTANCE;
typedef void* LPVOID;

#define TRUE 1
#define DLL_PROCESS_ATTACH 1u

/*
 * Runtime_Initialize 自己还有一次性保护，所以 DllMain 与 InitializeASI 即使都被 Loader 调用也不会重复安装 Hook。
 * DllMain只执行已有同步安装；线程和设备初始化延后到Loader锁外，避免加载时死锁。
 */
void InitializeASI(void)
{
    const RuntimeContext* runtime = Runtime_GetContext();
    if (runtime && runtime->self_module) {
        (void)Runtime_Initialize(runtime->self_module);
    }
}

BOOL __stdcall DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        (void)Runtime_Initialize(module);
    }

    return TRUE;
}
