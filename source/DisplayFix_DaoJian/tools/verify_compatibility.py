# -*- coding: utf-8 -*-
"""
DisplayFix：ComeOn.exe 兼容性只读检查工具。

这个工具只读取 EXE，不写入任何字节，也不会生成补丁版 EXE。DisplayFix 不再用整个文件的
SHA-256 白名单锁版本，而是检查运行时真正依赖的机器码、调用关系和 vtable 结构是否仍然成立。

v0.3-test11 除了保留字体、动态分辨率、JMM、HUD、顶部按钮和输入链验证，还新增两项前端生命周期证据：

1. 从唯一的 0x00404D7A 风格分辨率派发签名向前 0x4A 字节，必须能验证到原版 0x00404D30
   风格显示模式函数头。test11 在主 HUD 真正出现后会复用这个函数，从原生前端 640x480/800x600
   切到 TargetWidth x BaseHeight；不能只靠“减一个地址常数”猜函数。
2. 主 HUD vtable +0x00 必须仍然指向已确认的 scalar deleting destructor 形状
   `56 8B F1 E8 ...`。test11 在 HUD 生命周期结束时通过这一槽恢复 FRONTEND profile，使返回菜单时
   原游戏自己的 640x480 请求不再被 GAMEPLAY profile 改写。

同时继续验证：
- 字体 DPI 原始/已修复状态；
- 两处分辨率映射与 JMM 选择器；
- 0x4087A0 -> 0x4B35F0 以及 test10 Steam delayed JMM 所需的资源路径构造链；
- 主 HUD +0x24 的真实 0x0B/0x0E 窗口分支与 +0x58 布局关系；
- test8 世界输入 0x4060EB -> 0x473F10；
- 全局 release 0x406086 -> 0x4B4560。

工具只报告兼容/不兼容，不运行游戏。代码注释故意写得很细，方便以后减少重复反编译。
"""

from __future__ import annotations

from pathlib import Path
import hashlib
import struct
import sys


# -------------------------------------------------------------------------------------------------
# 1. 已确认的机器码签名
# -------------------------------------------------------------------------------------------------

# 字体路径：未修复状态。
FONT_ORIGINAL = bytes.fromhex(
    "6A 48 6A 5A 57 8B F0 "
    "FF 15 5C 80 52 00 "
    "50 6A 01 8B CE"
)

# 字体路径：已经把 GetDeviceCaps 替换成固定 96 DPI 后的状态。
FONT_PATCHED = bytes.fromhex(
    "6A 48 6A 5A 57 8B F0 "
    "83 C4 08 6A 60 58 "
    "50 6A 01 8B CE"
)

# 0x404D7A 一带的分辨率模式派发。
# 内部/隐藏分支的宽高可能已经被别的宽屏补丁改过，所以那 8 个字节用 ? 通配。
RES_MODE = bytes([
    0x83, 0xE8, 0x04, 0x74, 0x32, 0x48, 0x74, 0x19, 0x48, 0x75, 0x2C,
    0xC7, 0x86, 0x28, 0x02, 0x00, 0x00, 0, 0, 0, 0,
    0xC7, 0x86, 0x2C, 0x02, 0x00, 0x00, 0, 0, 0, 0,
    0xEB, 0x2A,
    0xC7, 0x86, 0x28, 0x02, 0x00, 0x00, 0x20, 0x03, 0x00, 0x00,
    0xC7, 0x86, 0x2C, 0x02, 0x00, 0x00, 0x58, 0x02, 0x00, 0x00,
    0xEB, 0x14,
    0xC7, 0x86, 0x28, 0x02, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00,
    0xC7, 0x86, 0x2C, 0x02, 0x00, 0x00, 0xE0, 0x01, 0x00, 0x00,
    0x6A, 0x00, 0x6A, 0x00,
])
RES_MODE_MASK = "xxxxxxxxxxxxxxxxx????xxxxxx????" + "x" * (len(RES_MODE) - 31)

# 0x470C3E 一带第一处分辨率映射。
# 从 cmp 640 开始覆盖到 640x431 原生分支，这样验证器和 ASI 都能处理
# “目标宽度恰好等于 640/800”时需要避开原生分支的边界情况。
RES_MAP1 = bytes([
    0x3D, 0x80, 0x02, 0x00, 0x00, 0x74, 0x23,
    0x3D, 0x20, 0x03, 0x00, 0x00, 0x74, 0x10,
    0x3D, 0, 0, 0, 0, 0x75, 0x1F,
    0x8B, 0xF0,
    0xBF, 0, 0, 0, 0, 0xEB, 0x16,
    0xBE, 0x20, 0x03, 0x00, 0x00,
    0xBF, 0x58, 0x02, 0x00, 0x00,
    0xEB, 0x0A,
    0xBE, 0x80, 0x02, 0x00, 0x00,
    0xBF, 0xAF, 0x01, 0x00, 0x00,
])
RES_MAP1_MASK = "xxxxxxxxxxxxxxx????xxxxx????xxxxxxxxxxxxxxxxxxxxxxxx"

# 0x47B0AB 一带第二处分辨率映射。
# 同样包含 640 / 800 / 内部三条比较和三个分支的稳定上下文。
RES_MAP2 = bytes([
    0x3D, 0x80, 0x02, 0x00, 0x00, 0x74, 0x26,
    0x3D, 0x20, 0x03, 0x00, 0x00, 0x74, 0x07,
    0x3D, 0, 0, 0, 0, 0x74, 0x0C,
    0x68, 0x58, 0x02, 0x00, 0x00,
    0x68, 0x20, 0x03, 0x00, 0x00,
    0xEB, 0x16,
    0x68, 0, 0, 0, 0,
    0x68, 0, 0, 0, 0,
    0xEB, 0x0A,
    0x68, 0xAF, 0x01, 0x00, 0x00,
    0x68, 0x80, 0x02, 0x00, 0x00,
])
RES_MAP2_MASK = "xxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????x????xxxxxxxxxxxx"

# 0x4B363B 一带 JMM 布局文件选择器。
# JMMDL.txt 的绝对字符串地址用 ? 通配，避免把资源地址误当成兼容门槛。
JMM_LAYOUT = bytes([
    0x81, 0xFD, 0x80, 0x02, 0x00, 0x00,
    0xF3, 0xA4,
    0x75, 0x0B,
    0x8D, 0x54, 0x24, 0x10,
    0xBF, 0, 0, 0, 0,
    0xEB, 0x19,
    0x81, 0xFD, 0x20, 0x03, 0x00, 0x00,
    0x74, 0x08,
    0x81, 0xFD, 0x00, 0x04, 0x00, 0x00,
    0x75, 0x42,
])
JMM_LAYOUT_MASK = "xxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx"

# 0x004087A0：从分辨率对象读取 +0x228 Width / +0x22C Height，
# 然后调用 JMM/UI 管理器 0x4B35F0 的包装函数。
# GUI 管理器绝对地址和 E8 rel32 都通配。test3~test5 曾实验性 hook 这条 CALL；
# v0.3-test8 已停止安装该运行时 hook，但仍验证结构并保留研究证据，供 Steam 专项后续使用。
JMM_APPLY_CALLSITE = bytes([
    0x8B, 0x81, 0x2C, 0x02, 0x00, 0x00,
    0x8B, 0x89, 0x28, 0x02, 0x00, 0x00,
    0x50, 0x51, 0x6A, 0x00,
    0xB9, 0, 0, 0, 0,
    0xE8, 0, 0, 0, 0,
    0xC3,
])
JMM_APPLY_CALLSITE_MASK = "xxxxxxxxxxxxxxxxx????x????x"

# 0x004B35F0 函数头。调用点解析出来的目标必须以这一串开始。
JMM_LOAD_FUNCTION_HEAD = bytes.fromhex("81 EC 00 01 00 00 53 55 56 57 68")


# 0x004060D6 一带：UI manager 的按下分派已经返回 0，主循环准备把同一次按下交给世界输入。
# test8 只改 0x004060EB -> 0x00473F10 这一条 CALL；0x004B44F0 完全保持原版。
WORLD_MOUSE_PRESS_CALLSITE = bytes([
    0x8B, 0x0D, 0, 0, 0, 0,
    0x3B, 0xCF,
    0x74, 0x10,
    0x8B, 0x54, 0x24, 0x10,
    0x8B, 0x44, 0x24, 0x0C,
    0x52, 0x50, 0x53,
    0xE8, 0, 0, 0, 0,
    0x89, 0x1D, 0, 0, 0, 0,
])
WORLD_MOUSE_PRESS_CALLSITE_MASK = "xx????xxxxxxxxxxxxxxxx????xx????"


# 0x00406070 一带：鼠标释放时把 event_type=1 / x / y 交给 UI manager 的调用点。
GLOBAL_MOUSE_RELEASE_CALLSITE = bytes([
    0x8B, 0x54, 0x24, 0x10,
    0x8B, 0x44, 0x24, 0x0C,
    0x52, 0x50, 0x53,
    0xB9, 0, 0, 0, 0,
    0x89, 0x3D, 0, 0, 0, 0,
    0xE8, 0, 0, 0, 0,
    0x85, 0xC0, 0x75, 0x1A,
])
GLOBAL_MOUSE_RELEASE_CALLSITE_MASK = "xxxxxxxxxxxx????xx????x????xxxx"


# 0x4C2AE3 一带主 HUD 根类构造签名。
MAIN_HUD_ROOT = bytes([
    0xC7, 0x06, 0, 0, 0, 0,
    0x83, 0xC8, 0xFF,
    0x89, 0x35, 0, 0, 0, 0,
    0x89, 0x86, 0xC0, 0x00, 0x00, 0x00,
    0x89, 0xBE, 0x24, 0x01, 0x00, 0x00,
    0x89, 0x86, 0x28, 0x01, 0x00, 0x00,
    0x89, 0x86, 0x2C, 0x01, 0x00, 0x00,
])
MAIN_HUD_ROOT_MASK = "xx????xxxxx????" + "x" * (len(MAIN_HUD_ROOT) - 15)

# 0x4B2B90 通用 JMM 布局函数函数头。
UI_LAYOUT = bytes([
    0x8B, 0x44, 0x24, 0x04,
    0x56,
    0x83, 0xF8, 0xFF,
    0x57,
    0x8B, 0xF1,
    0x74, 0x0C,
    0x89, 0x46, 0x14,
    0x8B, 0x44, 0x24, 0x10,
    0xE9, 0, 0, 0, 0,
])
UI_LAYOUT_MASK = "x" * 21 + "?" * 4


# -------------------------------------------------------------------------------------------------
# 2. 最小 PE32 解析
# -------------------------------------------------------------------------------------------------

def u16(data: bytes, offset: int) -> int:
    """从指定位置读取一个 16 位小端无符号整数。"""

    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    """从指定位置读取一个 32 位小端无符号整数。"""

    return struct.unpack_from("<I", data, offset)[0]


def rel32_target(instruction_va: int, data: bytes, instruction_file_offset: int) -> int:
    """
    解析 E8/E9 rel32 的目标 VA。

    instruction_va 是 opcode 所在的内存地址；位移从 opcode 后 1 字节读取，
    x86 计算规则是：下一条指令地址 + 有符号 displacement。
    """

    displacement = struct.unpack_from("<i", data, instruction_file_offset + 1)[0]
    return (instruction_va + 5 + displacement) & 0xFFFFFFFF


def parse_pe(data: bytes) -> tuple[int, list[tuple[str, int, int, int, int]]]:
    """
    返回 (ImageBase, sections)。

    sections 中每一项是：
        (节名, VirtualAddress, VirtualSize, RawSize, RawPointer)
    """

    if len(data) < 0x100 or data[:2] != b"MZ":
        raise RuntimeError("不是有效的 MZ/PE 文件。")

    pe = u32(data, 0x3C)
    if pe + 0x18 >= len(data) or data[pe : pe + 4] != b"PE\0\0":
        raise RuntimeError("找不到有效的 PE\\0\\0 Header。")

    coff = pe + 4
    machine = u16(data, coff)
    if machine != 0x014C:
        raise RuntimeError(f"Machine=0x{machine:04X}，不是 i386/Win32。")

    section_count = u16(data, coff + 2)
    optional_size = u16(data, coff + 16)
    optional = coff + 20

    if u16(data, optional) != 0x010B:
        raise RuntimeError("目标不是 PE32。")

    image_base = u32(data, optional + 28)
    section_table = optional + optional_size

    sections: list[tuple[str, int, int, int, int]] = []

    for index in range(section_count):
        one = section_table + index * 40
        name = data[one : one + 8].split(b"\0", 1)[0].decode("ascii", errors="replace")
        virtual_size = u32(data, one + 8)
        virtual_address = u32(data, one + 12)
        raw_size = u32(data, one + 16)
        raw_pointer = u32(data, one + 20)
        sections.append((name, virtual_address, virtual_size, raw_size, raw_pointer))

    return image_base, sections


def va_to_file_offset(
    va: int,
    image_base: int,
    sections: list[tuple[str, int, int, int, int]],
) -> int:
    """把内存里的绝对 VA 转换成 EXE 文件偏移。"""

    rva = va - image_base

    for _name, section_rva, virtual_size, raw_size, raw_pointer in sections:
        # max(VirtualSize, RawSize) 可以兼容文件和装载后映像大小略有差异的节。
        covered = max(virtual_size, raw_size)
        if section_rva <= rva < section_rva + covered:
            return raw_pointer + (rva - section_rva)

    raise RuntimeError(f"VA 0x{va:08X} 不属于任何已知 PE Section。")


# -------------------------------------------------------------------------------------------------
# 3. 通配签名搜索
# -------------------------------------------------------------------------------------------------

def find_masked(data: bytes, pattern: bytes, mask: str) -> list[int]:
    """返回所有匹配文件偏移；mask 中 x=必须相同，?=任意字节。"""

    if len(pattern) != len(mask):
        raise RuntimeError("内部错误：pattern 和 mask 长度不一致。")

    hits: list[int] = []

    for start in range(0, len(data) - len(pattern) + 1):
        for index, expected in enumerate(pattern):
            if mask[index] == "x" and data[start + index] != expected:
                break
        else:
            hits.append(start)

    return hits


def require_unique(name: str, hits: list[int]) -> int:
    """要求签名恰好命中一次，并返回唯一文件偏移。"""

    if len(hits) != 1:
        raise RuntimeError(f"{name} 命中次数={len(hits)}，预期必须等于 1。")
    return hits[0]


# -------------------------------------------------------------------------------------------------
# 4. 单文件验证
# -------------------------------------------------------------------------------------------------

def verify_one(path: Path) -> list[str]:
    """验证一个 EXE；成功时返回适合打印的结论列表。"""

    data = path.read_bytes()
    image_base, sections = parse_pe(data)

    # DisplayFix 运行时只扫描 .text，所以离线工具也只在 .text 里检查这些代码签名。
    try:
        text = next(item for item in sections if item[0] == ".text")
    except StopIteration as exc:
        raise RuntimeError("找不到 .text Section。") from exc

    _name, text_rva, _virtual_size, raw_size, raw_pointer = text
    text_data = data[raw_pointer : raw_pointer + raw_size]

    original_count = data.count(FONT_ORIGINAL)
    patched_count = data.count(FONT_PATCHED)
    if original_count == 1 and patched_count == 0:
        font_state = "原始字体路径，可由 ASI 运行时修复"
    elif original_count == 0 and patched_count == 1:
        font_state = "字体路径已经是 96 DPI 修复状态，ASI 会识别并跳过重复写入"
    else:
        raise RuntimeError(
            f"字体签名状态不唯一：original={original_count}, patched={patched_count}。"
        )

    mode_off = require_unique("分辨率模式派发", find_masked(text_data, RES_MODE, RES_MODE_MASK))

    # test11 需要在“主 HUD 真正出现后”复用原版 SetDisplayMode 包装函数，把前端 640x480 切成
    # DisplayFix 的游戏内目标。因此除了模式派发签名唯一，还必须验证 mode_off 前 0x4A 字节确实
    # 是 0x404D30 风格函数头，避免仅凭固定差值误认函数。
    if mode_off < 0x4A:
        raise RuntimeError("分辨率模式派发前空间不足，无法解析原版显示模式函数。")
    display_mode_off = mode_off - 0x4A
    display_head = text_data[display_mode_off : display_mode_off + 18]
    if len(display_head) < 18 or not (
        display_head[0:9] == bytes.fromhex("64 A1 00 00 00 00 6A FF 68")
        and display_head[13:18] == bytes.fromhex("50 8B 44 24 10")
    ):
        raise RuntimeError("分辨率模式派发没有位于已确认的原版 SetDisplayMode 包装函数内。")
    display_mode_va = image_base + text_rva + display_mode_off

    map1_off = require_unique("第一处分辨率映射", find_masked(text_data, RES_MAP1, RES_MAP1_MASK))
    map2_off = require_unique("第二处分辨率映射", find_masked(text_data, RES_MAP2, RES_MAP2_MASK))
    jmm_off = require_unique("JMM 布局选择器", find_masked(text_data, JMM_LAYOUT, JMM_LAYOUT_MASK))
    jmm_apply_off = require_unique(
        "GUI/JMM 分辨率应用包装函数",
        find_masked(text_data, JMM_APPLY_CALLSITE, JMM_APPLY_CALLSITE_MASK),
    )
    hud_off = require_unique("主 HUD 根类构造", find_masked(text_data, MAIN_HUD_ROOT, MAIN_HUD_ROOT_MASK))
    layout_off = require_unique("JMM 通用布局函数", find_masked(text_data, UI_LAYOUT, UI_LAYOUT_MASK))

    # 把 .text 内文件偏移换算成真正 VA，方便核对 vtable 指针关系。
    hud_va = image_base + text_rva + hud_off
    layout_va = image_base + text_rva + layout_off

    # 主 HUD 构造签名的 C7 06 后 4 字节就是 vtable 的绝对 VA。
    vtable_va = u32(text_data, hud_off + 2)

    # ---------------------------------------------------------------------------------------------
    # 0x4087A0 风格 GUI/JMM 分辨率应用包装函数。
    #
    # v0.3-test11 继续把这条包装函数作为 Steam delayed JMM apply 的关键结构证据：
    #   1. 先要求整个包装函数签名唯一；
    #   2. 包装函数 +16 必须仍能解析出 UI manager，+21 必须是 E8；
    #   3. 解码 rel32 后，目标必须落在 PE 中并匹配 0x4B35F0 已确认函数头；
    #   4. 0x4B35F0 +0x1E 还必须 call 到已确认的资源路径构造函数 0x4EB9E0 风格函数头，
    #      因为 test10 要从其中解析游戏资源根目录缓冲区，避免过早重放 JMM。
    # test10 仍然不改写 0x4087B5 这条 CALL；非 Steam 自然 JMM apply 保持原版。
    # ---------------------------------------------------------------------------------------------
    jmm_apply_va = image_base + text_rva + jmm_apply_off
    jmm_call_va = jmm_apply_va + 21
    jmm_call_file_offset = raw_pointer + jmm_apply_off + 21

    if data[jmm_call_file_offset] != 0xE8:
        raise RuntimeError("GUI/JMM 分辨率应用包装函数 +21 不是 E8 CALL。")

    jmm_load_va = rel32_target(jmm_call_va, data, jmm_call_file_offset)
    jmm_load_file_offset = va_to_file_offset(jmm_load_va, image_base, sections)
    if data[jmm_load_file_offset : jmm_load_file_offset + len(JMM_LOAD_FUNCTION_HEAD)] != JMM_LOAD_FUNCTION_HEAD:
        raise RuntimeError(
            "GUI/JMM 分辨率应用 CALL 没有指向已确认的 JMM/UI 广播函数头："
            f"target=0x{jmm_load_va:08X}。"
        )

    # test10 新增：0x4B35F0 +0x1E 必须是 E8，目标函数头应为
    # `56 57 BF <root-buffer> 83 C9 FF 33 C0 ...`。BF 的 imm32 是游戏资源根目录缓冲区。
    jmm_path_call_va = jmm_load_va + 0x1E
    jmm_path_call_file = va_to_file_offset(jmm_path_call_va, image_base, sections)
    if data[jmm_path_call_file] != 0xE8:
        raise RuntimeError("JMM/UI 函数 +0x1E 不是资源路径构造 E8 CALL。")

    jmm_path_builder_va = rel32_target(jmm_path_call_va, data, jmm_path_call_file)
    jmm_path_builder_file = va_to_file_offset(jmm_path_builder_va, image_base, sections)
    path_head = data[jmm_path_builder_file : jmm_path_builder_file + 12]
    if len(path_head) < 12 or not (
        path_head[0:3] == bytes.fromhex("56 57 BF")
        and path_head[7:12] == bytes.fromhex("83 C9 FF 33 C0")
    ):
        raise RuntimeError(
            f"JMM 资源路径构造函数 0x{jmm_path_builder_va:08X} 不符合已确认函数头。"
        )

    resource_root_va = int.from_bytes(path_head[3:7], "little")
    if not (0x00400000 <= resource_root_va < 0x00600000):
        raise RuntimeError(
            f"解析出的 JMM 资源根目录缓冲区地址异常：0x{resource_root_va:08X}。"
        )

    # ---------------------------------------------------------------------------------------------
    # v0.3-test8 世界鼠标按下 callsite。
    #
    # test7 曾包装更早的 0x4B44F0，但实机导致普通地图左键与 Alt+F4 一起失效；反汇编确认
    # 0x4B44F0 依赖调用者保留下来的 ESI。test8 因此只验证/修改 0x4060EB -> 0x473F10。
    # ---------------------------------------------------------------------------------------------
    world_press_off = require_unique(
        "世界鼠标按下 callsite",
        find_masked(text_data, WORLD_MOUSE_PRESS_CALLSITE, WORLD_MOUSE_PRESS_CALLSITE_MASK),
    )
    world_press_va = image_base + text_rva + world_press_off
    world_press_call_file = raw_pointer + world_press_off + 21
    world_press_call_va = world_press_va + 21
    if data[world_press_call_file] != 0xE8:
        raise RuntimeError("世界鼠标按下 callsite +21 不是 E8 CALL。")
    world_press_target_va = rel32_target(world_press_call_va, data, world_press_call_file)
    world_press_target_file = va_to_file_offset(world_press_target_va, image_base, sections)
    world_press_head = data[world_press_target_file : world_press_target_file + 26]
    if len(world_press_head) < 26 or not (
        world_press_head[0:9] == bytes.fromhex("64 A1 00 00 00 00 6A FF 68")
        and world_press_head[13:15] == bytes.fromhex("50 A1")
        and world_press_head[19:26] == bytes.fromhex("64 89 25 00 00 00 00")
    ):
        raise RuntimeError(
            f"世界鼠标按下 CALL 目标 0x{world_press_target_va:08X} 不符合 0x473F10 已确认函数头。"
        )

    # ---------------------------------------------------------------------------------------------
    # v0.3-test8 全局鼠标释放 callsite。
    # ---------------------------------------------------------------------------------------------
    global_release_off = require_unique(
        "全局鼠标释放 callsite",
        find_masked(text_data, GLOBAL_MOUSE_RELEASE_CALLSITE, GLOBAL_MOUSE_RELEASE_CALLSITE_MASK),
    )
    global_release_va = image_base + text_rva + global_release_off
    global_release_call_file = raw_pointer + global_release_off + 22
    global_release_call_va = global_release_va + 22
    if data[global_release_call_file] != 0xE8:
        raise RuntimeError("全局鼠标释放 callsite +22 不是 E8 CALL。")
    global_release_target_va = rel32_target(global_release_call_va, data, global_release_call_file)
    global_release_target_file = va_to_file_offset(global_release_target_va, image_base, sections)
    global_release_head = data[global_release_target_file : global_release_target_file + 18]
    if len(global_release_head) < 18 or not (
        global_release_head[0:7] == bytes.fromhex("56 57 8B F1 33 FF E8")
        and global_release_head[11:18] == bytes.fromhex("85 C0 74 1C 8B 54 24")
    ):
        raise RuntimeError(
            f"全局鼠标释放 CALL 目标 0x{global_release_target_va:08X} 不符合 0x4B4560 已确认函数头。"
        )

    # ---------------------------------------------------------------------------------------------
    # vtable +0x00：主 HUD scalar deleting destructor。
    #
    # test11 用它把运行时代码 profile 从 GAMEPLAY 恢复为 FRONTEND，保证返回主菜单/动画后
    # 原游戏下一次 640x480 请求不会继续被 TargetWidth/TargetHeight 截走。
    # 原版函数头必须是 push esi / mov esi,ecx / call <real dtor>。
    # ---------------------------------------------------------------------------------------------
    destructor_slot_va = vtable_va + 0x00
    destructor_slot_file_offset = va_to_file_offset(destructor_slot_va, image_base, sections)
    destructor_slot_target = u32(data, destructor_slot_file_offset)
    destructor_target_file_offset = va_to_file_offset(destructor_slot_target, image_base, sections)
    destructor_head = data[destructor_target_file_offset : destructor_target_file_offset + 4]
    if destructor_head != bytes.fromhex("56 8B F1 E8"):
        raise RuntimeError(
            "主 HUD vtable +0x00 不符合已确认 scalar deleting destructor 形状："
            f"target=0x{destructor_slot_target:08X}。"
        )

    # ---------------------------------------------------------------------------------------------
    # vtable +0x58：主 HUD 的布局函数。
    #
    # v0.2-test1 就依赖这一槽完成“只移动底部主 HUD，不碰小地图和右侧按钮”。
    # 所以这里继续要求它必须精确指向我们在 .text 中唯一找到的 JMM 通用布局函数。
    # ---------------------------------------------------------------------------------------------
    layout_slot_va = vtable_va + 0x58
    layout_slot_file_offset = va_to_file_offset(layout_slot_va, image_base, sections)
    layout_slot_target = u32(data, layout_slot_file_offset)

    if layout_slot_target != layout_va:
        raise RuntimeError(
            "主 HUD vtable +0x58 没有指向唯一 JMM 通用布局函数："
            f"slot=0x{layout_slot_target:08X}, layout=0x{layout_va:08X}。"
        )

    # ---------------------------------------------------------------------------------------------
    # vtable +0x24：主 HUD 真正的鼠标释放/事件分派函数。
    #
    # v0.3-test4 的实机 A/B 日志已经闭合：用户点击顶部两个圆形按钮时，原版 self+0xA8
    # 真正命中的 control ID 是 0x0B 和 0x0E，不是此前误判的 0x0F / 0x10。
    #
    # 原版两条分支结构分别是：
    #   ID 0x0E -> mov ecx,[target_global] -> call 0x4B1DB0 -> mov ecx,[同一 global]
    #            -> !active -> call [vtable+0x1C]
    #   ID 0x0B -> 同样结构，使用自己的 target_global。
    #
    # test5 的兜底只会复用目标对象自身 vtable+0x1C，所以这里必须把这两条原版分支验证清楚。
    # ---------------------------------------------------------------------------------------------
    event_slot_va = vtable_va + 0x24
    event_slot_file_offset = va_to_file_offset(event_slot_va, image_base, sections)
    event_slot_target = u32(data, event_slot_file_offset)
    event_target_file_offset = va_to_file_offset(event_slot_target, image_base, sections)

    if event_target_file_offset + 0x1DE > len(data):
        raise RuntimeError("主 HUD vtable +0x24 指向的事件函数超出文件范围。")

    event_code = data[event_target_file_offset : event_target_file_offset + 0x1DE]
    event_shape_ok = (
        event_code[0x19:0x1C] == bytes([0xFF, 0x50, 0x30])
        and event_code[0xD0:0xD6] == bytes([0x8B, 0xBF, 0xA8, 0x00, 0x00, 0x00])
        and event_code[0xDE:0xE1] == bytes([0x8B, 0x47, 0x28])
        and event_code[0x113:0x116] == bytes([0x83, 0xF8, 0x0E])
        and event_code[0x118:0x11A] == bytes([0x8B, 0x0D])
        and event_code[0x122] == 0xE8
        and event_code[0x127:0x129] == bytes([0x8B, 0x0D])
        and event_code[0x133:0x136] == bytes([0xFF, 0x56, 0x1C])
        and event_code[0x1BB:0x1BE] == bytes([0x83, 0xF8, 0x0B])
        and event_code[0x1C0:0x1C2] == bytes([0x8B, 0x0D])
        and event_code[0x1CA] == 0xE8
        and event_code[0x1CF:0x1D1] == bytes([0x8B, 0x0D])
        and event_code[0x1DB:0x1DE] == bytes([0xFF, 0x56, 0x1C])
    )

    if not event_shape_ok:
        raise RuntimeError(
            "主 HUD vtable +0x24 的 0x0B/0x0E 原版窗口开关结构不符合已确认版本，"
            f"target=0x{event_slot_target:08X}。"
        )

    target_global_0e_a = u32(event_code, 0x11A)
    target_global_0e_b = u32(event_code, 0x129)
    target_global_0b_a = u32(event_code, 0x1C2)
    target_global_0b_b = u32(event_code, 0x1D1)

    if target_global_0e_a != target_global_0e_b:
        raise RuntimeError("ID 0x0E 分支前后读取的目标窗口全局槽不同。")
    if target_global_0b_a != target_global_0b_b:
        raise RuntimeError("ID 0x0B 分支前后读取的目标窗口全局槽不同。")

    active_query_0e = rel32_target(
        event_slot_target + 0x122,
        data,
        event_target_file_offset + 0x122,
    )
    active_query_0b = rel32_target(
        event_slot_target + 0x1CA,
        data,
        event_target_file_offset + 0x1CA,
    )
    if active_query_0e != active_query_0b:
        raise RuntimeError("ID 0x0B 与 0x0E 分支使用的 active 查询函数不同。")


    # 读出当前内部/隐藏分支宽高，便于识别这是原版还是哪一种外部宽屏改版。
    hidden_width = u32(text_data, mode_off + 17)
    hidden_height = u32(text_data, mode_off + 27)

    # 显示各关键签名的 VA，而不是只有“通过”两个字；接档时更容易人工复核。
    return [
        f"SHA-256={hashlib.sha256(data).hexdigest()}",
        font_state,
        f"分辨率模式派发 VA=0x{image_base + text_rva + mode_off:08X}",
        f"原版显示模式函数 VA=0x{display_mode_va:08X}（test11 前端/游戏内 profile 生命周期切换）",
        f"当前内部/扩展分支={hidden_width}x{hidden_height}",
        f"第一处分辨率映射 VA=0x{image_base + text_rva + map1_off:08X}",
        f"第二处分辨率映射 VA=0x{image_base + text_rva + map2_off:08X}",
        f"JMM 布局选择器 VA=0x{image_base + text_rva + jmm_off:08X}",
        f"GUI/JMM 分辨率应用包装 VA=0x{jmm_apply_va:08X} -> JMM/UI 广播 0x{jmm_load_va:08X}（test10 Steam delayed JMM 上下文）",
        f"JMM 资源路径构造 VA=0x{jmm_path_builder_va:08X} -> 资源根缓冲区 0x{resource_root_va:08X}",
        f"世界鼠标按下 callsite VA=0x{world_press_va:08X} -> 0x{world_press_target_va:08X}（仅 0x0B/0x0E 点击穿透保护）",
        f"全局鼠标释放 callsite VA=0x{global_release_va:08X} -> 0x{global_release_target_va:08X}（顶部窗口 fallback）",
        f"主 HUD 根类构造 VA=0x{hud_va:08X}",
        f"主 HUD vtable=0x{vtable_va:08X}",
        f"vtable+0x00 -> 0x{destructor_slot_target:08X}（test11 前端 profile 恢复点）",
        f"vtable+0x24 -> 0x{event_slot_target:08X}（顶部按钮 0x0B/0x0E 原版窗口开关已验证）",
        f"ID 0x0B target global=0x{target_global_0b_a:08X}",
        f"ID 0x0E target global=0x{target_global_0e_a:08X}",
        f"两分支 active query=0x{active_query_0b:08X}，最终均调用各目标对象 vtable+0x1C",
        f"vtable+0x58 -> 0x{layout_slot_target:08X}（已和通用布局函数交叉验证）",
        "vtable+0x30 不再被 DisplayFix hook（此前三种 self+0xA8/hit-test 修补均已实机失败）",
    ]


# -------------------------------------------------------------------------------------------------
# 5. 命令行入口
# -------------------------------------------------------------------------------------------------

def main() -> int:
    """接受一个或多个 EXE；全部通过返回 0，有任何一个失败返回 1。"""

    if len(sys.argv) < 2:
        print("用法：python verify_compatibility.py <ComeOn.exe> [更多 ComeOn 改版.exe ...]")
        return 1

    failed = False

    for raw_path in sys.argv[1:]:
        path = Path(raw_path).resolve()
        print(f"\n[验证目标] {path}")

        try:
            lines = verify_one(path)
            for line in lines:
                print(f"[通过] {line}")
        except Exception as exc:
            failed = True
            print(f"[失败] {exc}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
