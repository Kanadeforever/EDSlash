#ifndef BLADESWORD_QOL_X86_DETOUR_H
#define BLADESWORD_QOL_X86_DETOUR_H

/*
 * X86Detour 只处理已经人工确认过函数头的 32 位入口跳转。
 * 它不会反汇编指令，因此调用者必须提供完整指令边界的覆盖长度。
 */
typedef struct X86Detour {
    unsigned long target;
    unsigned long gateway;
    unsigned long overwrite_size;
    unsigned char original[16];
    /* 保存实际写入的完整跳转和填充字节，撤销时逐字节核对归属。 */
    unsigned char patch[16];
    int installed;
} X86Detour;

/*
 * 在 target 写入近跳转到 replacement，并返回可调用的 gateway。
 * overwrite_size 必须在 5~16 之间，而且不能截断相对跳转/调用指令。
 */
int X86Detour_Install(X86Detour* detour,
                      unsigned long target,
                      unsigned long replacement,
                      unsigned long overwrite_size);

/* 仅当完整目标字节仍属于本 Hook 时恢复原字节。
 * 成功返回 1；失败返回 0 并保留网关与安装状态，避免悬空原函数指针。 */
int X86Detour_Remove(X86Detour* detour);

#endif
