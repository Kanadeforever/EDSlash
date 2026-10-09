#ifndef EDSLASH_TEST_LOG_CODEPAGE_H
#define EDSLASH_TEST_LOG_CODEPAGE_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* 仅日志回归目标强制包含本文件，将CP_ACP映射到测试指定的系统代码页。
 * 调用仍进入真实Windows转换API，不替换编码算法，也不改变生产ASI。 */
int WINAPI RuntimeTest_MultiByteToWideChar(UINT codepage,DWORD flags,LPCCH input,
    int input_count,LPWSTR output,int output_count);
int WINAPI RuntimeTest_WideCharToMultiByte(UINT codepage,DWORD flags,LPCWCH input,
    int input_count,LPSTR output,int output_count,LPCCH fallback,LPBOOL used_fallback);
#define MultiByteToWideChar RuntimeTest_MultiByteToWideChar
#define WideCharToMultiByte RuntimeTest_WideCharToMultiByte
#endif
