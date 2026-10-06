"""一次构建同时验证发行件与完整调试件，静态SDL不需要外置运行库。"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from toolchain import compiler, program
from controller.verify_profiles import verify_baselines
from verify_build import verify, verify_variants, verify_debug_info
from sync_config import plan as config_plan, apply as config_apply

SOURCE = Path(__file__).resolve().parents[1]
ROOT = SOURCE.parent
BUILD = SOURCE / ".build"
RELEASE = ROOT / "release"


def run(arguments, environment):
    # 参数数组保留中文及空格路径，不通过字符串拼接生成额外Shell命令。
    subprocess.run([str(arg) for arg in arguments], check=True, env=environment, cwd=ROOT)


def prepare_package(directory, notices):
    """目录配置已经在发布前同步，此处只补随包许可。"""
    directory.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(notices, directory / "第三方许可.txt")


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="一次构建EDSlash发行件和_debug完整版，并验证全部回归。")
    parser.add_argument("--checks-only", action="store_true", help="完整构建验证，但不更新release或源码模板")
    parser.add_argument("--upx", action="store_true", help="另压缩发行副本，调试件始终保留完整")
    args = parser.parse_args()
    # 同步器先在临时配置上回归，不能直接拿用户发布配置当测试数据。
    run([sys.executable, SOURCE / 'tools/test_sync_config.py'], os.environ.copy())
    # 真实原指令须能拒绝旧错误地图地址，不能把模拟对象回放当成档案地址正确的证据。
    run([sys.executable, SOURCE / 'tools/controller/test_profiles.py'], os.environ.copy())
    cc, environment = compiler()
    cmake = program("cmake", environment)
    ninja = program("ninja", environment)
    cxx = cc.with_name("g++.exe")
    if not cxx.is_file():
        raise RuntimeError("SDL含Windows C++后端，请使用完整32位MinGW工具链。")
    # 优先使用当前编译器配套strip，不固定个人工具路径，也不自动安装。
    strip = cc.with_name(cc.name.replace("gcc", "strip"))
    if not strip.is_file():
        strip = Path(program("strip", environment))
    upstream = ROOT / "thirdparty/SDL-release-3.4.16"
    if not (upstream / "CMakeLists.txt").is_file():
        raise RuntimeError("缺少固定SDL 3.4.16完整源码，请从完整项目包恢复thirdparty。")
    notices = ROOT / "docs/配置与SDL第三方许可.txt"
    if not notices.is_file():
        raise RuntimeError("缺少第三方许可文档，请从完整项目包恢复docs。")
    BUILD.mkdir(parents=True, exist_ok=True)
    run([sys.executable, SOURCE / "tools/controller/generate_profiles.py"], environment)
    metadata = json.loads((SOURCE / "tools/controller/profiles.json").read_text(encoding="utf-8"))
    samples = verify_baselines(metadata)
    run([cmake, "--log-level=WARNING", "-S", SOURCE, "-B", BUILD, "-G", "Ninja",
         f"-DCMAKE_MAKE_PROGRAM={ninja}", f"-DCMAKE_C_COMPILER={cc}",
         f"-DCMAKE_CXX_COMPILER={cxx}", "-DCMAKE_BUILD_TYPE=Release"], environment)
    run([cmake, "--build", BUILD, "--parallel", "6", "--", "--quiet"], environment)
    ctest = str(Path(cmake).with_name("ctest.exe"))
    if not Path(ctest).is_file():
        ctest = program("ctest", environment)
    run([ctest, "--test-dir", BUILD, "--output-on-failure"], environment)

    # 完整版只编译一次；发行件在副本上strip，绝不对原链接件直接去符号。
    linked, config = BUILD / "EDSlash.asi", BUILD / "EDSlash.toml"
    debug_asi = BUILD / "EDSlash_debug.asi"
    dist = BUILD / "publish"
    dist.mkdir(exist_ok=True)
    release_asi = dist / "EDSlash.asi"
    shutil.copyfile(linked, debug_asi)
    run([strip, "--strip-unneeded", "-o", release_asi, debug_asi], environment)
    debug_evidence = verify(debug_asi, config)
    evidence = verify(release_asi, config)
    evidence["发行与调试运行段一致"] = verify_variants(debug_asi, release_asi)
    evidence["源码调试资料与发行剥离"] = verify_debug_info(debug_asi, release_asi)
    # CTest已经加载完整链接件；这里还单独实际加载重命名件和strip件。
    run([BUILD / "test_load.exe", debug_asi], environment)
    run([BUILD / "test_load.exe", release_asi], environment)
    evidence["完整版"] = {"文件": "debug/EDSlash_debug.asi",
                          "SHA256": debug_evidence["ASI_SHA256"],
                          "字节数": debug_evidence["ASI字节数"],
                          "源码调试信息": True, "优化": "与发行件相同"}
    evidence["双基线静态复核"] = samples
    evidence["实机验收"] = {"性能候选1": "用户确认掉帧缓解、功能正常、实机通过",
                            "本轮双产物加载": "非游戏进程通过",
                            "本轮新文件实机": "尚未单独复测"}
    evidence["候选"] = "统一迁移性能候选1：后台日志、配置快速路径、5秒聚合计时"
    selected = {}
    for line in (BUILD / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(("SDL_VIDEO:", "SDL_DIRECTX:", "SDL_AUDIO:", "SDL_JOYSTICK:",
                            "SDL_HIDAPI:", "SDL_HAPTIC:", "SDL_SENSOR:", "SDL_STATIC:", "SDL_SHARED:")):
            key, value = line.split("=", 1)
            selected[key.split(":", 1)[0]] = value
    evidence["SDL构建选项"] = selected
    packed = None
    if args.upx:
        upx = os.environ.get("UPX_BIN") or shutil.which("upx", path=environment["PATH"])
        if not upx:
            raise RuntimeError("未找到UPX；加入PATH或设置UPX_BIN，基础构建不需要UPX。")
        # 只压缩已剥离的发行副本；_debug既不strip也不UPX。
        packed = dist / "EDSlash-upx.asi"
        shutil.copyfile(release_asi, packed)
        run([upx, "-9", packed], environment)
        run([upx, "-t", packed], environment)
        run([BUILD / "test_load.exe", packed], environment)
        evidence["UPX"] = {"源": "已剥离发行件", "SHA256": hashlib.sha256(packed.read_bytes()).hexdigest(),
                           "字节数": packed.stat().st_size, "压缩完整性及非游戏加载": "通过",
                           "本轮新文件实机": "尚未单独复测"}
    evidence["范围"] = "单ASI、静态SDL、TOML；本体/外传菜单、空存档尾页恢复、六物品区域X循环和原业务、持有图样中心锚点；仅外传手柄主菜单隐藏光标；构建配置补键"
    evidence["菜单验收"] = "双版本菜单、45槽与七处局部调用共52处回滚、六区域登记路由/隐藏过滤/原特殊槽/商店确认及视觉差异宿主通过；实际两作GUI/脚本/交易/属性/性能待验收"
    if args.checks_only:
        print("发行件／完整版及全部离线检查通过；未更新发布目录。")
        return

    # 先验证本轮要发布的全部配置，再发布ASI；没有重压UPX时不改旧包的配置。
    # 旧ASI可能不认识新键，不能只更新旧包TOML而留下旧二进制。
    debug_directory = RELEASE / "debug"
    packed_directory = RELEASE / "upx"
    directories = [RELEASE, debug_directory]
    if packed:
        directories.append(packed_directory)
    plans = [config_plan(directory / config.name, config.read_bytes()) for directory in directories]
    config_changes = {}
    for planned in plans:
        added = config_apply(planned)
        config_changes[str(planned[0].relative_to(RELEASE))] = added
        print(f"[配置同步] {planned[0]}：" + ("补入" + "、".join(added) if added else "已有选项完整，原文件保留"))
    # 调试件隔离在子目录，使用者只取其中一种，避免Loader一起发现两份ASI。
    prepare_package(RELEASE, notices)
    shutil.copyfile(release_asi, RELEASE / "EDSlash.asi")
    prepare_package(debug_directory, notices)
    shutil.copyfile(debug_asi, debug_directory / debug_asi.name)
    if packed:
        prepare_package(packed_directory, notices)
        shutil.copyfile(packed, packed_directory / "EDSlash.asi")
    evidence["发布配置"] = "本轮发行/调试及可选新UPX配置补入模板新增键；原值/绑定/注释保留；未重压旧UPX时整包不改"
    evidence["配置新增键"] = config_changes
    evidence["默认模板SHA256"] = hashlib.sha256(config.read_bytes()).hexdigest()
    report = json.dumps(evidence, ensure_ascii=False, indent=2) + "\n"
    (RELEASE / "统一构建验证.json").write_bytes(report.replace("\n", "\r\n").encode("utf-8"))
    shutil.copyfile(config, SOURCE / "config/EDSlash.toml")
    print(f"双产物完成：{RELEASE / 'EDSlash.asi'} 与 {debug_directory / debug_asi.name}")
    print("两者优化代码相同，发行件去符号，_debug保留源码调试信息；无需SDL3.dll。")


if __name__ == "__main__":
    main()
