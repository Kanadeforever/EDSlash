#include "GameProfile.h"

/*
 * 两个已确认游戏的 PE32 基本身份。
 *
 * 为什么 v0.1-dev1 先用 SizeOfImage + EntryPoint：
 *   - Steam / 非 Steam 样本这两个值一致；
 *   - 历史宽屏改版只改游戏代码立即数，不改变 PE 入口和映像尺寸；
 *   - 这一步发生在任何 Profile 专用 IAT 地址被访问之前，所以必须完全不依赖 Win32 API。
 *
 * 后端初始化以后仍会继续执行它原有的大量内容签名验证，因此这里不是唯一安全检查；
 * 它只是防止“把外传后端拿去访问本体 IAT”这种最早期、最危险的误识别。
 */
static const GameProfile PROFILE_DAOJIAN = {
    GAME_ID_DAOJIAN,
    "刀剑封魔录",
    0x00173000ul,
    0x0010F0EFul
};

static const GameProfile PROFILE_WAIZHUAN = {
    GAME_ID_WAIZHUAN,
    "刀剑封魔录外传：上古传说",
    0x001A5000ul,
    0x00127BCFul
};

/* 小工具：从内存里按小端序读取 16 位数字。 */
static unsigned short read_u16(const unsigned char* p)
{
    return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

/* 小工具：从内存里按小端序读取 32 位数字。 */
static unsigned long read_u32(const unsigned char* p)
{
    return (unsigned long)p[0]
        | ((unsigned long)p[1] << 8)
        | ((unsigned long)p[2] << 16)
        | ((unsigned long)p[3] << 24);
}

const GameProfile* GameProfile_Detect(void)
{
    /*
     * 两款游戏的 PE ImageBase 都固定为 0x00400000，并且当前支持样本没有 ASLR。
     * 因为这个函数运行在游戏已经正常启动、ASI 已经被 LoadLibrary 之后，所以这里可以直接读取主 EXE 映像。
     */
    const unsigned char* image = (const unsigned char*)0x00400000ul;
    unsigned long pe_offset;
    const unsigned char* pe;
    const unsigned char* file_header;
    const unsigned char* optional_header;
    unsigned short machine;
    unsigned short optional_magic;
    unsigned long entry_point;
    unsigned long image_size;

    /* DOS 头开头必须是 ASCII 的 MZ。不是就绝不能继续解引用后续偏移。 */
    if (image[0] != 'M' || image[1] != 'Z') {
        return (const GameProfile*)0;
    }

    /* DOS+0x3C 保存 PE 头相对映像基址的偏移。老 PE 通常在很前面，再加一个保守上限防止坏数据。 */
    pe_offset = read_u32(image + 0x3Cul);
    if (pe_offset < 0x40ul || pe_offset > 0x1000ul) {
        return (const GameProfile*)0;
    }

    pe = image + pe_offset;
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0) {
        return (const GameProfile*)0;
    }

    file_header = pe + 4ul;
    machine = read_u16(file_header + 0ul);
    if (machine != 0x014Cu) {
        /* 0x014C 就是 Intel i386。项目只支持原游戏的 Win32/x86。 */
        return (const GameProfile*)0;
    }

    optional_header = pe + 24ul;
    optional_magic = read_u16(optional_header + 0ul);
    if (optional_magic != 0x010Bu) {
        /* 0x010B 表示 PE32；PE32+ / x64 必须拒绝。 */
        return (const GameProfile*)0;
    }

    /* PE32 OptionalHeader+0x10 是 AddressOfEntryPoint，+0x38 是 SizeOfImage。 */
    entry_point = read_u32(optional_header + 0x10ul);
    image_size = read_u32(optional_header + 0x38ul);

    if (entry_point == PROFILE_DAOJIAN.entry_point_rva && image_size == PROFILE_DAOJIAN.image_size) {
        return &PROFILE_DAOJIAN;
    }
    if (entry_point == PROFILE_WAIZHUAN.entry_point_rva && image_size == PROFILE_WAIZHUAN.image_size) {
        return &PROFILE_WAIZHUAN;
    }

    return (const GameProfile*)0;
}
