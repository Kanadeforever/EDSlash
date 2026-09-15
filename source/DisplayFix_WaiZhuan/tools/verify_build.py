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
6. 同目录的 DisplayFix.ini 必须存在、能以 UTF-8 解析，并且正式成品禁止 UTF-8 BOM；
7. 仓库内源码、脚本、配置和 Markdown 文本统一禁止 UTF-8 BOM，BAT 额外必须是 CRLF；
8. layer1d 必须继续保留 v0.2.1/v0.2.0 的 ResJM.Lib 多语言调用点 shim；
9. layer1d 必须继续保留 v0.2.1/v0.2.0 已实机闭环的 ComeOn.dll CreateWindowExA class-atom 安全 guard；
10. test3-test9 的 DirectShow/OpenGL/MovieManager/watchdog/teardown/worker 历史失败代码不得回到当前运行路径；
11. stripe1 继续要求 DisplayFix.log 使用简体中文等级和正文，不能残留旧版英文日志标签。

代码里的每一步都写了较细的中文说明，方便以后换编译器或接档时核对。
"""

# pathlib.Path 是 Python 标准库的路径对象。
# 用它处理 Windows/Linux 路径都比手工拼字符串稳妥。
from pathlib import Path

# hashlib 只用来计算 SHA-256，方便把最终二进制指纹写进测试记录。
import hashlib

# re 用来只提取 C 源码里的字符串字面量，避免把历史注释里的英文旧日志误判为运行日志。
import re

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
    检查发行 INI 的最小结构，并强制禁止 UTF-8 BOM。

    Windows 的 GetPrivateProfile* A 系列接口对 UTF-8 BOM 的兼容性不可靠，本项目早期已经实机遇到
    第一节 [Display] 因 BOM 而读取异常的问题。最终封版规则因此不再“依赖保护行容忍 BOM”，而是更直接：
    正式模板和 release 配置都必须是 UTF-8 无 BOM。

    第一行仍保留纯 ASCII 说明，是为了让文件结构一眼可辨，也给用户以后手工编辑时多一层防误操作余量；
    但它不再意味着项目允许 BOM 存在。
    """

    if not ini_path.is_file():
        raise RuntimeError(f"缺少同目录配置文件：{ini_path}")

    data = ini_path.read_bytes()

    if data.startswith(b"\xEF\xBB\xBF"):
        raise RuntimeError("DisplayFix.ini 禁止 UTF-8 BOM；请保存为 UTF-8 无 BOM。")

    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError(f"DisplayFix.ini 不是有效 UTF-8：{exc}") from exc

    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    if not lines or not lines[0].startswith("; DisplayFix configuration."):
        raise RuntimeError("DisplayFix.ini 第一行缺少固定 ASCII 配置说明。")

    required = ("[Display]", "BaseHeight=", "AspectRatio=", "[Font]", "FixDPI=", "[GUI]", "CenterMainHUD=", "AuxiliaryUIAboveHUD=")
    for marker in required:
        if marker not in text:
            raise RuntimeError(f"DisplayFix.ini 缺少必要内容：{marker}")

    return [
        "DisplayFix.ini UTF-8 无 BOM",
        "[Display] 前存在固定 ASCII 说明行",
        "BaseHeight / AspectRatio / FixDPI / CenterMainHUD / AuxiliaryUIAboveHUD 键存在",
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

    # 用户实机确认：即使首行先执行 chcp 65001，带 BOM 的 BAT 在本机 CMD 仍会乱码。
    # 因此封版规则明确反过来：BAT 必须是 UTF-8 无 BOM + CRLF。
    if raw.startswith(b"\xEF\xBB\xBF"):
        raise RuntimeError("build.bat 禁止 UTF-8 BOM；请保存为 UTF-8 无 BOM。")
    if b"\r\n" not in raw or raw.replace(b"\r\n", b"").find(b"\n") != -1:
        raise RuntimeError("build.bat 必须统一使用 CRLF 换行。")

    text = raw.decode("utf-8")
    lower = text.lower()

    if "where /r" in lower:
        raise RuntimeError("build.bat 禁止使用 where /r 递归扫描编译器，避免误选 ARM64 LLVM。")
    if "llvm\\arm64\\bin" in lower or "llvm\\arm64" in lower:
        raise RuntimeError("build.bat 不得引用 Visual Studio 的 ARM64 LLVM 目录。")
    if "vc\\tools\\llvm\\x64\\bin" not in lower:
        raise RuntimeError("build.bat 必须显式保留 Visual Studio Llvm\\x64\\bin 回退路径。")

    # 从稳定基线 v0.3.2 / v0.2.1 起，DisplayFix.log 使用中文 UTF-8 窄字符串；stripe1 继续保持。
    # 源码本身又按项目规则禁止 BOM，因此这里显式要求 clang 固定输入与执行字符集为 UTF-8，
    # 防止不同 Windows 系统代码页把中文字符串编译成不同字节。
    if "-finput-charset=utf-8" not in lower or "-fexec-charset=utf-8" not in lower:
        raise RuntimeError("build.bat 必须显式指定 -finput-charset=UTF-8 和 -fexec-charset=UTF-8。")

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
        "build.bat UTF-8 无 BOM + CRLF",
        "未使用 where /r 递归扫描 LLVM",
        "Visual Studio 回退固定为 Llvm\\x64\\bin",
        "clang 输入/执行字符集固定为 UTF-8",
        "所有有正文的 REM/echo 行末尾均有两个半角空格",
    ]


def validate_repository_bom_free(package_root: Path) -> list[str]:
    """
    扫描仓库内所有人类可编辑文本，确认没有任何 UTF-8 BOM。

    用户已经把“BOM 必须消失”定为项目硬规则，所以这里不只检查 build.bat。
    只扫描明确属于文本的扩展名，避免把 ASI 等二进制文件误当文本。
    """

    text_suffixes = {".bat", ".ini", ".md", ".py", ".c", ".h", ".cpp", ".txt"}
    checked = 0

    for top_name in ("source", "docs", "release"):
        top = package_root / top_name
        if not top.exists():
            continue
        for path in top.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in text_suffixes:
                continue
            checked += 1
            data = path.read_bytes()
            if data.startswith(b"\xEF\xBB\xBF"):
                raise RuntimeError(f"禁止 UTF-8 BOM：{path}")
            if path.suffix.lower() == ".bat":
                if b"\r\n" not in data or data.replace(b"\r\n", b"").find(b"\n") != -1:
                    raise RuntimeError(f"BAT 必须统一使用 CRLF：{path}")
                try:
                    data.decode("utf-8")
                except UnicodeDecodeError as exc:
                    raise RuntimeError(f"BAT 不是有效 UTF-8：{path}: {exc}") from exc

    return [f"仓库文本 UTF-8 BOM=0（已检查 {checked} 个文本文件）"]


def validate_font_display_independence(source_path: Path) -> list[str]:
    """
    静态检查 v0.1-test2 的配置开关顺序，防止以后又把字体修复绑回 Display.Enable。

    这里不尝试做完整 C 语法解析，因为这个小工具的职责只是构建后防回归。
    我们只检查 initialize_display_fix() 中三个非常稳定的语义锚点顺序：

    1. `if (config.fix_font_dpi)`：字体开关开始处理；
    2. `apply_font_dpi_fix(&text_region)`：真正执行字体补丁；
    3. `if (!config.enable)`：Display 主开关的提前退出。

    正确顺序必须是 1 -> 2 -> 3。这样即使 Display.Enable=0，字体步骤也已经先执行。
    如果以后有人把 Display gate 又挪回字体步骤前面，这个构建验证会立刻失败。
    """

    if not source_path.is_file():
        raise RuntimeError(f"缺少主源码：{source_path}")

    text = source_path.read_text(encoding="utf-8")

    # 先只截取初始化函数，避免文件前面的注释或其他辅助函数里出现相同关键字干扰位置判断。
    function_start = text.find("static void initialize_display_fix(void)")
    if function_start < 0:
        raise RuntimeError("找不到 initialize_display_fix()。")

    function_text = text[function_start:]

    font_if = function_text.find("if (config.fix_font_dpi)")
    font_apply = function_text.find("apply_font_dpi_fix(&text_region)")
    display_gate = function_text.find("if (!config.enable)")

    if font_if < 0 or font_apply < 0 or display_gate < 0:
        raise RuntimeError("找不到 FixDPI / apply_font_dpi_fix / Display.Enable 三个初始化锚点。")

    if not (font_if < font_apply < display_gate):
        raise RuntimeError(
            "配置顺序回归：Font.FixDPI 必须在 Display.Enable 提前退出之前独立执行。"
        )

    return ["Font.FixDPI 与 Display.Enable 初始化顺序独立（字体先执行，Display gate 后判断）"]


def validate_v021_stable_steam_scope(source_path: Path) -> list[str]:
    """
    检查 layer1d 继续完整继承 v0.2.1 / v0.2.0 Steam/OpenGL 稳定修复边界。

    已有实机证据：
    - test1 只 neutralize callback 后处理时 OpenGL 成功；
    - test2 只禁止 EDIT WndProc 子类化时 OpenGL 重新崩溃。

    layer1d 的新 Draw 层逻辑不得触碰这部分；Steam/OpenGL 运行逻辑必须继续恢复官方 EDIT 处理，并只修 CreateWindowExA callback 对 lpClassName 的 API 语义错误：
    lpClassName 可以是字符串，也可以是高 16 位为 0 的 class atom。ComeOn.dll 原 callback 会直接解引用它。

    当前允许的新增改动只有：
    - RVA 0x2841 的 6 字节入口改成 E9 rel32 + NOP；
    - ASI naked trampoline 重放原指令；
    - NULL / lpClassName < 0x10000 时跳到 RVA 0x28A1；
    - 普通字符串类名跳回 RVA 0x284E，官方 EDIT 比较和 SetWindowLongA 必须完整保留；
    - 仅记录 atom 命中计数，不在窗口回调中调用 Win32/日志函数。
    """

    if not source_path.is_file():
        raise RuntimeError(f"缺少主源码：{source_path}")

    text = source_path.read_text(encoding="utf-8")

    required = (
        "install_steam_resjm_language_shim",
        "steam_create_file_a_shim",
        'str_equal_icase(base_name, "ResJM.Lib")',
        "CREATE_FILE_CALL_PATTERN",
        "0xFF,0x15,0xE4,0x11,0x55,0x00",
        "Steam多语言 ResJM.Lib CreateFileA 兜底已安装",
        "steam_createwindow_class_atom_guard",
        "install_steam_createwindow_class_atom_guard",
        "steam_base + 0x2841u",
        "steam_base + 0x284Eu",
        "steam_base + 0x28A1u",
        "cmp ecx, 10000h",
        "g_steam_class_atom_bypass_count",
        "Steam ComeOn.dll CreateWindowExA 类Atom兼容修复已安装；官方EDIT处理保持完整",
        "Steam启动器控制的开场动画不属于DisplayFix职责；当前未启用任何历史影片实验",
    )
    for marker in required:
        if marker not in text:
            raise RuntimeError(f"layer1d 缺少从 v0.2.1 稳定基线继承的必要源码锚点：{marker}")

    # test1 过宽隔离与 test2 错误收缩都必须离开当前运行源码。
    forbidden_old_scope = (
        "find_steam_createwindow_hook_object",
        "neutralize_steam_createwindow_callback",
        "restore_existing_steam_edit_subclass",
        "isolate_steam_createwindow_hook",
        "disable_steam_edit_wndproc_subclass",
        "g_CreateWindowExA_address",
        "g_VirtualQuery",
        "MEMORY_BASIC_INFORMATION32",
        "replacement = 0xEBu",
    )
    for marker in forbidden_old_scope:
        if marker in text:
            raise RuntimeError(f"layer1d 仍残留 test1/test2 旧隔离代码：{marker}")

    # test3~test9 的旧影片/OpenGL实验入口继续禁止回归。
    forbidden_history = (
        "install_steam_movie_aspect_patch",
        "start_steam_movie_window_watchdog",
        "install_steam_original_movie_hooks",
        "suppress_steam_movie_export",
        "install_steam_opengl_safe_teardown",
        "install_steam_movie_hard_quarantine",
        "terminate_running_steam_movie_workers",
        "steam_movie_quarantine_watch_thread",
    )
    for marker in forbidden_history:
        if marker in text:
            raise RuntimeError(f"layer1d 混入了历史失败影片实验代码：{marker}")

    # 官方 EDIT 逻辑必须仍然留在 ComeOn.dll 原始路径；当前源码不能再主动禁 SetWindowLongA。
    if "官方EDIT处理保持完整" not in text:
        raise RuntimeError("layer1d 没有明确保留 v0.2.1 已封版的官方 EDIT WndProc 路径。")

    # 语言兜底仍然只能改游戏低层 CALL，不能改写 CreateFileA IAT。
    if "g_steam_create_file_callsite" not in text:
        raise RuntimeError("layer1d 缺少 v0.2.1 已封版的 ResJM CreateFileA 调用点记录。")
    if "GAME_CreateFileA =" in text or "GAME_CREATE_FILE_IAT_ADDRESS" in text:
        raise RuntimeError("layer1d 疑似重新尝试改写 CreateFileA IAT；当前只允许继承低层调用点 shim。")

    return [
        "layer1d 未破坏外传 v0.2.1/v0.2.0 Steam 稳定兼容边界",
        "ComeOn.dll 全局 CreateWindowExA Hook 与官方 EDIT WndProc 路径均保留",
        "只对 NULL/MAKEINTATOM 类名跳过 ComeOn.dll 的字符串解引用与 EDIT 后处理",
        "test1/test2 旧隔离代码及 test3-test9 影片失败实验均未回到当前运行源码",
    ]



def validate_layer1d_scope(source_path: Path) -> list[str]:
    """
    检查 layer1d 的运行边界，防止 test1~test7 和 layer1b 的失败路线重新混回源码。

    layer1d 的硬规则：
    1. 不移动物品/装备/技能 GUI，不修改鼠标坐标、self+0xA8、child、active 或键盘业务；
    2. 完全禁止 layer1b 的 manager+0x18/+0x1C、object+0x08/+0x0C 顶层链重排；
    3. 继续使用 layer1a 已实机通过的“HUD 后延迟 Draw”，并允许动态纳入菜单体系的独立顶层辅助面板；
    4. 输入只 Hook 原版顶层 picker 的单次 CALL：先执行原 picker，只有它选中 HUD 且鼠标命中已延后绘制的
       可输入菜单 root 时才替换返回值；manager+0x40 必须继续由原版 0x4B44D0 更新；
    5. 手工候选必须镜像原版 JMM 属性 0x0D==1 门槛，并优先使用原版 0x4B1D30 的顶层命中矩形；
    6. 已在合法菜单上下文中确认的独立面板允许做短生命周期跟踪，但每帧必须重新验证：仍在真实 Draw 链、active、
       vtable 未变且仍与 HUD 相交；主菜单关闭后不得凭几何关系发现新的未知对象；
    7. UI manager 的 HUD 特殊绘制 pass 仍只验证，不 Hook、不重放。
    """

    if not source_path.is_file():
        raise RuntimeError(f"缺少主源码：{source_path}")

    text = source_path.read_text(encoding="utf-8")

    required = (
        '"AuxiliaryUIAboveHUD"',
        "g_auxiliary_ui_above_hud",
        "UI_MANAGER_DRAW_CALLSITE_PATTERN",
        "UI_TOP_LEVEL_PICK_CALLSITE_PATTERN",
        "install_auxiliary_ui_draw_layer_hook",
        "ui_manager_draw_layer_scope_hook",
        "ui_top_level_pick_layer_hook",
        "layer_main_hud_draw_hook",
        "layer_equipment_draw_hook",
        "layer_skill_draw_hook",
        "layer_inventory_draw_hook",
        "layer_dynamic_auxiliary_draw_hook",
        "ensure_dynamic_layer_hooks_for_current_menu",
        "layer_find_input_override_root",
        "layer_object_has_active_deferred_draw_wrapper",
        "layer_top_level_root_accepts_mouse_input",
        "TrackedAuxiliaryObject",
        "layer_track_auxiliary_object",
        "layer_refresh_tracked_auxiliary_objects",
        "layer_is_tracked_auxiliary_object",
        "layer_clear_all_tracked_auxiliary_objects",
        "辅助GUI独立面板主窗口关闭后继续置于HUD上方",
        "g_original_ui_top_level_pick",
        "g_original_ui_get_hit_rect",
        "g_original_ui_property_get",
        "input_picker[0x4Cu]",
        "input_picker[0xADu]",
        "input_picker + 0x67u",
        "input_picker + 0x5Bu",
        "patch_rel32_call(input_pick_call, (LPVOID)&ui_top_level_pick_layer_hook)",
        "manager+0x40仍由原版0x4B44D0更新",
        "g_layer_draw_scope_depth == 1u",
        "original_current_after_draw",
        "decode_rel32_target(manager_draw + 34u)",
        "UI manager 的原版HUD特殊绘制pass保持原样，只执行一次",
    )
    for marker in required:
        if marker not in text:
            raise RuntimeError(f"layer1d 缺少必要源码锚点：{marker}")

    # test1~test7 的菜单位移/坐标补偿路线继续严格禁止。
    forbidden_old = (
        "apply_modal_safe_area",
        "g_modal_safe_area_enabled",
        "move_top_level_ui_x",
        "place_modal_target_absolute",
        "sync_bag_panel_with_item_delta",
        "modal_hit_update_hook",
        "repair_modal_action_hit_child",
        "ensure_modal_menu_hit_hooks",
        "g_bag_manual_sync_enabled",
    )
    for marker in forbidden_old:
        if marker in text:
            raise RuntimeError(f"layer1d 混入 test1~test7 失败运行代码：{marker}")

    # layer1b 已被实机证明没有执行成功；其“直接重排顶层链”的函数必须从运行源码彻底消失。
    forbidden_layer1b = (
        "layer_validate_bidirectional_top_level_chain",
        "layer_move_top_level_node_after",
        "layer_promote_overlapping_menu_nodes_after_hud",
        "辅助GUI Z顺序提升",
        "顶层双向链验证失败",
    )
    for marker in forbidden_layer1b:
        if marker in text:
            raise RuntimeError(f"layer1d 仍残留 layer1b 顶层链重排实现：{marker}")

    # 精确禁止对四个顶层链指针的赋值。读取这些偏移用于原版结构验证/候选扫描是允许的。
    forbidden_writes = (
        "*(LPVOID*)((BYTE*)manager + 0x18u) =",
        "*(LPVOID*)((BYTE*)manager + 0x1Cu) =",
        "*(LPVOID*)((BYTE*)node + 0x08u) =",
        "*(LPVOID*)((BYTE*)node + 0x0Cu) =",
    )
    for marker in forbidden_writes:
        if marker in text:
            raise RuntimeError(f"layer1d 禁止写顶层链，但发现赋值：{marker}")

    return [
        "layer1d 运行边界正确：不移动 GUI、不写顶层链、不做鼠标坐标补偿",
        "0x0B/0x0D/0x0E 继续使用 HUD 后延迟 Draw；独立辅助顶层对象支持动态 Draw wrapper",
        "输入只覆盖原版顶层 picker 的单次返回值，manager+0x40 仍由原版 0x4B44D0 更新",
        "输入候选必须满足原版 JMM 属性 0x0D==1，并使用原版顶层命中矩形路径",
        "独立面板短生命周期跟踪存在：主 root 关闭后只保留已确认对象，并逐帧复核 Draw 链/active/vtable/HUD 相交",
        "HUD 特殊 pass 不 Hook/不重放；嵌套 manager Draw 不参与外层延迟队列",
        "帧末兜底补画后恢复原版 manager+0x20 返回值，不假定必须为 NULL",
        "test1~test7 菜单位移/输入补偿与 layer1b 顶层链重排运行代码=0",
    ]


def validate_stripe1_width_alignment(source_path: Path) -> list[str]:
    """
    检查 stripe1 唯一新增的运行时变量：TargetWidth 必须按最接近的 8 像素边界计算。

    这一轮绝不能借修竖条之名重新改 GUI、输入或 Strategy。这里不尝试完整解析 C AST，
    只锁住几条足够稳定、能明确代表新算法的源码锚点，并禁止旧版“奇数补成偶数”逻辑继续存在。
    """

    if not source_path.is_file():
        raise RuntimeError(f"缺少主源码：{source_path}")

    text = source_path.read_text(encoding="utf-8")

    required = (
        "floor_width = product / aspect_height;",
        "remainder = product % aspect_height;",
        "block_base = floor_width & ~7u;",
        "block_offset = floor_width & 7u;",
        "width = block_base + 8u;",
        "stripe1目标宽度8像素对齐余数=",
    )
    for marker in required:
        if marker not in text:
            raise RuntimeError(f"stripe1 缺少 8 像素对齐源码锚点：{marker}")

    forbidden = (
        "if ((width & 1u) != 0u)",
        "老 DirectDraw 对偶数宽度更友好，奇数仍然向上补成偶数",
    )
    for marker in forbidden:
        if marker in text:
            raise RuntimeError(f"stripe1 仍残留旧版偶数宽度算法：{marker}")

    return [
        "stripe1 TargetWidth 已改为最接近的 8 像素倍数",
        "旧版仅偶数对齐运行代码=0",
        "运行日志包含 TargetWidth%8 诊断字段",
    ]

def validate_chinese_runtime_logging(source_path: Path, expected_version: str) -> list[str]:
    """
    检查真正编译进 ASI 的 C 字符串字面量是否已经完成日志中文化。

    这里故意只分析双引号字符串，不直接搜索整个源码：历史研究注释里需要保留旧版英文日志，
    如果直接全文查 `[RUNTIME]` 会把正确保留的历史证据误报成回归。

    当前规则：
    1. 正式版本标题必须存在；
    2. 新日志必须至少包含 [成功]/[信息]/[失败]/[运行] 四类中文等级；
    3. 任何会编译进二进制的旧 [OK]/[INFO]/[WARN]/[FAIL]/[RUNTIME] 标签都禁止存在。
    """

    if not source_path.is_file():
        raise RuntimeError(f"缺少主源码：{source_path}")

    text = source_path.read_text(encoding="utf-8")
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', text)

    if expected_version not in literals:
        raise RuntimeError(f"缺少当前中文日志版本标题：{expected_version}")

    joined = "\n".join(literals)
    for marker in ("[成功]", "[信息]", "[失败]", "[运行]"):
        if marker not in joined:
            raise RuntimeError(f"中文日志缺少等级标记：{marker}")

    forbidden = ("[OK]", "[INFO]", "[WARN]", "[FAIL]", "[RUNTIME]")
    for marker in forbidden:
        if marker in joined:
            raise RuntimeError(f"运行时字符串仍残留旧版英文日志标签：{marker}")

    return ["DisplayFix.log 运行时字符串已全面中文化，旧英文日志标签=0"]

def main() -> int:
    """命令行入口。成功返回 0，失败返回 1。"""

    # 如果用户显式给了路径，就验证那个路径。
    # 否则默认验证脚本上一级目录的 release\DisplayFix.asi。
    if len(sys.argv) >= 2:
        asi_path = Path(sys.argv[1]).resolve()
    else:
        # 新仓库结构固定为：
        #   <仓库根>\source\DisplayFix_WaiZhuan\tools\verify_build.py
        # 所以 parents[3] 才是仓库根目录。
        package_root = Path(__file__).resolve().parents[3]
        asi_path = package_root / "release" / "DisplayFix.asi"

    # INI 必须跟 ASI 同目录，否则用户直接拷贝 release 时配置会丢失。
    ini_path = asi_path.with_name("DisplayFix.ini")

    try:
        lines = validate_asi(asi_path)

        ini_lines = validate_ini(ini_path)

        # build.bat 与本工具固定同属 source\\DisplayFix_WaiZhuan；这里顺便验证构建脚本自身。
        build_bat = Path(__file__).resolve().parents[1] / "build.bat"
        build_lines = validate_build_bat(build_bat)

        package_root = Path(__file__).resolve().parents[3]
        bom_lines = validate_repository_bom_free(package_root)

        # v0.1-test2 新增：确保字体修复永远先于 Display.Enable 的提前退出。
        source_path = Path(__file__).resolve().parents[1] / "src" / "DisplayFix.c"
        logging_lines = validate_chinese_runtime_logging(source_path, "DisplayFix 外传 v0.2.3-stripe1")
        independence_lines = validate_font_display_independence(source_path)
        layer_lines = validate_layer1d_scope(source_path)
        stripe_lines = validate_stripe1_width_alignment(source_path)

        # layer1d：继续只允许 v0.2.1 已实机闭环的 CreateWindowExA class-atom 安全修复；图层改动不得碰这里。
        stable_scope_lines = validate_v021_stable_steam_scope(source_path)

        print(f"[验证目标] {asi_path}")
        for line in lines:
            print(f"[通过] {line}")
        for line in ini_lines:
            print(f"[通过] {line}")
        for line in build_lines:
            print(f"[通过] {line}")
        for line in bom_lines:
            print(f"[通过] {line}")
        for line in independence_lines:
            print(f"[通过] {line}")
        for line in layer_lines:
            print(f"[通过] {line}")
        for line in stripe_lines:
            print(f"[通过] {line}")
        for line in stable_scope_lines:
            print(f"[通过] {line}")
        for line in logging_lines:
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
