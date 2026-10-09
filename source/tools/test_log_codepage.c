#include "test_log_codepage.h"
/* 包装函数内部必须调用真实API，解除宏映射避免包装函数调用自身。 */
#undef MultiByteToWideChar
#undef WideCharToMultiByte

int WINAPI RuntimeTest_MultiByteToWideChar(UINT codepage,DWORD flags,LPCCH input,
    int input_count,LPWSTR output,int output_count)
{
    /* 只有系统ANSI代码页被替换，日志输出要求的CP_UTF8仍保持原语义。 */
    return MultiByteToWideChar(codepage==CP_ACP ? EDSLASH_TEST_ACP:codepage,
        flags,input,input_count,output,output_count);
}

int WINAPI RuntimeTest_WideCharToMultiByte(UINT codepage,DWORD flags,LPCWCH input,
    int input_count,LPSTR output,int output_count,LPCCH fallback,LPBOOL used_fallback)
{
    /* 调用Windows真实转换，保留无法表示字符时的替代行为和失败返回。 */
    return WideCharToMultiByte(codepage==CP_ACP ? EDSLASH_TEST_ACP:codepage,
        flags,input,input_count,output,output_count,fallback,used_fallback);
}
