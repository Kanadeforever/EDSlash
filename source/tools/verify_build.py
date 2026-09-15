#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BladeSwordQOL v0.1-dev1 构建/源码防回归验证器。

这个工具不运行游戏，它负责在发布前检查“一个 ASI + 两个 Profile + 模块化 Runtime”的结构有没有被意外破坏。
检查尽量使用 Python 标准库，宿主只要有 Python 3 就能运行，不需要联网安装第三方包。
"""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"[失败] {message}")
    raise SystemExit(1)


def ok(message: str) -> None:
    print(f"[通过] {message}")


def read_text(path: Path) -> str:
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        fail(f"文本文件带 UTF-8 BOM：{path}")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as exc:
        fail(f"文本文件不是有效 UTF-8：{path} ({exc})")


def parse_pe(path: Path) -> dict[str, int | bytes]:
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        fail("ASI 不是有效 MZ/PE 文件")
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_off + 0xF8 > len(data) or data[pe_off:pe_off + 4] != b"PE\0\0":
        fail("ASI 缺少有效 PE 头")
    machine, sections, _timestamp, _symptr, _symcount, opt_size, characteristics = struct.unpack_from(
        "<HHIIIHH", data, pe_off + 4
    )
    opt = pe_off + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    entry = struct.unpack_from("<I", data, opt + 16)[0]
    number_rva = struct.unpack_from("<I", data, opt + 92)[0]
    import_rva = import_size = export_rva = export_size = 0
    if number_rva >= 2:
        export_rva, export_size = struct.unpack_from("<II", data, opt + 96)
        import_rva, import_size = struct.unpack_from("<II", data, opt + 104)
    return {
        "machine": machine,
        "sections": sections,
        "opt_size": opt_size,
        "characteristics": characteristics,
        "magic": magic,
        "entry": entry,
        "import_rva": import_rva,
        "import_size": import_size,
        "export_rva": export_rva,
        "export_size": export_size,
        "data": data,
    }


def extract_function(text: str, signature: str) -> str:
    """按函数签名找到一段 C 函数正文，用花括号深度精确截取，供本体/外传共通算法对比。"""
    start = text.find(signature)
    if start < 0:
        fail(f"找不到函数：{signature}")
    brace = text.find("{", start)
    if brace < 0:
        fail(f"函数没有正文：{signature}")
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    fail(f"函数花括号不闭合：{signature}")
    return ""


def main() -> None:
    if len(sys.argv) != 2:
        fail("用法：python source\\BladeSwordQOL\\tools\\verify_build.py release\\BladeSwordQOL.asi")

    asi = Path(sys.argv[1]).resolve()
    if not asi.is_file():
        fail(f"找不到 ASI：{asi}")

    # 工具位于 <包根>\source\BladeSwordQOL\tools\。
    # project 指源码工程目录，root 指固定的 docs/source/release 包根。
    project = Path(__file__).resolve().parents[1]
    root = Path(__file__).resolve().parents[3]
    release = root / "release"
    ini = release / "BladeSwordQOL.ini"
    if not ini.is_file():
        fail("release 中缺少 BladeSwordQOL.ini")

    # 包根是长期固定交付契约：只能有 docs/source/release 三个目录。
    # 这样源码重构不会把 src/tools/template/build.bat 再泄漏到包根。
    top_level = sorted(p.name for p in root.iterdir())
    if top_level != ["docs", "release", "source"]:
        fail(f"包根结构错误，应严格只有 docs/source/release，实际为：{top_level}")
    if not (root / "docs").is_dir() or not (root / "source").is_dir() or not release.is_dir():
        fail("docs/source/release 必须全部是目录")
    if project != root / "source" / "BladeSwordQOL" or not project.is_dir():
        fail("统一源码工程必须位于 source\\BladeSwordQOL\\")
    ok("包根严格保持 docs/source/release 三目录，统一工程位于 source\\BladeSwordQOL")

    pe = parse_pe(asi)
    if pe["machine"] != 0x014C or pe["magic"] != 0x010B:
        fail("ASI 必须是 Win32/x86 PE32")
    if not (pe["characteristics"] & 0x2000):
        fail("PE 没有 DLL 标志")
    if pe["entry"] == 0:
        fail("PE 入口点为 0")
    if pe["import_rva"] != 0 or pe["import_size"] != 0:
        fail("统一 ASI 出现 Import Table；当前项目要求继续保持零额外导入")
    if pe["export_rva"] == 0 or b"InitializeASI\x00" not in pe["data"]:
        fail("ASI 没有导出 InitializeASI")
    ok("PE32/x86、DLL、入口点、零 Import Table、InitializeASI 导出")

    # release 只允许两个正式运行文件。
    release_files = sorted(p.name for p in release.iterdir() if p.is_file())
    if release_files != ["BladeSwordQOL.asi", "BladeSwordQOL.ini"]:
        fail(f"release 文件不符合规范：{release_files}")
    ok("release 严格只有 BladeSwordQOL.asi + BladeSwordQOL.ini")

    # 所有 Markdown 必须集中在 docs；项目其它目录出现 Markdown 就算结构回归。
    bad_md = [p for p in root.rglob("*.md") if root / "docs" not in p.parents]
    if bad_md:
        fail("发现 docs 外 Markdown：" + ", ".join(str(p.relative_to(root)) for p in bad_md))
    docs = sorted((root / "docs").glob("*.md"))
    if not docs:
        fail("docs 目录没有 Markdown")
    for doc in docs:
        read_text(doc)
        if not any("\u4e00" <= ch <= "\u9fff" for ch in doc.stem):
            fail(f"文档文件名不是简体中文命名：{doc.name}")
    ok(f"全部 {len(docs)} 份 Markdown 均集中在 docs，文件名为中文且 UTF-8 无 BOM")

    main_c = read_text(project / "src" / "Main.c")
    dao = read_text(project / "src" / "Modules" / "DisplayFix" / "Backend_DaoJian.c")
    wz = read_text(project / "src" / "Modules" / "DisplayFix" / "Backend_WaiZhuan.c")
    runtime = read_text(project / "src" / "Runtime" / "Runtime.c")
    profiles = read_text(project / "src" / "Runtime" / "GameProfile.c")
    hooks = read_text(project / "src" / "Runtime" / "HookManager.c")
    events = read_text(project / "src" / "Runtime" / "EventBus.c")
    modules = read_text(project / "src" / "Runtime" / "ModuleRegistry.c")
    ini_text = read_text(ini)

    if main_c.count("DllMain(") != 1 or main_c.count("InitializeASI(") != 1:
        fail("Main.c 必须且只能定义一次 DllMain / InitializeASI")
    if "DllMain(" in dao or "InitializeASI(" in dao or "DllMain(" in wz or "InitializeASI(" in wz:
        fail("Profile 后端重新出现 DLL/ASI 入口")
    ok("唯一入口规则成立：只有 Main.c 拥有 DllMain / InitializeASI")

    for token in ("0x00173000ul", "0x0010F0EFul", "0x001A5000ul", "0x00127BCFul"):
        if token not in profiles:
            fail(f"GameProfile 身份常量缺失：{token}")
    ok("Runtime 会在访问 Profile 专用 IAT 前识别本体/外传 PE 身份")

    if "ModuleRegistry_InitializeAll" not in runtime or "DisplayFixModule_Initialize" not in modules:
        fail("Runtime 没有通过 ModuleRegistry 初始化 DisplayFix")
    if "HookManager_Claim" not in hooks or "EventBus_Emit" not in events:
        fail("共享 Hook / EventBus 基础设施缺失")
    if "HookManager_ReleaseOwned" not in hooks or "ModuleRegistry_IsInitialized" not in modules:
        fail("共享 Hook 声明回滚或模块故障隔离基础设施缺失")
    if "g_module_initialized" not in modules or "return 1;" not in modules:
        fail("ModuleRegistry 没有保留独立模块状态/继续初始化语义")
    for event in ("RUNTIME_EVENT_GAMEPLAY_ENTER", "RUNTIME_EVENT_GAMEPLAY_EXIT", "RUNTIME_EVENT_UI_DRAW_BEGIN", "RUNTIME_EVENT_UI_DRAW_END"):
        if event not in dao or event not in wz:
            fail(f"两个 Profile 后端没有同时广播共享事件：{event}")
    ok("ModuleRegistry、HookManager、EventBus、Hook声明回滚与模块故障隔离基础设施均存在")

    # 8 像素宽度规则必须属于两个后端共同的 calculate_target_width，而不是 Auto 分支或 StripeFix 模块。
    dao_calc = extract_function(dao, "static DWORD calculate_target_width")
    wz_calc = extract_function(wz, "static DWORD calculate_target_width")
    if dao_calc != wz_calc:
        fail("本体/外传 calculate_target_width 已分叉；AspectRatio 共通算法必须只有同一语义")
    for token in ("floor_width & ~7u", "floor_width & 7u", "block_base + 8u"):
        if token not in dao_calc:
            fail(f"AspectRatio 8 像素对齐逻辑缺失：{token}")
    if "AspectRatio=Auto" not in ini_text or "手工 W:H" not in ini_text or "不是 Auto 专项" not in ini_text:
        fail("INI 没有明确说明 8 像素对齐适用于整个 AspectRatio 配置项")
    if (project / "src" / "Modules" / "StripeFix").exists():
        fail("发现错误的独立 StripeFix 模块")
    ok("8 像素对齐属于 AspectRatio 统一宽度求值，Auto 与手工比例都会经过同一算法")

    # 刚刚实机闭合的 layer1d 关键符号必须同时存在于两个后端。
    layer_tokens = (
        "layer_refresh_tracked_auxiliary_objects",
        "RUNTIME_EVENT_UI_DRAW_BEGIN",
        "辅助GUI独立面板主窗口关闭后继续置于HUD上方",
        "install_auxiliary_ui_draw_layer_hook",
    )
    for token in layer_tokens:
        if token not in dao or token not in wz:
            fail(f"layer1d 防回归符号缺失：{token}")
    ok("layer1d GUI 绘制/输入与独立面板生命周期代码同时保留在两个 Profile")

    # 主 HUD 居中后的 0x0B/0x0E 原版按压动画必须通过同一个 root picker 修复，禁止手工伪造 pressed 状态。
    animation_tokens = (
        "主HUD按钮按压root校正",
        "identify_top_button_at_point(hud, point->x, point->y, &hit_child)",
        "return hud;",
        "if (hud_result && config.center_main_hud)",
        "0x0B/0x0E按压动画使用原版root路径恢复",
    )
    for token in animation_tokens:
        if token not in dao or token not in wz:
            fail(f"两个 Profile 没有同时保留 HUD 按压动画 root 修复：{token}")
    if "hud_result && config.center_main_hud && config.auxiliary_ui_above_hud" in dao or \
       "hud_result && config.center_main_hud && config.auxiliary_ui_above_hud" in wz:
        fail("HUD 按压动画 root 修复又被错误绑定到 AuxiliaryUIAboveHUD 开关")
    for forbidden in ("pressed =", "set_pressed", "button_pressed"):
        # 历史注释可以出现普通英文 pressed，但这里仅禁止明显的手工 pressed 赋值/函数命名。
        if forbidden in dao or forbidden in wz:
            fail(f"发现疑似手工伪造按钮 pressed 状态：{forbidden}")
    ok("0x0B/0x0E 点击动画通过原版 root picker 路径恢复，并与 AuxiliaryUIAboveHUD 开关解耦")

    # 外传 Steam 的两条红线用已确认源码标识锁住。
    wz_redlines = ("ResJM.Lib", "CreateWindowExA", "EDIT")
    for token in wz_redlines:
        if token not in wz:
            fail(f"外传 Steam 兼容红线源码标识缺失：{token}")
    ok("外传 Steam ResJM.Lib 与 ComeOn.dll/EDIT 专项代码仍在")

    # 统一文件名不能回归旧 DisplayFix.ini/log。
    for text, name in ((dao, "本体"), (wz, "外传")):
        if 'make_sibling_path(module_path, "BladeSwordQOL.ini"' not in text:
            fail(f"{name}后端没有使用统一 BladeSwordQOL.ini")
        if 'make_sibling_path(module_path, "BladeSwordQOL.log"' not in text:
            fail(f"{name}后端没有使用统一 BladeSwordQOL.log")
    ok("两个 Profile 共用 BladeSwordQOL.ini / BladeSwordQOL.log")

    digest = hashlib.sha256(asi.read_bytes()).hexdigest()
    print(f"[信息] BladeSwordQOL.asi SHA-256={digest}")
    print("[成功] BladeSwordQOL v0.1-dev1 静态构建验证全部通过")


if __name__ == "__main__":
    main()
