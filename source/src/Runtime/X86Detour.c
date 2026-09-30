#include "X86Detour.h"
#include "Win32Bridge.h"

static void copy_bytes(unsigned char* destination,
                       const unsigned char* source,
                       unsigned long count)
{
    unsigned long i;
    for (i = 0ul; i < count; ++i) {
        destination[i] = source[i];
    }
}

static void write_rel32(unsigned char* instruction,
                        unsigned long opcode_address,
                        unsigned long destination)
{
    signed long displacement;

    displacement = (signed long)(destination - (opcode_address + 5ul));
    instruction[0] = 0xE9u;
    instruction[1] = (unsigned char)(displacement & 0xFFl);
    instruction[2] = (unsigned char)((displacement >> 8) & 0xFFl);
    instruction[3] = (unsigned char)((displacement >> 16) & 0xFFl);
    instruction[4] = (unsigned char)((displacement >> 24) & 0xFFl);
}

int X86Detour_Install(X86Detour* detour,
                      unsigned long target,
                      unsigned long replacement,
                      unsigned long overwrite_size)
{
    unsigned char* gateway;
    unsigned char patch[16];
    unsigned long i;

    if (!detour || detour->installed) {
        return 0;
    }
    if (target == 0ul || replacement == 0ul || overwrite_size < 5ul || overwrite_size > 16ul) {
        return 0;
    }
    if (!RuntimeWin32_Read(target, detour->original, overwrite_size)) {
        return 0;
    }

    gateway = (unsigned char*)RuntimeWin32_AllocateExecutable(overwrite_size + 5ul);
    if (!gateway) {
        return 0;
    }

    copy_bytes(gateway, detour->original, overwrite_size);
    write_rel32(gateway + overwrite_size,
                (unsigned long)(gateway + overwrite_size),
                target + overwrite_size);

    for (i = 0ul; i < overwrite_size; ++i) {
        patch[i] = 0x90u;
    }
    write_rel32(patch, target, replacement);
    copy_bytes(detour->patch, patch, overwrite_size);

    if (!RuntimeWin32_WriteCode(target, patch, overwrite_size)) {
        RuntimeWin32_FreeExecutable(gateway);
        return 0;
    }

    detour->target = target;
    detour->gateway = (unsigned long)gateway;
    detour->overwrite_size = overwrite_size;
    detour->installed = 1;
    return 1;
}

int X86Detour_Remove(X86Detour* detour)
{
    unsigned char actual[16];
    unsigned long i;
    int matches_patch = 1;
    int matches_original = 1;

    if (!detour || !detour->installed) {
        return 1;
    }

    /* 撤销只用于初始化失败后的同步回滚，不支持运行中的线程热卸载。
     * 同时核对实际字节，不能把 HookManager 的声明当成物理内存仍属于自己的证明。 */
    /* 只检查 E9 无法分辨其它插件安装的跳转。完整字节不同就保留原样，
     * 不覆盖后来者，也不释放仍可能被其它跳转引用的网关。 */
    if (!RuntimeWin32_Read(detour->target, actual, detour->overwrite_size)) {
        return 0;
    }
    for (i = 0ul; i < detour->overwrite_size; ++i) {
        if (actual[i] != detour->patch[i]) matches_patch = 0;
        if (actual[i] != detour->original[i]) matches_original = 0;
    }
    if (!matches_patch && !matches_original) return 0;
    /* 上次恢复可能已经写回原字节，但缓存刷新失败。允许对此状态重新刷新，
     * 只有保护和缓存处理都完成才释放网关，防止处理器仍使用旧跳转。 */
    if (RuntimeWin32_WriteCode(detour->target, detour->original, detour->overwrite_size) != 1) {
        /* 写入前失败或写入后附加处理失败，都保留资源以便重试。 */
        return 0;
    }

    RuntimeWin32_FreeExecutable((void*)detour->gateway);
    detour->target = 0ul;
    detour->gateway = 0ul;
    detour->overwrite_size = 0ul;
    detour->installed = 0;
    return 1;
}
