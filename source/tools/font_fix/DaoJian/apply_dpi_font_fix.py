# -*- coding: utf-8 -*-
"""
刀剑封魔录 ComeOn.exe：DPI 字体修复补丁器 v1.0。

这个版本和早期 test1 最大的区别：
- 不再用“整个 EXE 的 SHA-256 必须完全相同”来决定是否允许补丁；
- 改成验证“真正要修改的字体创建机器码”以及它前后的稳定上下文；
- 因此，宽屏补丁等只要没有改到同一段字体创建代码，就可以直接共存；
- 如果目标机器码不存在、出现多次、或者上下文不符合预期，脚本会拒绝修改。

脚本始终先复制，再修改副本；不会覆盖用户拖入的原文件。
"""

# Path 是 Python 标准库中的路径工具。
# 它能正确处理 Windows 路径，不需要我们自己到处拼接“\\”。
from pathlib import Path

# hashlib 只用于“记录输入/输出文件的 SHA-256”，方便测试和接档。
# 注意：从 v1.0 起，SHA-256 不再作为能不能打补丁的硬性门槛。
import hashlib

# shutil.copy2() 用来复制文件，并尽量保留原文件时间戳。
import shutil

# struct 用来从 PE 文件头中读取小端整数。
# 这里只做很轻量的 PE32 / x86 身份检查，不依赖第三方库。
import struct

# sys 用来读取命令行参数。
# 把 EXE 拖到 BAT 上以后，BAT 会把 EXE 路径传给这个 Python 脚本。
import sys


# -------------------------------------------------------------------------------------------------
# 1. 字体 DPI 修复的“原始代码签名”和“修复后代码签名”
# -------------------------------------------------------------------------------------------------
#
# 原游戏在这段代码里：
#   1) push 0x48              -> GetDeviceCaps 的 LOGPIXELSY 参数 = 90
#   2) push edi               -> 设备上下文 HDC
#   3) call [GetDeviceCaps]    -> 根据 Windows 当前缩放得到 96/120/144/... DPI
#   4) 后面把这个 DPI 交给 MulDiv，算出字体高度
#
# 问题在于：游戏自己的文字缓存/字格没有随着 DPI 一起扩大。
# 125% 时字体会按 120 DPI 创建，但字格仍然按原始尺寸裁切，所以文字缺边。
#
# 我们不改窗口 DPI，不改 cnc-ddraw，只让“字体创建这一处”永远看到 96 DPI。
#
# 为了兼容宽屏 EXE 等其他改版，这里不再检查整个文件哈希。
# 我们检查的是一小段非常具体的机器码上下文。
# 只要这段代码仍然是我们确认过的字体创建路径，就允许补丁。
ORIGINAL_CONTEXT = bytes.fromhex(
    "6A 48 "                  # push 0x48：LOGPIXELSY
    "6A 5A "                  # push 0x5A：前一处逻辑参数，作为上下文锚点
    "57 "                     # push edi：HDC
    "8B F0 "                  # mov esi,eax
    "FF 15 5C 80 52 00 "      # call dword ptr [0052805C] -> GetDeviceCaps
    "50 "                     # push eax：把 DPI 结果继续送入后续计算
    "6A 01 "                  # push 1
    "8B CE "                  # mov ecx,esi
)

# 修复后只替换 GetDeviceCaps CALL 这 6 个字节：
#   83 C4 08  -> add esp,8
#                 原本 GetDeviceCaps 是 stdcall，返回时会自动清掉两个 4 字节参数。
#                 现在不调用函数了，所以我们自己清掉这 8 字节，保持栈完全一致。
#   6A 60     -> push 96
#   58        -> pop eax
#                 最终让 EAX = 96，模拟 GetDeviceCaps 返回 96 DPI。
PATCHED_CONTEXT = bytes.fromhex(
    "6A 48 "
    "6A 5A "
    "57 "
    "8B F0 "
    "83 C4 08 6A 60 58 "       # 原 CALL 被等长 6 字节替换
    "50 "
    "6A 01 "
    "8B CE "
)

# ORIGINAL_CONTEXT 中，真正需要替换的 6 字节从第 7 个字节开始。
# 前面：6A48(2) + 6A5A(2) + 57(1) + 8BF0(2) = 7 字节。
PATCH_RELATIVE_OFFSET = 7

# 原始 CALL 指令的 6 字节。
ORIGINAL_CALL = bytes.fromhex("FF 15 5C 80 52 00")

# 新的 6 字节。
PATCH_CALL = bytes.fromhex("83 C4 08 6A 60 58")


# -------------------------------------------------------------------------------------------------
# 2. 基础工具函数
# -------------------------------------------------------------------------------------------------
def sha256_of_bytes(data: bytes) -> str:
    """计算一段二进制数据的 SHA-256，只用于记录，不作为兼容性门槛。"""

    # hashlib.sha256(data) 会一次完成哈希计算。
    # hexdigest() 把结果变成人类容易复制的十六进制字符串。
    return hashlib.sha256(data).hexdigest()


def find_all(data: bytes, needle: bytes) -> list[int]:
    """返回 needle 在 data 中出现的所有文件偏移。"""

    # hits 用来保存每一次匹配的位置。
    hits: list[int] = []

    # start 表示下一次从哪里继续查找。
    start = 0

    # 不断查找，直到 bytes.find() 返回 -1，也就是再也找不到。
    while True:
        pos = data.find(needle, start)

        # -1 表示没有更多结果，循环结束。
        if pos < 0:
            break

        # 记录这一次命中的文件偏移。
        hits.append(pos)

        # 从当前位置后一个字节继续找，避免永远找到同一个位置。
        start = pos + 1

    return hits


def validate_pe32_x86(data: bytes) -> None:
    """做轻量 PE32/x86 身份检查，避免把补丁器拖到完全无关的文件上。"""

    # DOS/PE 文件至少要比最小头结构大很多；太小的文件直接拒绝。
    if len(data) < 0x100:
        raise RuntimeError("文件太小，不像有效的 Windows PE 可执行文件。")

    # 所有正常 PE 文件开头都应该是 ASCII 字母 MZ。
    if data[0:2] != b"MZ":
        raise RuntimeError("文件没有 MZ 头，不是正常的 Windows PE 可执行文件。")

    # DOS 头 0x3C 位置保存真正 PE 头的文件偏移。
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]

    # 先检查 pe_offset 是否落在文件范围里，避免读取越界。
    if pe_offset + 0x18 > len(data):
        raise RuntimeError("PE 头偏移越界，文件可能损坏。")

    # 真正的 PE 头必须以四字节 PE\0\0 开头。
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise RuntimeError("找不到有效的 PE\\0\\0 文件头。")

    # PE Signature 后面的 COFF File Header 前两个字节是 Machine。
    # 0x014C 就是 IMAGE_FILE_MACHINE_I386，也就是 32 位 x86。
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    if machine != 0x014C:
        raise RuntimeError(
            f"目标不是 32 位 x86 PE。Machine=0x{machine:04X}，预期为 0x014C。"
        )

    # Optional Header 紧跟在 20 字节 COFF File Header 后面。
    optional_header = pe_offset + 4 + 20

    # Optional Header 的 Magic：0x010B=PE32，0x020B=PE32+（64 位）。
    magic = struct.unpack_from("<H", data, optional_header)[0]
    if magic != 0x010B:
        raise RuntimeError(
            f"目标不是 PE32。OptionalHeader.Magic=0x{magic:04X}，预期为 0x010B。"
        )


def locate_font_dpi_code(data: bytes) -> tuple[str, int]:
    """定位字体 DPI 代码，并判断当前是“未修复”还是“已经修复”。"""

    # 搜索未修复版本的完整上下文。
    original_hits = find_all(data, ORIGINAL_CONTEXT)

    # 搜索已经修复版本的完整上下文。
    patched_hits = find_all(data, PATCHED_CONTEXT)

    # 最安全的正常情况：原始签名恰好出现一次，并且修复签名完全没有出现。
    if len(original_hits) == 1 and len(patched_hits) == 0:
        return "original", original_hits[0]

    # 如果修复签名恰好出现一次、原始签名没有出现，说明这个 EXE 已经打过补丁。
    if len(original_hits) == 0 and len(patched_hits) == 1:
        return "patched", patched_hits[0]

    # 任何模糊情况都不猜。
    # 例如：完全找不到、找到两次、原始和修复版同时存在，都说明文件结构超出已确认范围。
    raise RuntimeError(
        "无法唯一确认刀剑封魔录的字体 DPI 目标代码。\n"
        f"原始签名命中次数：{len(original_hits)}\n"
        f"已修复签名命中次数：{len(patched_hits)}\n"
        "为了避免误改，脚本已经停止。"
    )


# -------------------------------------------------------------------------------------------------
# 3. 真正的补丁流程
# -------------------------------------------------------------------------------------------------
def patch_exe(input_path: Path) -> tuple[str, Path | None, int, str, str | None]:
    """
    验证并修复 EXE。

    返回值依次为：
    - 状态："patched" 或 "already_patched"
    - 输出路径；如果本来已经修复，则为 None
    - 找到的机器码上下文文件偏移
    - 输入 SHA-256
    - 输出 SHA-256；如果没有生成新文件，则为 None
    """

    # 必须是存在的普通文件。
    if not input_path.is_file():
        raise RuntimeError(f"找不到输入文件：{input_path}")

    # 一次读入整个 EXE。
    # ComeOn.exe 只有约 1.3 MiB，这样处理很轻量，而且便于进行完整签名搜索。
    source_data = input_path.read_bytes()

    # 先确认它至少是 32 位 x86 PE 文件。
    validate_pe32_x86(source_data)

    # 记录输入文件 SHA-256，供日志、测试和以后接档比较。
    input_sha256 = sha256_of_bytes(source_data)

    # 根据目标机器码本身判断兼容性，而不是根据整个文件哈希判断。
    state, context_offset = locate_font_dpi_code(source_data)

    # 如果已经是修复版，就不重复修改。
    if state == "patched":
        return "already_patched", None, context_offset, input_sha256, None

    # 真正 CALL 指令的位置 = 上下文起点 + 上下文内部相对偏移。
    patch_offset = context_offset + PATCH_RELATIVE_OFFSET

    # 再做一次最局部检查：这 6 字节必须确实就是 GetDeviceCaps CALL。
    # 这属于“保险丝”：前面的上下文已通过，这里仍然再次确认实际写入位置。
    found_call = source_data[patch_offset : patch_offset + len(ORIGINAL_CALL)]
    if found_call != ORIGINAL_CALL:
        raise RuntimeError(
            "目标上下文已找到，但真正准备修改的 6 字节不符合预期。\n"
            f"实际：{found_call.hex(' ').upper()}\n"
            f"预期：{ORIGINAL_CALL.hex(' ').upper()}"
        )

    # 输出文件名沿用输入文件自己的名字。
    # 例如：
    #   ComeOn.exe          -> ComeOn_DPI_FontFix.exe
    #   ComeOn480P.exe      -> ComeOn480P_DPI_FontFix.exe
    #   MyWideComeOn.exe    -> MyWideComeOn_DPI_FontFix.exe
    output_path = input_path.with_name(input_path.stem + "_DPI_FontFix" + input_path.suffix)

    # 先 copy2()，这样目标文件会继承输入 EXE 的修改时间等常用属性。
    shutil.copy2(input_path, output_path)

    # 用 bytearray 创建可修改副本。
    patched_data = bytearray(source_data)

    # 只替换那 6 个字节；文件其他位置一个字节都不主动修改。
    patched_data[patch_offset : patch_offset + len(PATCH_CALL)] = PATCH_CALL

    # 写回输出副本。
    output_path.write_bytes(patched_data)

    # 重新从磁盘读取，确保实际生成的文件就是预期结果。
    written_data = output_path.read_bytes()

    # 再跑一次 PE 检查，确认基本文件头没有被破坏。
    validate_pe32_x86(written_data)

    # 确认修复后签名恰好出现一次。
    written_state, written_context_offset = locate_font_dpi_code(written_data)
    if written_state != "patched" or written_context_offset != context_offset:
        raise RuntimeError("写入后复核失败：修复后的机器码上下文不符合预期。")

    # 除了那 6 个目标字节，输出文件的其他所有字节必须与输入完全相同。
    # 下面逐字节找差异，确保补丁器没有意外改动别处。
    changed_positions = [
        index
        for index, (old_byte, new_byte) in enumerate(zip(source_data, written_data))
        if old_byte != new_byte
    ]

    # 文件长度也必须保持不变；这是等长机器码补丁的重要性质。
    if len(source_data) != len(written_data):
        raise RuntimeError("写入后文件长度发生变化，已经判定为异常。")

    # 预期变化位置只能是 patch_offset 到 patch_offset+5 共 6 个字节中的实际差异位。
    # 注意有些新旧字节可能碰巧相等，所以不能简单要求“必须正好 6 个不同字节”。
    allowed_positions = set(range(patch_offset, patch_offset + len(PATCH_CALL)))
    if any(position not in allowed_positions for position in changed_positions):
        raise RuntimeError("写入后发现目标 6 字节以外还有其他内容变化，已经判定为异常。")

    # 计算输出文件哈希，供用户记录。
    output_sha256 = sha256_of_bytes(written_data)

    return "patched", output_path, context_offset, input_sha256, output_sha256


# -------------------------------------------------------------------------------------------------
# 4. 命令行入口
# -------------------------------------------------------------------------------------------------
def main() -> int:
    """读取用户拖入的 EXE，执行修复，并显示容易理解的结果。"""

    # 脚本只接受一个输入文件。
    if len(sys.argv) != 2:
        print("用法：python apply_dpi_font_fix.py <ComeOn.exe 或兼容改版 EXE>")
        print("推荐直接把目标 EXE 拖到 apply_fix.bat 上。")
        return 1

    # resolve() 把相对路径转换成完整绝对路径，后面的提示会更清楚。
    input_path = Path(sys.argv[1]).resolve()

    try:
        status, output_path, context_offset, input_sha256, output_sha256 = patch_exe(input_path)
    except Exception as exc:
        # 只显示面向用户的错误信息，不扔一大串 Python traceback。
        print(f"[失败] {exc}")
        return 1

    # 无论是否已经打过补丁，都把输入哈希显示出来，便于以后做版本记录。
    print(f"[信息] 输入 SHA-256：{input_sha256}")
    print(f"[信息] 字体 DPI 代码上下文文件偏移：0x{context_offset:X}")

    # 如果本来已经修复，就明确告诉用户，不重复生成文件。
    if status == "already_patched":
        print("[完成] 这个 EXE 已经包含 96 DPI 字体修复，不需要再次修改。")
        return 0

    # 正常新生成补丁文件时，输出位置和哈希都显示出来。
    print("[成功] 已生成 DPI 字体修复版：")
    print(output_path)
    print(f"[信息] 输出 SHA-256：{output_sha256}")
    print()
    print("兼容性判断依据是目标字体机器码，而不是整个 EXE 的 SHA-256。")
    print("因此，只要其他补丁没有改动同一段字体代码，宽屏等改版可以直接继续补丁。")

    return 0


# 只有直接运行本文件时才执行 main()。
# 将来如果另一个工具 import 本模块，它不会自动开始修改文件。
if __name__ == "__main__":
    raise SystemExit(main())
