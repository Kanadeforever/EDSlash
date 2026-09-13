# -*- coding: utf-8 -*-
"""
DisplayFix 构建结果检查工具。

这个工具不运行 ASI，也不会改游戏文件。
它只负责检查编译出来的 DisplayFix.asi 是否仍然符合本项目当前的几个硬要求：

1. 必须是 32 位 x86 的 PE32；
2. 必须带 DLL 标志，因为 ASI 本质上就是由加载器 LoadLibrary 的 DLL；
3. 必须有非零入口点；
4. 必须导出 InitializeASI，兼容会主动调用该入口的 ASI Loader；
5. 当前架构故意不让 ASI 自己带 Windows DLL 导入表，因此 Import Directory 必须为 0；
6. 同目录的 DisplayFix.ini 必须存在，并且 [Display] 不能因为 UTF-8 BOM 落在文件第一节而失效。

代码里的每一步都写了较细的中文说明，方便以后换编译器或接档时核对。
"""

# pathlib.Path 是 Python 标准库的路径对象。
# 用它处理 Windows/Linux 路径都比手工拼字符串稳妥。
from pathlib import Path

# hashlib 只用来计算 SHA-256，方便把最终二进制指纹写进测试记录。
import hashlib

# struct 用来按“小端整数”读取 PE 文件头。
# x86 Windows PE 本来就是小端格式。
import struct

# sys 用来读取命令行参数并返回退出码。
import sys


# PE/COFF 里代表 32 位 Intel x86 的 Machine 值。
IMAGE_FILE_MACHINE_I386 = 0x014C

# PE Optional Header 里代表 PE32（不是 64 位 PE32+）的 Magic。
PE32_MAGIC = 0x010B

# COFF Characteristics 里的 DLL 位。
IMAGE_FILE_DLL = 0x2000


def sha256_file(path: Path) -> str:
    """读取整个文件并返回十六进制 SHA-256。"""

    # ASI 只有几十 KB，直接一次读入内存非常轻量。
    data = path.read_bytes()

    # hexdigest() 返回人类方便复制、比较的 64 字符十六进制文本。
    return hashlib.sha256(data).hexdigest()


def read_u16(data: bytes, offset: int) -> int:
    """从 data[offset] 开始读取一个 16 位小端无符号整数。"""

    # < 表示小端，H 表示 unsigned short，也就是 2 字节无符号整数。
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    """从 data[offset] 开始读取一个 32 位小端无符号整数。"""

    # <I 就是小端 unsigned int，长度 4 字节。
    return struct.unpack_from("<I", data, offset)[0]


def validate_asi(asi_path: Path) -> list[str]:
    """
    检查 ASI，并返回用于打印的成功信息列表。

    任何关键条件不满足都会 raise RuntimeError。
    这样命令行退出码会是失败，构建脚本也能据此停止。
    """

    # 先确认文件本身存在。
    if not asi_path.is_file():
        raise RuntimeError(f"找不到 ASI：{asi_path}")

    # 一次读入全部二进制。
    data = asi_path.read_bytes()

    # 正常 PE 文件最前面必须是 DOS 头的 MZ 标记。
    if len(data) < 0x100 or data[0:2] != b"MZ":
        raise RuntimeError("DisplayFix.asi 不是有效的 MZ/PE 文件。")

    # DOS 头 0x3C 保存真正 PE Header 的文件偏移。
    pe_offset = read_u32(data, 0x3C)

    # 做范围检查，防止损坏文件让后续解析越界。
    if pe_offset + 0x18 >= len(data):
        raise RuntimeError("PE Header 偏移越界。")

    # PE Header 必须以 4 字节 PE\0\0 开头。
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise RuntimeError("找不到 PE\\0\\0 Signature。")

    # PE Signature 后面马上是 20 字节 COFF File Header。
    coff = pe_offset + 4

    # COFF +0 是 Machine。
    machine = read_u16(data, coff + 0)
    if machine != IMAGE_FILE_MACHINE_I386:
        raise RuntimeError(
            f"Machine=0x{machine:04X}，不是要求的 Win32/x86 0x014C。"
        )

    # COFF +18 是 Characteristics；其中 0x2000 表示 DLL。
    characteristics = read_u16(data, coff + 18)
    if (characteristics & IMAGE_FILE_DLL) == 0:
        raise RuntimeError("PE 没有 DLL 标志，不能作为正常 ASI/DLL 使用。")

    # Optional Header 紧跟在 20 字节 COFF Header 后。
    optional = coff + 20

    # Optional Header 起始的 Magic 必须是 0x010B，也就是 PE32。
    magic = read_u16(data, optional + 0)
    if magic != PE32_MAGIC:
        raise RuntimeError(
            f"OptionalHeader.Magic=0x{magic:04X}，不是要求的 PE32 0x010B。"
        )

    # PE32 Optional Header +16 是 AddressOfEntryPoint。
    entry_point = read_u32(data, optional + 16)
    if entry_point == 0:
        raise RuntimeError("AddressOfEntryPoint=0，DllMain 入口丢失。")

    # PE32 Optional Header +92 是 NumberOfRvaAndSizes。
    number_of_directories = read_u32(data, optional + 92)
    if number_of_directories < 2:
        raise RuntimeError("PE Data Directory 数量异常，无法检查导入表。")

    # PE32 Data Directory 从 Optional Header +96 开始。
    # 第 0 项是 Export，第 1 项是 Import；每项由 RVA + Size 两个 DWORD 组成。
    data_directory = optional + 96
    import_rva = read_u32(data, data_directory + 8)
    import_size = read_u32(data, data_directory + 12)

    # 当前 DisplayFix 的架构刻意使用 ComeOn.exe 已解析好的 IAT，
    # 因此自己不应新增 CRT/kernel32/user32 导入表。
    if import_rva != 0 or import_size != 0:
        raise RuntimeError(
            f"Import Directory 非零：RVA=0x{import_rva:08X}, Size=0x{import_size:X}。"
        )

    # 精确解析导出表会让这个小工具变长很多。
    # 对当前构建验证来说，导出名字明文一定会出现在 .edata 中；
    # 所以这里直接确认 ASCII 名称存在即可。
    if b"InitializeASI\x00" not in data:
        raise RuntimeError("找不到 InitializeASI 导出名称。")

    # 所有检查通过后，把关键信息组成列表交给 main() 打印。
    return [
        "PE32 / i386",
        "DLL 标志存在",
        f"入口点 RVA=0x{entry_point:08X}",
        "InitializeASI 导出名称存在",
        "Import Directory=0（无外部导入表）",
        f"SHA-256={sha256_file(asi_path)}",
    ]


def validate_ini(ini_path: Path) -> list[str]:
    """
    检查发行 INI 的最小结构。

    Windows 的 GetPrivateProfile* A 系列接口并不可靠识别 UTF-8 BOM。
    如果 BOM 后面立刻就是 [Display]，第一节可能完全读不到，BaseHeight 就会静默回退默认 480。
    当前模板在真正 [Display] 之前固定放一行 ASCII 说明，因此即使某个编辑器以后重新加 BOM，
    受影响的也只是第一行说明，而不是配置节本身。
    """

    if not ini_path.is_file():
        raise RuntimeError(f"缺少同目录配置文件：{ini_path}")

    data = ini_path.read_bytes()

    # utf-8-sig 会自动去掉可选 BOM；中文注释只用于人看，真正的节名和键名仍然全部是 ASCII。
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"DisplayFix.ini 不是有效 UTF-8：{exc}") from exc

    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")

    # 第一行必须是保护性说明，第二个有效结构才允许进入 [Display]。
    # 这样用户的编辑器即便重新加 UTF-8 BOM，也不会再破坏 [Display]。
    if not lines or not lines[0].startswith("; DisplayFix configuration."):
        raise RuntimeError("DisplayFix.ini 第一行缺少 BOM 保护说明，可能再次导致 [Display] 第一节读取失败。")

    required = ("[Display]", "BaseHeight=", "AspectRatio=", "[Font]", "FixDPI=", "[GUI]", "CenterMainHUD=")
    for marker in required:
        if marker not in text:
            raise RuntimeError(f"DisplayFix.ini 缺少必要内容：{marker}")

    return [
        "DisplayFix.ini UTF-8 可解析",
        "[Display] 前存在 BOM 保护行",
        "BaseHeight / AspectRatio / FixDPI / CenterMainHUD 键存在",
    ]



def validate_build_bat(build_bat: Path) -> list[str]:
    """
    检查 build.bat 自身是否仍符合当前仓库的硬约束。

    这一步专门防止 v0.3-test8a 出现过的回归：递归扫描 Visual Studio 后，
    首个 clang.exe 恰好落在 ARM64\\bin，x64 Windows 因而提示“映像文件无效”。
    当前规则很简单：禁止递归 where /r，Visual Studio 只能显式使用 Llvm\\x64\\bin。
    """

    if not build_bat.is_file():
        raise RuntimeError(f"缺少构建脚本：{build_bat}")

    raw = build_bat.read_bytes()

    # BAT 必须保持 UTF-8 BOM + CRLF，这是本项目统一的 Windows 脚本格式。
    if not raw.startswith(b"\xEF\xBB\xBF"):
        raise RuntimeError("build.bat 必须使用 UTF-8 BOM。")
    if b"\r\n" not in raw or raw.replace(b"\r\n", b"").find(b"\n") != -1:
        raise RuntimeError("build.bat 必须统一使用 CRLF 换行。")

    text = raw.decode("utf-8-sig")
    lower = text.lower()

    if "where /r" in lower:
        raise RuntimeError("build.bat 禁止使用 where /r 递归扫描编译器，避免误选 ARM64 LLVM。")
    if "llvm\\arm64\\bin" in lower or "llvm\\arm64" in lower:
        raise RuntimeError("build.bat 不得引用 Visual Studio 的 ARM64 LLVM 目录。")
    if "vc\\tools\\llvm\\x64\\bin" not in lower:
        raise RuntimeError("build.bat 必须显式保留 Visual Studio Llvm\\x64\\bin 回退路径。")

    # 用户明确要求：所有有正文的 REM / echo 行末尾必须保留两个半角空格。
    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.lstrip()
        lowered = stripped.lower()
        has_text_rem = lowered.startswith("rem ") and len(stripped) > 4
        has_text_echo = lowered.startswith("echo ")

        if (has_text_rem or has_text_echo) and not line.endswith("  "):
            raise RuntimeError(
                f"build.bat 第 {line_number} 行 REM/echo 正文末尾没有两个半角空格。"
            )

    return [
        "build.bat UTF-8 BOM + CRLF",
        "未使用 where /r 递归扫描 LLVM",
        "Visual Studio 回退固定为 Llvm\\x64\\bin",
        "所有有正文的 REM/echo 行末尾均有两个半角空格",
    ]

def main() -> int:
    """命令行入口。成功返回 0，失败返回 1。"""

    # 如果用户显式给了路径，就验证那个路径。
    # 否则默认验证脚本上一级目录的 release\DisplayFix.asi。
    if len(sys.argv) >= 2:
        asi_path = Path(sys.argv[1]).resolve()
    else:
        # 新仓库结构固定为：
        #   <仓库根>\source\DisplayFix_DaoJian\tools\verify_build.py
        # 所以 parents[3] 才是仓库根目录。
        package_root = Path(__file__).resolve().parents[3]
        asi_path = package_root / "release" / "DisplayFix.asi"

    # INI 必须跟 ASI 同目录，否则用户直接拷贝 release 时配置会丢失。
    ini_path = asi_path.with_name("DisplayFix.ini")

    try:
        lines = validate_asi(asi_path)

        ini_lines = validate_ini(ini_path)

        # build.bat 与本工具固定同属 source\\DisplayFix_DaoJian；这里顺便验证构建脚本自身。
        build_bat = Path(__file__).resolve().parents[1] / "build.bat"
        build_lines = validate_build_bat(build_bat)

        print(f"[验证目标] {asi_path}")
        for line in lines:
            print(f"[通过] {line}")
        for line in ini_lines:
            print(f"[通过] {line}")
        for line in build_lines:
            print(f"[通过] {line}")
        print(f"[通过] 配置文件：{ini_path.name}")
        return 0

    except Exception as exc:
        # 这里捕获异常只是为了给用户一个干净的中文错误信息，
        # 不是为了吞掉错误：下面仍然返回非 0 退出码。
        print(f"[失败] {exc}")
        return 1


# Python 文件被直接运行时，从这里进入 main()。
if __name__ == "__main__":
    raise SystemExit(main())
