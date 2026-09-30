#ifndef BLADESWORD_QOL_WIN32_BRIDGE_H
#define BLADESWORD_QOL_WIN32_BRIDGE_H

#include "GameProfile.h"

/*
 * 主 ASI 不直接链接 Windows 导入库。
 * Runtime 先利用游戏已经解析好的 GetModuleHandleA / GetProcAddress IAT 槽，
 * 再把 QoL 所需的少量 Kernel32 API 保存为函数指针。
 */

typedef struct RuntimeMemoryRegion {
    unsigned long base;
    unsigned long size;
    unsigned long state;
    unsigned long protect;
} RuntimeMemoryRegion;

/* 初始化一次；成功后其它 RuntimeWin32_* 函数才能使用。 */
int RuntimeWin32_Initialize(const GameProfile* profile);

/* 查询桥是否已经准备好。 */
int RuntimeWin32_IsReady(void);

/* 返回毫秒计时。GetTickCount 的 32 位回绕由调用者使用无符号减法处理。 */
unsigned long RuntimeWin32_TickCount(void);

/* 读取同目录 INI 的整数配置。 */
int RuntimeWin32_GetPrivateProfileInt(const char* section,
                                      const char* key,
                                      int fallback,
                                      const char* file_name);

/* 取得指定模块的完整路径。 */
unsigned long RuntimeWin32_GetModuleFileName(void* module, char* buffer, unsigned long capacity);

/* 构造“模块所在目录 + 文件名”。成功返回 1。 */
int RuntimeWin32_BuildSiblingPath(void* module,
                                  const char* file_name,
                                  char* output,
                                  unsigned long capacity);

/* 检查地址区间是否属于已提交且可读的内存。 */
int RuntimeWin32_IsReadable(unsigned long address, unsigned long size);

/* 安全复制游戏内存到插件缓冲区。 */
int RuntimeWin32_Read(unsigned long address, void* output, unsigned long size);

/* 取得包含 address 的内存区基本信息。 */
int RuntimeWin32_Query(unsigned long address, RuntimeMemoryRegion* region);

/* 为 trampoline 申请可读写执行内存。 */
void* RuntimeWin32_AllocateExecutable(unsigned long size);

/* 释放 RuntimeWin32_AllocateExecutable 得到的整块内存。 */
void RuntimeWin32_FreeExecutable(void* memory);

/* 临时改代码页保护、写入并刷新指令缓存。
 * 0=尚未写入；1=全部完成；2=字节已写入，但缓存刷新或保护恢复失败。
 * 返回 2 时必须保留相关 Hook 资源，不能误当成“目标完全没有修改”。 */
int RuntimeWin32_WriteCode(unsigned long address, const void* bytes, unsigned long size);

#endif
