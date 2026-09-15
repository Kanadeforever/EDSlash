# -*- coding: utf-8 -*-
"""
DisplayFix layer1d：外传 ComeOn.exe / Steam ComeOn.dll 兼容性只读检查工具。

这个工具只读取 EXE/DLL，不写入任何字节，也不会生成补丁文件。兼容判断依赖机器码、调用关系和 vtable
结构，而不是整个文件 SHA-256 白名单。

layer1d 从外传 v0.2.1 稳定运行基线重新开线。外传 Steam 两条已封版兼容路径是硬红线：
- ResJM.Lib 多语言 CreateFileA 低层调用点兜底必须继续存在；
- ComeOn.dll CreateWindowExA class-atom guard 必须继续保留官方 EDIT WndProc/OpenGL 路径。

同时继承本体同层级的深度结构验证：Strategy/JMM/HUD、0x09~0x0E 六路窗口、键盘快捷键、
0x0B 装备 / 0x0D 技能 / 0x0E 物品三个菜单类，以及 layer1d 所需的 UI manager Draw / 顶层链架构。

layer1d 不移动辅助 GUI，也不修改 child tree、GetCursorPos、self+0xA8、快捷键或 active；运行时不写顶层链；绘制使用 HUD 后延迟 Draw，输入只覆盖原版 picker 的单次返回值。
因此工具仍会验证 +0x30/self+0xA8 等原版输入结构，但这些结构只作为“输入必须保持原样”的兼容证据。
乾坤袋对象地址也只保留为逆向证据，本版不手工移动。
"""

from __future__ import annotations

from pathlib import Path
import hashlib
import struct
import sys


# -------------------------------------------------------------------------------------------------
# 1. 已确认的机器码签名
# -------------------------------------------------------------------------------------------------

# test4 证据：物品 control 0x17 切换的乾坤袋/辅助面板构造写槽指令。
# Steam / 非 Steam 用户样本完全一致。layer1d 运行时不改这个对象的坐标/业务，但兼容性验证仍确认“vtable + 写槽”存在，作为完整逆向接档证据。
BAG_AUX_PANEL_VTABLE = 0x0055395C
BAG_AUX_PANEL_SLOT = 0x0058CCB8
BAG_AUX_PANEL_CTOR_WRITE = bytes.fromhex("C7 06 5C 39 55 00 89 35 B8 CC 58 00")

# layer1d：UI manager Draw 包装函数。
# 机器码语义：mov eax,[ecx] / mov ecx,<manager> / push eax / call <manager Draw> / ret。
# manager 绝对地址和 E8 rel32 会因本体/外传及版本位置变化，所以对应字节使用通配。
UI_MANAGER_DRAW_WRAPPER = bytes([
    0x8B, 0x01,
    0xB9, 0, 0, 0, 0,
    0x50,
    0xE8, 0, 0, 0, 0,
    0xC3,
])
UI_MANAGER_DRAW_WRAPPER_MASK = "xxx????xx????x"

# layer1d：0x4B4790 风格顶层 root picker callsite。
# 语义：lea POINT / mov ecx,manager / push POINT / call picker / push result / call set-current-root / mov eax,[manager+0x40]。
UI_TOP_LEVEL_PICK_CALLSITE = bytes([
    0x8D,0x44,0x24,0x0C,
    0x8B,0xCE,
    0x50,
    0xC7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,
    0xE8,0,0,0,0,
    0x50,
    0x8B,0xCE,
    0xE8,0,0,0,0,
    0x8B,0x4C,0x24,0x14,
    0x8B,0x46,0x40,
])
UI_TOP_LEVEL_PICK_CALLSITE_MASK = "xxxxxxxxxxxxxxxx????xxxx????xxxxxxx"



# 字体路径：未修复状态。
FONT_ORIGINAL = bytes.fromhex(
    "6A 48 6A 5A 57 8B F0 "
    "FF 15 54 10 55 00 "
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

# v0.1-clean1：Steam 多语言兜底依赖的低层 CreateFileA 调用上下文。
# 关键指令是中间的 `FF 15 E4 11 55 00`，也就是通过游戏 IAT 0x005511E4 调用 CreateFileA。
# clean1 只把这一条 6 字节 CALL 等长改成 rel32 CALL + NOP，不修改 IAT 本身。
RESJM_CREATEFILE_CALL = bytes([
    0xFF,0x75,0xF0,
    0xFF,0x75,0xF4,
    0xFF,0x75,0x08,
    0xFF,0x15,0xE4,0x11,0x55,0x00,
    0x8B,0xF0,
    0x3B,0xF7,
    0x75,0x14,
    0xFF,0x15,0xF0,0x11,0x55,0x00,
])
RESJM_CREATEFILE_CALL_MASK = "x" * len(RESJM_CREATEFILE_CALL)

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
    0x75, 0x44,
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


# test13：0x00404A00 风格高层状态切换函数头，并覆盖“离开旧状态 3”的清理分支。
# jump table 地址、两个 E8 rel32 和日志函数 E8 都用 ? 通配。
STRATEGY_EXIT_CALLSITE = bytes([
    0x8B, 0xCE, 0xE8, 0, 0, 0, 0,
    0x8B, 0xCE, 0xE8, 0, 0, 0, 0,
    0x68, 0, 0, 0, 0,
    0x68, 0, 0, 0, 0,
    0xE8, 0, 0, 0, 0,
    0x83, 0xC4, 0x08, 0xEB, 0x19,
])
STRATEGY_EXIT_CALLSITE_MASK = "xxx????xxx????x????x????x????xxxxx"

STRATEGY_ENTER_CALLSITE = bytes([
    0x8B, 0xCE, 0xE8, 0, 0, 0, 0,
    0x68, 0, 0, 0, 0,
    0x68, 0, 0, 0, 0,
    0xE8, 0, 0, 0, 0,
    0x83, 0xC4, 0x08,
    0x8B, 0xCE, 0xE8, 0, 0, 0, 0,
    0x68, 0, 0, 0, 0,
    0x68, 0, 0, 0, 0,
    0xE8, 0, 0, 0, 0,
    0x83, 0xC4, 0x08,
])
STRATEGY_ENTER_CALLSITE_MASK = "xxx????x????x????x????xxxxxx????x????x????x????xxx"

# test15：原游戏启动前端的 mode 4 显示模式应用。
#
# 0x004053D8 一带已静态确认：
#   push ebx              ; 当前启动路径里 ebx=0，也就是 force=0
#   push 4                ; 原版标题/前端明确请求 mode 4
#   mov  ecx,ebp
#   mov  [ebp+0x08],4
#   call 0x00404D30
#
# E8 的 rel32 会随代码布局变化，所以 4 字节位移使用通配；其余 opcode/常量都要求完全一致。
FRONTEND_MODE4_APPLY = bytes([
    0x53,
    0x6A, 0x04,
    0x8B, 0xCD,
    0xC7, 0x45, 0x08, 0x04, 0x00, 0x00, 0x00,
    0xE8, 0x00, 0x00, 0x00, 0x00,
])
FRONTEND_MODE4_APPLY_MASK = "xxxxxxxxxxxxx????"


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


# test2：主 HUD 每帧调用原版键盘快捷键处理函数的上下文。
# 第三条 E8（签名起点 +26）在本体为 0x004C2F45 -> 0x004C42B0，
# 在外传为 0x004D7505 -> 0x004D8890。绝对地址不同，但周围对象字段访问完全同形。
HUD_KEYBOARD_SHORTCUT_CALLSITE = bytes([
    0x8B, 0x87, 0x24, 0x01, 0x00, 0x00,
    0x85, 0xC0,
    0x75, 0x07,
    0x8B, 0xCF,
    0xE8, 0, 0, 0, 0,
    0x8B, 0xCF,
    0xE8, 0, 0, 0, 0,
    0x8B, 0xCF,
    0xE8, 0, 0, 0, 0,
    0x83, 0xBF, 0xF4, 0x0B, 0x00, 0x00, 0xFF,
    0x74, 0x22,
    0x8B, 0x97, 0xF8, 0x0B, 0x00, 0x00,
    0x42,
])
HUD_KEYBOARD_SHORTCUT_CALLSITE_MASK = "xxxxxxxxxxxxx????xxx????xxx????xxxxxxxxxxxxxxxx"


# test5：继续验证原版通用 child hit-test 函数开头；当前运行时明确不再 Hook 它。
#
# 历史 test1~test7 曾平移顶层物品/装备/技能窗口；下面保留相关输入机器码只用于证明原版命中结构仍存在，layer1d 不再平移窗口。
# 原版通用命中函数会在内部单独调用 GetCursorPos，再直接用得到的屏幕 X/Y 和 child +0x14/+0x18 比较。
# test4 曾改这一处 6 字节 `FF 15 <GetCursorPos_IAT>`，但实机证明方向错误；test5 只验证它仍是原版 `FF 15`，绝不写入。
# 全局 GetCursorPos IAT、顶层 press 分派和世界点击全部保持原版。
UI_CHILD_HITTEST_CURSOR = bytes([
    0x83,0xEC,0x08,
    0x8D,0x44,0x24,0x00,
    0x53,0x55,0x56,0x57,
    0x8B,0xF9,
    0x50,
    0x8B,0xB7,0x9C,0x00,0x00,0x00,
    0x89,0xB7,0xA0,0x00,0x00,0x00,
    0xFF,0x15,0,0,0,0,
    0x8B,0x5C,0x24,0x10,
    0x8B,0x6C,0x24,0x14,
])
UI_CHILD_HITTEST_CURSOR_MASK = "xxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx"
EXPECTED_GET_CURSOR_POS_IAT = 0x005513E4


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


def find_all(data: bytes, pattern: bytes) -> list[int]:
    """返回 pattern 在 data 中所有不重叠命中的起始偏移。

    这是一个通用的精确字节搜索辅助函数。当前 test14 主线主要使用通配签名搜索，
    保留这个函数只是方便以后对绝对地址写入/读取做额外交叉验证。
    """

    hits: list[int] = []
    start = 0

    while True:
        pos = data.find(pattern, start)
        if pos < 0:
            break
        hits.append(pos)
        start = pos + len(pattern)

    return hits


def require_unique(name: str, hits: list[int]) -> int:
    """要求签名恰好命中一次，并返回唯一文件偏移。"""

    if len(hits) != 1:
        raise RuntimeError(f"{name} 命中次数={len(hits)}，预期必须等于 1。")
    return hits[0]



def find_modal_class_vtable_from_global(
    text_data: bytes,
    image_base: int,
    text_rva: int,
    global_slot: int,
) -> tuple[int, int]:
    """从“constructor 把 this 写进目标 global slot”的机器码反推菜单类 vtable。

    已确认三个菜单构造函数都包含：
        C7 06 <vtable imm32>     ; mov [esi], vtable
        ...
        89 35 <global imm32>     ; mov [global_slot], esi

    Steam / 非 Steam 两份 EXE 的代码位置可以不同，但这个对象构造语义必须成立。
    返回 `(constructor_vtable_write_va, vtable_va)`；如果不能唯一闭合就拒绝兼容。
    """

    global_bytes = struct.pack("<I", global_slot)
    candidates: list[tuple[int, int]] = []
    start = 0

    while True:
        pos = text_data.find(b"\x89\x35" + global_bytes, start)
        if pos < 0:
            break

        # vtable 写入通常就在前 32 字节内。倒序找最近的 `C7 06 imm32`，避免把更早的别类构造误认进来。
        search_start = max(0, pos - 32)
        for back in range(pos - 2, search_start - 1, -1):
            if text_data[back : back + 2] == b"\xC7\x06" and back + 6 <= len(text_data):
                vtable_va = u32(text_data, back + 2)
                ctor_va = image_base + text_rva + back
                candidates.append((ctor_va, vtable_va))
                break

        start = pos + 1

    # 同一个类析构函数也会清 global slot，但那里写的是 0，不会匹配 `89 35`，所以正常应当恰好一条。
    unique = []
    for item in candidates:
        if item not in unique:
            unique.append(item)

    if len(unique) != 1:
        raise RuntimeError(
            f"global slot 0x{global_slot:08X} 的菜单构造/vtable 闭合数量={len(unique)}，预期 1。"
        )

    return unique[0]


def verify_modal_menu_class(
    *,
    name: str,
    control_id: int,
    global_slot: int,
    text_data: bytes,
    data: bytes,
    image_base: int,
    sections: list[tuple[str, int, int, int, int]],
    text_rva: int,
    child_hittest_va: int,
    layout_va: int,
    item_variant: bool,
) -> dict[str, int]:
    """验证一个辅助菜单类的原版 Draw/事件/命中/布局结构，并返回反编译地址供报告固化。"""

    ctor_va, vtable_va = find_modal_class_vtable_from_global(
        text_data, image_base, text_rva, global_slot
    )

    slots: dict[int, int] = {}
    for slot_offset in (0x08, 0x1C, 0x24, 0x30, 0x58):
        slot_va = vtable_va + slot_offset
        slot_file = va_to_file_offset(slot_va, image_base, sections)
        slots[slot_offset] = u32(data, slot_file)

    # layer1d 实际只会 Hook +0x08 Draw，所以离线兼容验证必须先证明这个槽仍指向当前 PE 的可执行代码。
    try:
        va_to_file_offset(slots[0x08], image_base, sections)
    except Exception as exc:
        raise RuntimeError(f"{name} vtable+0x08 Draw 不在当前 PE 可映射代码范围：0x{slots[0x08]:08X}。") from exc

    if slots[0x58] != layout_va:
        raise RuntimeError(
            f"{name} vtable+0x58 未指向通用布局：actual=0x{slots[0x58]:08X}, expected=0x{layout_va:08X}。"
        )

    hit_va = slots[0x30]
    hit_file = va_to_file_offset(hit_va, image_base, sections)
    hit_code = data[hit_file : hit_file + 40]
    if len(hit_code) < 31 or hit_code[0:4] != bytes.fromhex("56 8B F1 E8"):
        raise RuntimeError(f"{name} vtable+0x30 函数头不符合已确认命中更新形状。")

    # +3 的 E8 必须调用同一个通用 direct-child hit-test；layer1d 不修改这条路径；验证它只是为了确认原版输入结构没有变化。
    hit_child_target = rel32_target(hit_va + 3, data, hit_file + 3)
    if hit_child_target != child_hittest_va:
        raise RuntimeError(
            f"{name} vtable+0x30 没有调用已确认 child hit-test："
            f"actual=0x{hit_child_target:08X}, expected=0x{child_hittest_va:08X}。"
        )

    if not item_variant:
        expected_tail = bytes.fromhex("89 86 A8 00 00 00 5E C2 0C 00")
        if hit_code[8:18] != expected_tail:
            raise RuntimeError(f"{name} vtable+0x30 没有把 child hit-test 返回值写入 self+0xA8。")
    else:
        if not (
            hit_code[8:10] == bytes.fromhex("8B CE")
            and hit_code[10:16] == bytes.fromhex("89 86 A8 00 00 00")
            and hit_code[16] == 0xE8
            and hit_code[21:27] == bytes.fromhex("89 86 FC 00 00 00")
            and hit_code[27:31] == bytes.fromhex("5E C2 0C 00")
        ):
            raise RuntimeError(
                f"{name} 物品版 vtable+0x30 的 self+0xA8 / self+0xFC 更新结构发生变化。"
            )

    event_va = slots[0x24]
    event_file = va_to_file_offset(event_va, image_base, sections)
    event_code = data[event_file : event_file + 0x600]
    if len(event_code) < 0x40:
        raise RuntimeError(f"{name} vtable+0x24 事件函数超出文件范围。")

    # 每个类继续验证关键 self+0xA8 动作证据，用来保证 layer1d 没有建立在错误输入结构之上；不复制整个业务函数。
    if control_id == 0x0B:
        if event_code[0:28] != bytes.fromhex(
            "8B 81 A8 00 00 00 85 C0 74 0F 83 78 28 5D 75 09 8B 01 6A 00 6A 00 FF 50 1C C2 0C 00"
        ):
            raise RuntimeError(f"{name} 关闭按钮 0x5D -> vtable+0x1C 的事件证据变化。")
    elif control_id == 0x0D:
        # 0x7E/0x7F/0x80 直接从 self+0xA8->ID 分支；0x81/82/83 和 0x84..0x8F 通过全局控件查找后比较 self+0xA8。
        required = [
            bytes.fromhex("8B 87 A8 00 00 00"),
            bytes.fromhex("83 F8 7F"),
            bytes.fromhex("3D 80 00 00 00"),
            bytes.fromhex("83 F8 7E"),
            bytes.fromhex("68 81 00 00 00"),
            bytes.fromhex("68 82 00 00 00"),
            bytes.fromhex("68 83 00 00 00"),
            bytes.fromhex("8D 86 84 00 00 00"),
            bytes.fromhex("83 FE 0C"),
        ]
        for sig in required:
            if sig not in event_code:
                raise RuntimeError(f"{name} 技能动作 ID 0x7E~0x8F 的 self+0xA8 事件证据不完整。")
    elif control_id == 0x0E:
        for cid in (0x15, 0x17, 0x5A, 0x16):
            sig = bytes([0x83, 0xF8, cid])
            if sig not in event_code[:0x180]:
                raise RuntimeError(f"{name} 物品动作 ID 0x{cid:02X} 的事件分支缺失。")
        if bytes.fromhex("8B 86 A8 00 00 00") not in event_code[:0x80]:
            raise RuntimeError(f"{name} 物品事件没有读取 self+0xA8。")
    else:
        raise RuntimeError(f"内部错误：未支持的辅助菜单 control ID 0x{control_id:02X}。")

    return {
        "ctor": ctor_va,
        "vtable": vtable_va,
        "draw": slots[0x08],
        "active": slots[0x1C],
        "event": slots[0x24],
        "hit": slots[0x30],
        "layout": slots[0x58],
    }


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

    # test4 继续验证乾坤袋/物品辅助对象地址证据：构造函数必须唯一地把该类实例写入已确认全局槽。
    bag_aux_ctor_hits = []
    start = 0
    while True:
        hit = data.find(BAG_AUX_PANEL_CTOR_WRITE, start)
        if hit < 0:
            break
        bag_aux_ctor_hits.append(hit)
        start = hit + 1
    if len(bag_aux_ctor_hits) != 1:
        raise RuntimeError(
            f"乾坤袋/物品辅助面板构造写槽签名数量异常：期望 1，实际 {len(bag_aux_ctor_hits)}；"
            "拒绝把该地址继续当作已确认乾坤袋证据。"
        )
    bag_aux_ctor_off = bag_aux_ctor_hits[0]
    if not (raw_pointer <= bag_aux_ctor_off < raw_pointer + raw_size):
        raise RuntimeError("乾坤袋/物品辅助面板构造写槽签名不在 .text 节。")
    bag_aux_ctor_va = image_base + text_rva + (bag_aux_ctor_off - raw_pointer)
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

    # test13 不再主动调用这个函数，但游戏自己的 0x407000 Strategy-enter 函数会立即 call 它。
    # 因此仍然必须验证 mode_off 前 0x4A 字节确实是原版 0x404D30 风格函数头，后面还会把
    # Strategy-enter 内部的 CALL 目标与这里解析出的地址做交叉验证。
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

    # test14/test15 都依赖原版 0x404D30 的“同 mode + force=0 早退”语义。
    # 0x404D4D 开始应为：
    #   cmp [esi+0x04],eax
    #   jne ...
    #   mov ecx,[esp+0x1C]   ; 第二参数 force
    #   test ecx,ecx
    #   je  ...              ; force=0 时直接跳到函数收尾
    early_return = text_data[display_mode_off + 0x1D : display_mode_off + 0x2A]
    if len(early_return) < 13 or not (
        early_return[0:3] == bytes.fromhex("39 46 04")
        and early_return[3:5] == bytes.fromhex("75 0C")
        and early_return[5:9] == bytes.fromhex("8B 4C 24 1C")
        and early_return[9:11] == bytes.fromhex("85 C9")
        and early_return[11:13] == bytes.fromhex("0F 84")
    ):
        raise RuntimeError("原版显示模式函数缺少 test14 依赖的同-mode/force=0 早退结构。")

    # ---------------------------------------------------------------------------------------------
    # test15：游戏自己的 Strategy 状态进入/离开证据 + 临时 force 参数入口。
    #
    # 状态切换函数唯一命中后：
    #   +0x1E 是“离开旧状态 3”时 call 0x407040；
    #   +0x97 是“新状态 3 / BeforeStrategy”时 call 0x407000。
    #
    # 进入函数本身必须把 self+0x280 的显示模式写到 self+0x08，然后立即 call 我们上面已经解析的
    # 原版显示模式函数。这样才能证明 test15 仍借用游戏真实 Strategy 进入时刻，而不是换一个猜测 gate。
    # ---------------------------------------------------------------------------------------------
    strategy_exit_site_off = require_unique(
        "外传 Strategy exit callsite",
        find_masked(text_data, STRATEGY_EXIT_CALLSITE, STRATEGY_EXIT_CALLSITE_MASK),
    )
    strategy_enter_site_off = require_unique(
        "外传 Strategy enter callsite",
        find_masked(text_data, STRATEGY_ENTER_CALLSITE, STRATEGY_ENTER_CALLSITE_MASK),
    )

    strategy_exit_call_off = strategy_exit_site_off + 9
    strategy_enter_call_off = strategy_enter_site_off + 27
    strategy_exit_call_file = raw_pointer + strategy_exit_call_off
    strategy_enter_call_file = raw_pointer + strategy_enter_call_off
    strategy_exit_call_va = image_base + text_rva + strategy_exit_call_off
    strategy_enter_call_va = image_base + text_rva + strategy_enter_call_off

    if data[strategy_exit_call_file] != 0xE8:
        raise RuntimeError("外传 Strategy exit 目标位置不是 E8 CALL。")
    if data[strategy_enter_call_file] != 0xE8:
        raise RuntimeError("外传 Strategy enter 目标位置不是 E8 CALL。")

    strategy_exit_va = rel32_target(strategy_exit_call_va, data, strategy_exit_call_file)
    strategy_enter_va = rel32_target(strategy_enter_call_va, data, strategy_enter_call_file)
    strategy_exit_file = va_to_file_offset(strategy_exit_va, image_base, sections)
    strategy_enter_file = va_to_file_offset(strategy_enter_va, image_base, sections)

    strategy_enter_head = data[strategy_enter_file : strategy_enter_file + 20]
    if len(strategy_enter_head) < 20 or not (
        strategy_enter_head[0:5] == bytes.fromhex("56 8B F1 6A 00")
        and strategy_enter_head[5:11] == bytes.fromhex("8B 86 80 02 00 00")
        and strategy_enter_head[11] == 0x50
        and strategy_enter_head[12:15] == bytes.fromhex("89 46 08")
        and strategy_enter_head[15] == 0xE8
    ):
        raise RuntimeError(
            f"外传 Strategy enter 目标 0x{strategy_enter_va:08X} 不符合已确认结构。"
        )

    strategy_display_call_file = strategy_enter_file + 0x0F
    strategy_display_call_va = strategy_enter_va + 0x0F
    strategy_display_target = rel32_target(strategy_display_call_va, data, strategy_display_call_file)
    if strategy_display_target != display_mode_va:
        raise RuntimeError(
            "外传 Strategy enter 内部显示模式 CALL 与分辨率模式函数交叉验证失败："
            f"target=0x{strategy_display_target:08X}, expected=0x{display_mode_va:08X}。"
        )

    strategy_exit_head = data[strategy_exit_file : strategy_exit_file + 10]
    if len(strategy_exit_head) < 10 or not (strategy_exit_head[0] == 0xB9 and strategy_exit_head[5] == 0xE9):
        raise RuntimeError(
            f"外传 Strategy exit 目标 0x{strategy_exit_va:08X} 不符合已确认尾调用包装结构。"
        )

    # ---------------------------------------------------------------------------------------------
    # test15 新增：原游戏“标题/前端就是 mode 4”的独立证据。
    #
    # 这条签名不参与运行时 patch，只用于防止我们错误地把某个历史兼容 EXE 的前端语义也假定成 640x480。
    # 命中以后，再把最后的 E8 CALL 解出来，并要求它和上面解析的 0x404D30 是同一个函数。
    # 这样 test15 的 exit `mode=4, force=1` 就不是凭经验写死，而是由当前 EXE 自己的启动路径证明。
    # ---------------------------------------------------------------------------------------------
    frontend_mode4_off = require_unique(
        "原版前端 mode 4 应用",
        find_masked(text_data, FRONTEND_MODE4_APPLY, FRONTEND_MODE4_APPLY_MASK),
    )
    frontend_mode4_va = image_base + text_rva + frontend_mode4_off
    frontend_mode4_call_file = raw_pointer + frontend_mode4_off + 0x0C
    frontend_mode4_call_va = frontend_mode4_va + 0x0C
    frontend_mode4_target = rel32_target(
        frontend_mode4_call_va,
        data,
        frontend_mode4_call_file,
    )
    if frontend_mode4_target != display_mode_va:
        raise RuntimeError(
            "原版前端 mode 4 CALL 与显示模式函数交叉验证失败："
            f"target=0x{frontend_mode4_target:08X}, expected=0x{display_mode_va:08X}。"
        )

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
    # v0.3-test14 继续把这条包装函数作为 Steam delayed JMM apply 的关键结构证据：
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

    # test13 明确不再把 callsite 开头的 world_global 当 gameplay gate。
    # 用户 test12 实机已经证明该对象在主菜单也可能存在；这里仅验证 test8 已通过的世界点击目标函数。

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
    # test2/test3 键盘快捷键 callsite。
    #
    # 这是本轮“鼠标和键盘统一走同一套模态布局”的关键静态证据。DisplayFix 不改快捷键判断本身，
    # 只包住第三条原版 thiscall：原函数先执行，随后再执行绝对锚定。
    # ---------------------------------------------------------------------------------------------
    keyboard_off = require_unique(
        "主 HUD 键盘快捷键 callsite",
        find_masked(text_data, HUD_KEYBOARD_SHORTCUT_CALLSITE, HUD_KEYBOARD_SHORTCUT_CALLSITE_MASK),
    )
    keyboard_site_va = image_base + text_rva + keyboard_off
    keyboard_call_file = raw_pointer + keyboard_off + 26
    keyboard_call_va = keyboard_site_va + 26
    if data[keyboard_call_file] != 0xE8:
        raise RuntimeError("主 HUD 键盘快捷键 callsite +26 不是第三条 E8 CALL。")

    keyboard_target_va = rel32_target(keyboard_call_va, data, keyboard_call_file)
    keyboard_target_file = va_to_file_offset(keyboard_target_va, image_base, sections)
    keyboard_head = data[keyboard_target_file : keyboard_target_file + 32]
    if len(keyboard_head) < 32 or not (
        keyboard_head[0] == 0xA1
        and keyboard_head[5:9] == bytes.fromhex("53 56 8B F1")
        and keyboard_head[9:17] == bytes.fromhex("85 C0 74 08 3B C6 0F 85")
        and keyboard_head[21] == 0xA1
        and keyboard_head[26:32] == bytes.fromhex("B3 F0 85 C0 74 18")
    ):
        raise RuntimeError(
            f"键盘快捷键 CALL 目标 0x{keyboard_target_va:08X} 不符合已确认函数头。"
        )


    # ---------------------------------------------------------------------------------------------
    # test5 逆向证据：通用 child hit-test 内部 GetCursorPos 调用点必须保持原版。
    #
    # 这里验证的是“按钮命中补偿可以安全安装”的机器码事实，而不是验证某个截图坐标：
    #   1. 整个函数头在 .text 中必须唯一；
    #   2. 函数头 +26 必须仍是 6 字节 `FF 15 imm32`；
    #   3. imm32 必须正好是当前 外传 已确认的 GetCursorPos IAT 0x005513E4。
    #
    # 运行时 ASI 会把这一条局部 CALL 改成 E8 hook + NOP。全局 IAT 不改，因此 HUD、小地图和世界输入不会
    # 被三菜单的坐标反算污染。
    # ---------------------------------------------------------------------------------------------
    child_hittest_off = require_unique(
        "通用 child hit-test GetCursorPos 调用点",
        find_masked(text_data, UI_CHILD_HITTEST_CURSOR, UI_CHILD_HITTEST_CURSOR_MASK),
    )
    child_hittest_va = image_base + text_rva + child_hittest_off
    child_cursor_call_off = child_hittest_off + 26
    child_cursor_call_va = child_hittest_va + 26
    child_cursor_call = text_data[child_cursor_call_off : child_cursor_call_off + 6]
    if len(child_cursor_call) != 6 or child_cursor_call[0:2] != bytes.fromhex("FF 15"):
        raise RuntimeError("通用 child hit-test +26 已不是 6 字节 FF 15 GetCursorPos IAT 调用。")
    child_cursor_iat = u32(child_cursor_call, 2)
    if child_cursor_iat != EXPECTED_GET_CURSOR_POS_IAT:
        raise RuntimeError(
            "通用 child hit-test 的 GetCursorPos IAT 地址变化："
            f"actual=0x{child_cursor_iat:08X}, expected=0x{EXPECTED_GET_CURSOR_POS_IAT:08X}。"
        )

    # ---------------------------------------------------------------------------------------------
    # vtable +0x00：主 HUD scalar deleting destructor。
    #
    # test13 仍 hook 这一槽，但只用于清掉最近 HUD 实例缓存；它不再决定 FRONTEND/GAMEPLAY。
    # 原版函数头仍必须是 push esi / mov esi,ecx / call <real dtor>，保证缓存清理桥接调用约定正确。
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
    # test2 在这里不再只验证 0x0B / 0x0E。反汇编已经确认主 HUD 的六个主圆形按钮 0x09~0x0E
    # 都使用完全相同的“读取 target global -> 查询 active -> 再读同一 global -> vtable+0x1C”结构。
    # 六个 target global 都用于结构识别与排除误判；历史 test5 曾只移动 0x0B/0x0D/0x0E。layer1d 不移动任何一路，但仍要求六路原版结构全部成立。
    #
    # 0x0B / 0x0E 仍然具有额外意义：历史鼠标点击穿透/释放 fallback 只对这两路做过实机闭环，
    # 所以 test2 只扩大“布局读取”，不扩大那条输入行为补丁。
    # ---------------------------------------------------------------------------------------------
    event_slot_va = vtable_va + 0x24
    event_slot_file_offset = va_to_file_offset(event_slot_va, image_base, sections)
    event_slot_target = u32(data, event_slot_file_offset)
    event_target_file_offset = va_to_file_offset(event_slot_target, image_base, sections)

    if event_target_file_offset + 0x208 > len(data):
        raise RuntimeError("主 HUD vtable +0x24 指向的事件函数超出文件范围。")

    event_code = data[event_target_file_offset : event_target_file_offset + 0x208]
    if not (
        event_code[0x19:0x1C] == bytes([0xFF, 0x50, 0x30])
        and event_code[0xD0:0xD6] == bytes([0x8B, 0xBF, 0xA8, 0x00, 0x00, 0x00])
        and event_code[0xDE:0xE1] == bytes([0x8B, 0x47, 0x28])
    ):
        raise RuntimeError(
            "主 HUD vtable +0x24 的公共事件分派前半段不符合已确认版本，"
            f"target=0x{event_slot_target:08X}。"
        )

    branch_ids = [0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E]
    cmp_offsets = [0x167, 0x191, 0x1BB, 0x13D, 0x1E5, 0x113]
    target_globals: dict[int, int] = {}
    active_queries: dict[int, int] = {}

    for control_id, cmp_offset in zip(branch_ids, cmp_offsets):
        mov_a = cmp_offset + 5
        query_call = cmp_offset + 15
        mov_b = cmp_offset + 20
        toggle_call = cmp_offset + 32

        if event_code[cmp_offset : cmp_offset + 5] != bytes([0x83, 0xF8, control_id, 0x75, 0x25]):
            raise RuntimeError(f"ID 0x{control_id:02X} 分支入口结构不符合已确认版本。")
        if event_code[mov_a : mov_a + 2] != bytes([0x8B, 0x0D]):
            raise RuntimeError(f"ID 0x{control_id:02X} 第一次 target global 读取不是 mov ecx,[imm32]。")
        if event_code[mov_a + 6 : mov_a + 9] != bytes([0x6A, 0x00, 0x8B]) or event_code[mov_a + 9] != 0x31:
            raise RuntimeError(f"ID 0x{control_id:02X} active 查询前的 push 0 / mov esi,[ecx] 结构变化。")
        if event_code[query_call] != 0xE8:
            raise RuntimeError(f"ID 0x{control_id:02X} active 查询不是 E8 CALL。")
        if event_code[mov_b : mov_b + 2] != bytes([0x8B, 0x0D]):
            raise RuntimeError(f"ID 0x{control_id:02X} 第二次 target global 读取不是 mov ecx,[imm32]。")
        if event_code[toggle_call : toggle_call + 3] != bytes([0xFF, 0x56, 0x1C]):
            raise RuntimeError(f"ID 0x{control_id:02X} 最终不是调用目标对象 vtable+0x1C。")

        global_a = u32(event_code, mov_a + 2)
        global_b = u32(event_code, mov_b + 2)
        if global_a != global_b:
            raise RuntimeError(f"ID 0x{control_id:02X} 分支前后读取的目标窗口全局槽不同。")

        query_target = rel32_target(
            event_slot_target + query_call,
            data,
            event_target_file_offset + query_call,
        )
        target_globals[control_id] = global_a
        active_queries[control_id] = query_target

    if len(set(active_queries.values())) != 1:
        details = ", ".join(f"0x{k:02X}->0x{v:08X}" for k, v in active_queries.items())
        raise RuntimeError(f"六个主模态分支没有共用同一个 active 查询函数：{details}")

    active_query_va = next(iter(active_queries.values()))


    # ---------------------------------------------------------------------------------------------
    # 历史 test6/test7 曾围绕三个菜单类的输入路径做过失败实验；layer1d 已彻底停止这些运行时修改。
    # 但这些失败实验留下了很有价值的类结构证据，所以兼容工具继续把三类的
    # constructor/vtable/+0x08/+0x24/+0x30/+0x58 全部闭合并输出。
    # 目的正好相反：确认 layer1d 只需要 Hook +0x08 Draw，+0x30/self+0xA8 仍可完全交还原版。
    # ---------------------------------------------------------------------------------------------
    modal_classes: dict[int, dict[str, int]] = {}
    modal_classes[0x0B] = verify_modal_menu_class(
        name="装备菜单",
        control_id=0x0B,
        global_slot=target_globals[0x0B],
        text_data=text_data,
        data=data,
        image_base=image_base,
        sections=sections,
        text_rva=text_rva,
        child_hittest_va=child_hittest_va,
        layout_va=layout_va,
        item_variant=False,
    )
    modal_classes[0x0D] = verify_modal_menu_class(
        name="技能菜单",
        control_id=0x0D,
        global_slot=target_globals[0x0D],
        text_data=text_data,
        data=data,
        image_base=image_base,
        sections=sections,
        text_rva=text_rva,
        child_hittest_va=child_hittest_va,
        layout_va=layout_va,
        item_variant=False,
    )
    modal_classes[0x0E] = verify_modal_menu_class(
        name="物品菜单",
        control_id=0x0E,
        global_slot=target_globals[0x0E],
        text_data=text_data,
        data=data,
        image_base=image_base,
        sections=sections,
        text_rva=text_rva,
        child_hittest_va=child_hittest_va,
        layout_va=layout_va,
        item_variant=True,
    )

    # ---------------------------------------------------------------------------------------------
    # layer1d：UI manager 顶层 Draw 架构。
    #
    # 这里只读验证，不打补丁。运行时 layer1d 会 Hook 包装函数内部那条 manager Draw CALL，原因是同一个包装函数
    # 在游戏里存在多个外层调用者；Hook 包装函数内部可以天然覆盖它们，而无需对场景 callsite 逐个猜测。
    # ---------------------------------------------------------------------------------------------
    draw_wrapper_off = require_unique(
        "UI manager Draw 包装函数",
        find_masked(text_data, UI_MANAGER_DRAW_WRAPPER, UI_MANAGER_DRAW_WRAPPER_MASK),
    )
    draw_wrapper_va = image_base + text_rva + draw_wrapper_off
    ui_manager_va = u32(text_data, draw_wrapper_off + 3)
    if not (0x00400000 <= ui_manager_va < 0x00600000):
        raise RuntimeError(f"UI manager 绝对地址异常：0x{ui_manager_va:08X}。")

    manager_draw_call_va = draw_wrapper_va + 8
    manager_draw_call_file = raw_pointer + draw_wrapper_off + 8
    manager_draw_va = rel32_target(manager_draw_call_va, data, manager_draw_call_file)
    manager_draw_file = va_to_file_offset(manager_draw_va, image_base, sections)
    manager_code = data[manager_draw_file : manager_draw_file + 72]
    if len(manager_code) < 55:
        raise RuntimeError("UI manager Draw 函数长度不足，无法验证 layer1d 顶层绘制结构。")

    # 与 ASI 安装时相同的关键结构：先读取 HUD global、执行 HUD 特殊 pass，再开始 manager+0x1C 顶层链，
    # 每个顶层对象最终通过 vtable+0x08 Draw。
    if not (
        manager_code[0:3] == bytes.fromhex("56 8B F1")
        and manager_code[3:5] == bytes.fromhex("8B 0D")
        and manager_code[9] == 0x57
        and manager_code[10:14] == bytes.fromhex("8B 7C 24 0C")
        and manager_code[14:16] == bytes.fromhex("85 C9")
        and manager_code[18] == 0xE8
        and manager_code[27:29] == bytes.fromhex("8B 0D")
        and manager_code[33] == 0x57
        and manager_code[34] == 0xE8
        and manager_code[39:42] == bytes.fromhex("8B 4E 1C")
        and manager_code[49:51] == bytes.fromhex("8B 01")
        and manager_code[51] == 0x57
        and manager_code[52:55] == bytes.fromhex("FF 50 08")
    ):
        raise RuntimeError("UI manager Draw 不符合已确认的 HUD special-pass + 顶层 vtable+0x08 绘制结构。")

    hud_global_a = u32(manager_code, 5)
    hud_global_b = u32(manager_code, 29)
    if hud_global_a != hud_global_b:
        raise RuntimeError(
            f"UI manager Draw 两次主 HUD global 不一致：0x{hud_global_a:08X} / 0x{hud_global_b:08X}。"
        )

    hud_special_pass_va = rel32_target(manager_draw_va + 34, data, manager_draw_file + 34)
    va_to_file_offset(hud_special_pass_va, image_base, sections)

    main_hud_draw_slot_file = va_to_file_offset(vtable_va + 0x08, image_base, sections)
    main_hud_draw_va = u32(data, main_hud_draw_slot_file)
    va_to_file_offset(main_hud_draw_va, image_base, sections)

    # ---------------------------------------------------------------------------------------------
    # layer1d：顶层输入 root picker。运行时只 Hook 这一条 rel32 CALL，不直接写 manager+0x40。
    # ---------------------------------------------------------------------------------------------
    input_pick_off = require_unique(
        "顶层输入 root picker callsite",
        find_masked(text_data, UI_TOP_LEVEL_PICK_CALLSITE, UI_TOP_LEVEL_PICK_CALLSITE_MASK),
    )
    input_pick_site_va = image_base + text_rva + input_pick_off
    input_pick_call_va = input_pick_site_va + 15
    input_pick_call_file = raw_pointer + input_pick_off + 15
    input_picker_va = rel32_target(input_pick_call_va, data, input_pick_call_file)
    input_picker_file = va_to_file_offset(input_picker_va, image_base, sections)
    input_picker_code = data[input_picker_file : input_picker_file + 0xB3]
    if len(input_picker_code) < 0xB3:
        raise RuntimeError("顶层 root picker 长度不足，无法验证 layer1d 输入结构。")

    if not (
        input_picker_code[0:3] == bytes.fromhex("53 8B D9")
        and input_picker_code[0x4C:0x4F] == bytes.fromhex("8B 73 18")
        and input_picker_code[0x51:0x54] == bytes.fromhex("89 73 20")
        and input_picker_code[0x65:0x67] == bytes.fromhex("8B CE")
        and input_picker_code[0x67] == 0xE8
        and input_picker_code[0x9F:0xA2] == bytes.fromhex("8B 43 20")
        and input_picker_code[0xA2:0xA5] == bytes.fromhex("8B 4B 1C")
        and input_picker_code[0xAD:0xB0] == bytes.fromhex("8B 40 0C")
        and input_picker_code[0xB0:0xB3] == bytes.fromhex("89 43 20")
    ):
        raise RuntimeError("顶层 root picker 不符合 manager+0x18 -> manager+0x20 -> object+0x0C 的原版选择结构。")

    input_hit_rect_va = rel32_target(input_picker_va + 0x67, data, input_picker_file + 0x67)
    input_property_get_va = rel32_target(input_picker_va + 0x5B, data, input_picker_file + 0x5B)
    va_to_file_offset(input_hit_rect_va, image_base, sections)
    va_to_file_offset(input_property_get_va, image_base, sections)





    # clean1 额外确认 Steam 多语言最终兜底所依赖的 CreateFileA 调用上下文仍然唯一存在。
    # 这里只读验证 EXE，不修改任何字节。真正的 ASI 运行时补丁只在检测到 ComeOn.dll 的 Steam 环境安装。
    resjm_call_off = require_unique(
        "ResJM.Lib CreateFileA 调用上下文",
        find_masked(text_data, RESJM_CREATEFILE_CALL, RESJM_CREATEFILE_CALL_MASK),
    )
    resjm_call_va = image_base + text_rva + resjm_call_off + 9

    # 读出当前内部/隐藏分支宽高，便于识别这是原版还是哪一种外部宽屏改版。
    hidden_width = u32(text_data, mode_off + 17)
    hidden_height = u32(text_data, mode_off + 27)

    # 显示各关键签名的 VA，而不是只有“通过”两个字；接档时更容易人工复核。
    return [
        f"SHA-256={hashlib.sha256(data).hexdigest()}",
        font_state,
        f"分辨率模式派发 VA=0x{image_base + text_rva + mode_off:08X}",
        f"原版显示模式函数 VA=0x{display_mode_va:08X}（由 Strategy enter 原版流程调用）",
        f"Strategy enter callsite VA=0x{strategy_enter_call_va:08X} -> 0x{strategy_enter_va:08X}",
        f"Strategy exit callsite VA=0x{strategy_exit_call_va:08X} -> 0x{strategy_exit_va:08X}",
        f"Strategy enter 内部显示模式 CALL -> 0x{strategy_display_target:08X}（与原版显示模式函数交叉一致）",
        f"Strategy enter force 参数机器码=6A {strategy_enter_head[4]:02X}（test14/test15 进入游戏时临时改为 01 后恢复）",
        "原版显示模式函数已确认存在：同 mode ID 且 force=0 时早退；进入/退出都需要在对应时刻强制重应用",
        f"原版前端 mode 4 应用 VA=0x{frontend_mode4_va:08X} -> 0x{frontend_mode4_target:08X}（与显示模式函数交叉一致）",
        f"当前内部/扩展分支={hidden_width}x{hidden_height}",
        f"第一处分辨率映射 VA=0x{image_base + text_rva + map1_off:08X}",
        f"第二处分辨率映射 VA=0x{image_base + text_rva + map2_off:08X}",
        f"JMM 布局选择器 VA=0x{image_base + text_rva + jmm_off:08X}",
        f"GUI/JMM 分辨率应用包装 VA=0x{jmm_apply_va:08X} -> JMM/UI 广播 0x{jmm_load_va:08X}（test10 Steam delayed JMM 上下文）",
        f"JMM 资源路径构造 VA=0x{jmm_path_builder_va:08X} -> 资源根缓冲区 0x{resource_root_va:08X}",
        f"ResJM.Lib CreateFileA 低层 CALL VA=0x{resjm_call_va:08X}（clean1 语言兜底依赖；IAT 0x005511E4 保持原样）",
        f"世界鼠标按下 callsite VA=0x{world_press_va:08X} -> 0x{world_press_target_va:08X}（仅点击穿透保护，不参与 gameplay gate）",
        f"全局鼠标释放 callsite VA=0x{global_release_va:08X} -> 0x{global_release_target_va:08X}（顶部窗口 fallback）",
        f"键盘快捷键 callsite VA=0x{keyboard_call_va:08X} -> 0x{keyboard_target_va:08X}（layer1d 保持原版，不 Hook）",
        f"通用 child hit-test VA=0x{child_hittest_va:08X}，GetCursorPos callsite=0x{child_cursor_call_va:08X} -> IAT 0x{child_cursor_iat:08X}（仅作原版输入证据；layer1d 不改写）",
        f"layer1d Draw 包装 VA=0x{draw_wrapper_va:08X}，UI manager=0x{ui_manager_va:08X} -> Draw 0x{manager_draw_va:08X}",
        f"UI manager HUD 特殊 pass=0x{hud_special_pass_va:08X}（layer1d 只验证，不 Hook/不重放）",
        f"layer1d 顶层输入 picker callsite=0x{input_pick_call_va:08X} -> 0x{input_picker_va:08X}",
        f"顶层输入原版矩形函数=0x{input_hit_rect_va:08X}，JMM 属性读取=0x{input_property_get_va:08X}（key 0x0D）",
        f"主 HUD vtable+0x08 Draw=0x{main_hud_draw_va:08X}",
        f"主 HUD 根类构造 VA=0x{hud_va:08X}",
        f"主 HUD vtable=0x{vtable_va:08X}",
        f"vtable+0x00 -> 0x{destructor_slot_target:08X}（test15 仅清理 HUD 实例缓存，不参与 profile 生命周期）",
        f"vtable+0x24 -> 0x{event_slot_target:08X}（顶部按钮 0x09~0x0E 六条原版窗口开关已验证）",
        f"乾坤袋/物品辅助面板构造写槽 VA=0x{bag_aux_ctor_va:08X}：vtable 0x{BAG_AUX_PANEL_VTABLE:08X} -> global slot 0x{BAG_AUX_PANEL_SLOT:08X}（只保留逆向证据；layer1d 不手工移动）",
        "六路 target global=" + ", ".join(
            f"0x{control_id:02X}:0x{target_globals[control_id]:08X}" for control_id in branch_ids
        ),
        f"六路 active query=0x{active_query_va:08X}，最终均调用各目标对象 vtable+0x1C",
        f"vtable+0x58 -> 0x{layout_slot_target:08X}（已和通用布局函数交叉验证）",
        "layer1d 三菜单类证据=" + "; ".join(
            f"ID0x{cid:02X}: ctor=0x{info['ctor']:08X}, vtable=0x{info['vtable']:08X}, "
            f"+0x08=0x{info['draw']:08X}, +0x24=0x{info['event']:08X}, +0x30=0x{info['hit']:08X}, +0x58=0x{info['layout']:08X}"
            for cid, info in modal_classes.items()
        ),
        "layer1d 运行边界：只可能 Hook 三菜单 vtable+0x08 Draw；+0x30/self+0xA8/GetCursorPos/快捷键全部保持原版",
    ]



# -------------------------------------------------------------------------------------------------
# 5. Steam ComeOn.dll 专项验证（v0.2.1 继承 v0.2.0 正式封版结构）
# -------------------------------------------------------------------------------------------------

def rva_to_file_offset(
    rva: int,
    sections: list[tuple[str, int, int, int, int]],
) -> int:
    """把 DLL/EXE 的 RVA 转成文件偏移，不依赖首选 ImageBase。"""

    for _name, section_rva, virtual_size, raw_size, raw_pointer in sections:
        covered = max(virtual_size, raw_size)
        if section_rva <= rva < section_rva + covered:
            return raw_pointer + (rva - section_rva)

    raise RuntimeError(f"RVA 0x{rva:08X} 不属于任何已知 PE Section。")


def require_bytes_at_rva(
    data: bytes,
    sections: list[tuple[str, int, int, int, int]],
    rva: int,
    expected: bytes,
    name: str,
) -> None:
    """
    要求某个固定 RVA 的文件字节完全匹配。

    ComeOn.dll 本轮不是靠模糊猜测 patch 任意相似代码，而是建立在已经反汇编闭合的 Steam 2.01 DLL 上。
    因此对 callback/初始化片段使用固定 RVA + 精确字节，比只搜一个很短的 opcode 串更能防止误适配。
    """

    file_offset = rva_to_file_offset(rva, sections)
    actual = data[file_offset : file_offset + len(expected)]
    if actual != expected:
        raise RuntimeError(
            f"{name} RVA 0x{rva:08X} 字节不符合已确认 Steam ComeOn.dll。"
        )


def verify_steam_dll(path: Path) -> list[str]:
    """
    验证 Steam ComeOn.dll 中 v0.2.1 继续依赖、并由 v0.2.0 正式封版的 CreateWindowExA callback 结构。

    本函数不要求整个 DLL SHA-256 永远固定；SHA 只作为报告信息。
    当前兼容门槛是：CreateFileA 多语言 Hook、CreateWindowExA callback、对 lpClassName 的直接字符串解引用，
    以及后续官方 EDIT 子类化链都仍然与已经逆向闭合的 Steam 2.01 样本一致。
    """

    data = path.read_bytes()
    image_base, sections = parse_pe(data)

    # 当前已确认 Steam 2.01 ComeOn.dll 的首选基址是 0x10000000。
    # 运行时允许 ASLR 搬家；这里检查的是磁盘结构，所以首选基址仍应和逆向样本一致。
    if image_base != 0x10000000:
        raise RuntimeError(
            f"Steam ComeOn.dll ImageBase=0x{image_base:08X}，不是已确认的 0x10000000。"
        )

    # 多语言配置字段必须存在。它们能证明当前文件仍然包含我们已经实机验证过的语言子系统。
    for text in ("local_config", "current_language", "chs"):
        encoded = text.encode("utf-16le") + b"\x00\x00"
        if encoded not in data:
            raise RuntimeError(f"Steam ComeOn.dll 缺少 UTF-16 多语言标记：{text}")

    # CreateWindowExA callback 会只针对 class == "EDIT" 安装自定义 WndProc。
    if b"EDIT\x00" not in data:
        raise RuntimeError('Steam ComeOn.dll 缺少 CreateWindowExA callback 使用的 "EDIT" 类名。')

    # RVA 0x54E8：先安装游戏 CreateFileA Hook（长度 7，callback RVA 0x3E30）。
    # 这部分必须继续存在，因为 v0.2.0 明确保留官方多语言逻辑。
    require_bytes_at_rva(
        data,
        sections,
        0x54E8,
        bytes.fromhex(
            "FF D7 8B 80 E4 11 15 00 50 68 30 3E 00 10 53 6A 07 E8 02 C0 FF FF"
        ),
        "CreateFileA 官方多语言 Hook 安装块",
    )

    # RVA 0x54FE：紧接着把 USER32!CreateWindowExA 交给同一个 Hook 引擎，覆盖长度 12，callback RVA 0x2830。
    require_bytes_at_rva(
        data,
        sections,
        0x54FE,
        bytes.fromhex(
            "8B 0D 54 51 01 10 51 68 30 28 00 10 53 6A 0C E8 EE BF FF FF"
        ),
        "CreateWindowExA 全局 Hook 安装块",
    )

    # callback 函数头必须先把 Hook 上下文交给 0x1540 original-forward helper。
    require_bytes_at_rva(
        data,
        sections,
        0x2830,
        bytes.fromhex("55 8B EC 8B 45 08 56 8B 75 0C 56 50 E8 FF EC FF FF"),
        "CreateWindowExA callback 函数头",
    )

    # callback post-create 起点是 test3 的真正补丁位置。
    # 这里先从 context+0x2C 取 lpClassName，只检查 NULL，随后就直接把它当 char* 与 "EDIT" 比较。
    # Win32 允许 lpClassName 是 MAKEINTATOM，因此这条原始路径存在把低地址 atom 当指针解引用的风险。
    require_bytes_at_rva(
        data,
        sections,
        0x2841,
        bytes.fromhex(
            "8B 4E 2C 83 C4 08 89 46 20 85 C9 74 53 BA 08 52 01 10 53 8A 19"
        ),
        "CreateWindowExA callback lpClassName 直接字符串解析入口",
    )

    # v0.2.0 会在运行时把 RVA 0x2841 的前 6 字节改跳 ASI guard；磁盘原文件必须保持原始结构。
    # 后面的 EDIT 子类化则必须继续存在，因为正式版不再像 test2 那样禁用官方 WndProc。
    require_bytes_at_rva(
        data,
        sections,
        0x2889,
        bytes.fromhex("75 16 8B 4E 48 68 A0 26 00 10 6A FC 51 FF 15 64 51 01 10"),
        "EDIT WndProc 子类化条件分支与 SetWindowLongA 调用",
    )

    # RVA 0x287E~0x28A5 是最关键的 EDIT 子类化证据：保存 HWND、读取自定义 WndProc 0x26A0、
    # SetWindowLongA(..., GWL_WNDPROC=-4, ...)，最后保存旧 WndProc。
    require_bytes_at_rva(
        data,
        sections,
        0x287E,
        bytes.fromhex(
            "A3 A8 F6 01 10 39 0D A4 F6 01 10 75 16 8B 4E 48 "
            "68 A0 26 00 10 6A FC 51 FF 15 64 51 01 10 A3 A4 F6 01 10 5E 5D C2 08 00"
        ),
        "EDIT WndProc 子类化块",
    )

    return [
        f"SHA-256={hashlib.sha256(data).hexdigest()}",
        "Steam ComeOn.dll PE32/i386 结构通过",
        "官方 CreateFileA 多语言 Hook 安装块仍存在（保留）",
        "USER32!CreateWindowExA 全局 Hook 安装块仍存在：callback RVA=0x2830, overwrite=12",
        'CreateWindowExA callback 的 EDIT -> SetWindowLongA(GWL_WNDPROC) 子类化链已确认',
        "RVA 0x2841 原始 lpClassName 路径会在只检查 NULL 后直接解引用 class 参数",
        "RVA 0x2889 原始 EDIT WndProc 子类化块仍完整存在（layer1d 继续保留）",
        "layer1d 继续沿用 v0.2.1：0x2841 前置 class-atom guard，同时保留普通字符串类名与官方 EDIT 处理",
    ]


# -------------------------------------------------------------------------------------------------
# 6. 命令行入口
# -------------------------------------------------------------------------------------------------

def main() -> int:
    """接受一个或多个 ComeOn.exe / ComeOn.dll；全部通过返回 0，有任何一个失败返回 1。"""

    if len(sys.argv) < 2:
        print("用法：python verify_compatibility.py <ComeOn.exe 或 Steam ComeOn.dll> [更多文件 ...]")
        return 1

    failed = False

    for raw_path in sys.argv[1:]:
        path = Path(raw_path).resolve()
        print(f"\n[验证目标] {path}")

        try:
            # Steam DLL 和游戏 EXE 依赖的是两套完全不同的证据。
            # 只按扩展名分流，避免把 DLL 的代码误送进 EXE HUD/Strategy 验证器。
            if path.suffix.lower() == ".dll":
                lines = verify_steam_dll(path)
            else:
                lines = verify_one(path)
            for line in lines:
                print(f"[通过] {line}")
        except Exception as exc:
            failed = True
            print(f"[失败] {exc}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
