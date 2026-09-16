#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BladeSwordQOL 构建产物验证器。

这个脚本的职责被刻意限制在两个范围内：

1. 检查最终生成的 BladeSwordQOL.asi 是否仍然是项目需要的 Win32/x86 ASI。
2. 检查 BladeSwordQOL.ini 是否缺少程序运行所必需的配置项，且这些配置值是否能被正常解析。

它不会检查源码目录结构、Markdown、README、Git 仓库文件、BAT 写法、代码注释、INI 注释文字，
也不会因为以后增加了新的源码模块、文档或额外 INI 选项而阻止构建。

这样可以避免“验证工具反过来规定项目怎么写”的问题：
验证器只负责验证真正会影响运行的构建产物和配置接口。
"""

from __future__ import annotations

import configparser
import struct
import sys
from pathlib import Path


def fail(message: str) -> None:
    """打印失败原因，然后用错误码 1 结束程序，让 build.bat 能进入失败分支。"""
    print(f"[失败] {message}")
    raise SystemExit(1)


def ok(message: str) -> None:
    """打印一条通过信息，方便从 build.bat 的控制台输出中确认验证到了哪一步。"""
    print(f"[通过] {message}")


def parse_pe(path: Path) -> dict[str, int | bytes]:
    """
    读取 ASI 的 PE 头，只提取本项目真正需要验证的字段。

    ASI 本质上就是一个 DLL，所以这里检查：
    - 是否有 MZ / PE 文件头；
    - CPU 是否为 i386；
    - Optional Header 是否为 PE32；
    - 是否有 DLL 标志；
    - 是否有有效入口点；
    - Import Directory / Export Directory 的位置。

    这里完全使用 Python 标准库，不要求用户额外安装 pefile 等第三方包。
    """
    data = path.read_bytes()

    # DOS 头最前面必须是 "MZ"。太短的文件也肯定不可能是合法 PE。
    if len(data) < 0x100 or data[:2] != b"MZ":
        fail("ASI 不是有效 MZ/PE 文件")

    # DOS 头 0x3C 位置保存真正 PE 头的文件偏移。
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]

    # PE 头必须以固定的四字节签名 "PE\\0\\0" 开始。
    if pe_offset + 0xF8 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        fail("ASI 缺少有效 PE 头")

    # IMAGE_FILE_HEADER 紧跟在 PE 签名后面。
    # 我们只关心 Machine、OptionalHeader 大小和 Characteristics。
    machine, _sections, _timestamp, _symptr, _symcount, _opt_size, characteristics = struct.unpack_from(
        "<HHIIIHH", data, pe_offset + 4
    )

    optional_header = pe_offset + 24

    # PE32 的 Magic 必须为 0x10B；PE32+ / x64 会是 0x20B。
    magic = struct.unpack_from("<H", data, optional_header)[0]

    # AddressOfEntryPoint 位于 PE32 Optional Header 的 +0x10。
    entry_point = struct.unpack_from("<I", data, optional_header + 16)[0]

    # NumberOfRvaAndSizes 决定后面的 Data Directory 是否存在。
    number_of_directories = struct.unpack_from("<I", data, optional_header + 92)[0]

    export_rva = 0
    export_size = 0
    import_rva = 0
    import_size = 0

    # Data Directory 第 0 项是 Export，第 1 项是 Import。
    if number_of_directories >= 2:
        export_rva, export_size = struct.unpack_from("<II", data, optional_header + 96)
        import_rva, import_size = struct.unpack_from("<II", data, optional_header + 104)

    return {
        "machine": machine,
        "characteristics": characteristics,
        "magic": magic,
        "entry_point": entry_point,
        "export_rva": export_rva,
        "export_size": export_size,
        "import_rva": import_rva,
        "import_size": import_size,
        "data": data,
    }


def validate_asi(asi_path: Path) -> None:
    """验证最终 ASI 的二进制属性，不检查源码实现细节。"""
    pe = parse_pe(asi_path)

    # 0x014C = IMAGE_FILE_MACHINE_I386。
    if pe["machine"] != 0x014C:
        fail("ASI 必须是 Win32/x86 (i386)")

    # 0x010B = PE32。这里明确拒绝误编译成 x64 的 PE32+。
    if pe["magic"] != 0x010B:
        fail("ASI 必须是 PE32，不能是 PE32+/x64")

    # IMAGE_FILE_DLL = 0x2000。ASI 作为 DLL 加载，因此这个标志必须存在。
    if not (pe["characteristics"] & 0x2000):
        fail("ASI 的 PE 头没有 DLL 标志")

    # 本项目使用自定义 DllMain 入口，入口点不能为 0。
    if pe["entry_point"] == 0:
        fail("ASI 的 PE 入口点为 0")

    # 当前 BladeSwordQOL 采用零 Import Table 的 freestanding 构建方式。
    # 如果这里突然出现 Import Table，通常说明链接参数或源码依赖发生了意外变化。
    if pe["import_rva"] != 0 or pe["import_size"] != 0:
        fail("ASI 出现 Import Table；当前构建要求保持零 Import Table")

    # ASI Loader 需要找到 InitializeASI 导出。
    # 先确认 PE 声明了 Export Directory，再确认导出名字确实存在于文件中。
    if pe["export_rva"] == 0 or pe["export_size"] == 0:
        fail("ASI 没有 Export Directory")
    if b"InitializeASI\x00" not in pe["data"]:
        fail("ASI 没有导出 InitializeASI")

    ok("ASI：PE32/x86、DLL、入口点、零 Import Table、InitializeASI 导出均正常")


def load_ini(path: Path, label: str) -> configparser.ConfigParser:
    """
    用 Python 标准库解析 INI。

    ConfigParser 会自动忽略以 ';' 或 '#' 开头的注释，因此验证结果完全不依赖注释写了什么。
    strict=True 可以顺便拦住同一个 section/key 被重复定义这种容易产生歧义的配置错误。
    """
    if not path.is_file():
        fail(f"找不到 {label}：{path}")

    raw = path.read_bytes()

    # 当前项目约定配置使用 UTF-8 无 BOM。BOM 虽然很多解析器能容忍，但这里会造成首个 section 名异常的风险。
    if raw.startswith(b"\xef\xbb\xbf"):
        fail(f"{label} 带 UTF-8 BOM")

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        fail(f"{label} 不是有效 UTF-8：{exc}")

    parser = configparser.ConfigParser(
        interpolation=None,
        strict=True,
        inline_comment_prefixes=None,
    )

    # 保留 key 原始大小写，虽然下面比较时会自己转成小写。
    # 这样错误信息可以继续显示用户实际写在文件里的名称。
    parser.optionxform = str

    try:
        parser.read_string(text)
    except configparser.Error as exc:
        fail(f"{label} 语法无法解析：{exc}")

    return parser


def find_key_case_insensitive(
    parser: configparser.ConfigParser,
    section: str,
    key: str,
) -> str | None:
    """在指定 section 中按不区分大小写的方式寻找 key，并返回实际 key 名。"""
    if not parser.has_section(section):
        return None

    wanted = key.lower()
    for actual_key in parser[section].keys():
        if actual_key.lower() == wanted:
            return actual_key
    return None


def require_key(
    parser: configparser.ConfigParser,
    label: str,
    section: str,
    key: str,
) -> str:
    """确认一个运行所需配置项存在，并返回去掉首尾空格后的值。"""
    if not parser.has_section(section):
        fail(f"{label} 缺少 [{section}] section")

    actual_key = find_key_case_insensitive(parser, section, key)
    if actual_key is None:
        fail(f"{label} 缺少 [{section}] {key}")

    value = parser[section][actual_key].strip()
    if value == "":
        fail(f"{label} 的 [{section}] {key} 不能为空")

    return value


def validate_bool_value(label: str, section: str, key: str, value: str) -> None:
    """本项目布尔配置只允许明确的 0 或 1，避免拼写错误被程序默认为其它行为。"""
    if value not in ("0", "1"):
        fail(f"{label} 的 [{section}] {key} 必须是 0 或 1，当前值：{value}")


def validate_positive_integer(label: str, section: str, key: str, value: str) -> None:
    """检查需要正整数的配置值，例如 BaseHeight。这里不擅自限制具体上限。"""
    try:
        number = int(value, 10)
    except ValueError:
        fail(f"{label} 的 [{section}] {key} 必须是十进制整数，当前值：{value}")

    if number <= 0:
        fail(f"{label} 的 [{section}] {key} 必须大于 0，当前值：{value}")


def validate_aspect_ratio(label: str, value: str) -> None:
    """
    检查 AspectRatio 的值格式。

    允许两种形式：
    - Auto
    - 正整数:正整数，例如 16:9、32:9、8:9

    这里只验证配置值能不能被程序理解，不检查任何关于 8 像素对齐的注释文字。
    """
    if value.lower() == "auto":
        return

    parts = value.split(":")
    if len(parts) != 2:
        fail(f"{label} 的 [Display] AspectRatio 必须是 Auto 或 W:H，当前值：{value}")

    try:
        width_part = int(parts[0].strip(), 10)
        height_part = int(parts[1].strip(), 10)
    except ValueError:
        fail(f"{label} 的 [Display] AspectRatio 两边必须是正整数，当前值：{value}")

    if width_part <= 0 or height_part <= 0:
        fail(f"{label} 的 [Display] AspectRatio 两边必须大于 0，当前值：{value}")


def validate_ini(parser: configparser.ConfigParser, label: str) -> None:
    """
    检查当前 v0.1-dev1 运行所必需的 INI 接口。

    这里只要求“程序已经依赖的 section/key 必须存在”。
    以后新增模块时可以自由增加 section/key；验证器不会因为出现额外选项而失败。
    """
    enable = require_key(parser, label, "Display", "Enable")
    base_height = require_key(parser, label, "Display", "BaseHeight")
    aspect_ratio = require_key(parser, label, "Display", "AspectRatio")
    fix_dpi = require_key(parser, label, "Font", "FixDPI")
    center_hud = require_key(parser, label, "GUI", "CenterMainHUD")
    auxiliary_ui = require_key(parser, label, "GUI", "AuxiliaryUIAboveHUD")

    validate_bool_value(label, "Display", "Enable", enable)
    validate_positive_integer(label, "Display", "BaseHeight", base_height)
    validate_aspect_ratio(label, aspect_ratio)
    validate_bool_value(label, "Font", "FixDPI", fix_dpi)
    validate_bool_value(label, "GUI", "CenterMainHUD", center_hud)
    validate_bool_value(label, "GUI", "AuxiliaryUIAboveHUD", auxiliary_ui)

    ok(f"{label}：必需 section/key 齐全，配置值格式正常")


def compare_required_ini_interface(
    release_ini: configparser.ConfigParser,
    template_ini: configparser.ConfigParser,
) -> None:
    """
    确认 release INI 与 template INI 都提供同一组“必需接口”。

    注意：这里只比较必需 key 是否同时存在，不要求两份 INI 的注释、顺序、额外选项或默认值完全相同。
    这样以后可以增加新模块选项，而不会让旧验证逻辑妨碍正常开发。
    """
    required = {
        "Display": ("Enable", "BaseHeight", "AspectRatio"),
        "Font": ("FixDPI",),
        "GUI": ("CenterMainHUD", "AuxiliaryUIAboveHUD"),
    }

    for section, keys in required.items():
        for key in keys:
            release_has = find_key_case_insensitive(release_ini, section, key) is not None
            template_has = find_key_case_insensitive(template_ini, section, key) is not None

            if release_has != template_has:
                fail(
                    f"release/template INI 必需接口不同步：[{section}] {key} "
                    f"(release={int(release_has)}, template={int(template_has)})"
                )

    ok("release/template INI 的必需配置接口一致")


def main() -> None:
    """命令行入口：接收最终 ASI 路径，然后验证 ASI 与两份 INI。"""
    if len(sys.argv) != 2:
        fail("用法：python tools\\verify_build.py release\\BladeSwordQOL.asi")

    asi_path = Path(sys.argv[1]).resolve()
    if not asi_path.is_file():
        fail(f"找不到 ASI：{asi_path}")

    # verify_build.py 位于 source\\tools\\。
    # parents[1] 是 source，parents[2] 是项目根目录。
    source_root = Path(__file__).resolve().parents[1]
    package_root = Path(__file__).resolve().parents[2]

    release_ini_path = package_root / "release" / "BladeSwordQOL.ini"
    template_ini_path = source_root / "template" / "BladeSwordQOL.ini"

    # 第一部分只验证最终 ASI 本身。
    validate_asi(asi_path)

    # 第二部分只验证 release/template 两份 INI 的配置接口与值格式。
    release_ini = load_ini(release_ini_path, "release\\BladeSwordQOL.ini")
    template_ini = load_ini(template_ini_path, "source\\template\\BladeSwordQOL.ini")

    validate_ini(release_ini, "release\\BladeSwordQOL.ini")
    validate_ini(template_ini, "source\\template\\BladeSwordQOL.ini")
    compare_required_ini_interface(release_ini, template_ini)

    print("[成功] BladeSwordQOL 构建产物验证全部通过")


if __name__ == "__main__":
    main()
