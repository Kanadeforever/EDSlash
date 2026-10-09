"""一次构建同时验证发行件与完整调试件，静态SDL不需要外置运行库。"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
# 构建不在源码旁生成Python字节缓存，子进程也遵循同一约束。
sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
from toolchain import compiler, program
from controller.verify_profiles import verify_baselines
from verify_build import verify_upx_roundtrip, test_upx_roundtrip_rejections, verify, verify_variants, verify_debug_info, test_pdb_rejections, test_upx_debug_record_compatibility
from sync_config import plan as config_plan, apply as config_apply

SOURCE = Path(__file__).resolve().parents[1]
ROOT = SOURCE.parent
BUILD = ROOT / ".build" / "msvc"
RELEASE = ROOT / "release"


def run(arguments, environment):
    # 参数数组保留中文及空格路径，不通过字符串拼接生成额外Shell命令。
    subprocess.run([str(arg) for arg in arguments], check=True, env=environment, cwd=ROOT)


def prepare_package(directory, notices):
    """目录配置已经在发布前同步，此处只补随包许可。"""
    directory.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(notices, directory / "第三方许可.txt")


def cleanup_legacy_cache():
    """清理源码旁旧缓存；根目录缓存按发布/检查模式分别收尾。"""
    legacy = SOURCE / '.build'
    # 删除前验证最终位置；不允许符号链接把清理指向源码之外。
    if legacy.exists():
        if legacy.is_symlink() or legacy.resolve() != SOURCE.resolve() / '.build':
            raise RuntimeError('旧构建目录位置异常，拒绝清理')
        shutil.rmtree(legacy)
    for cache in list(SOURCE.rglob('__pycache__')):
        if cache.is_symlink() or not cache.resolve().is_relative_to(SOURCE.resolve()):
            raise RuntimeError('源码字节缓存位置异常，拒绝清理')
        shutil.rmtree(cache)
    print('源码旧编译缓存已清理；本轮编译与诊断统一在根目录.build。')


def cleanup_build_cache():
    """产物/配置验证完成才清理临时目录；失败或检查模式保留诊断。"""
    target=(ROOT / '.build').resolve()
    # Python 3.11没有Path.is_junction，使用Windows原始重解析点属性兼容检测。
    def linked(path):
        return path.is_symlink() or bool(getattr(path.lstat(),'st_file_attributes',0)&0x400)
    if linked(target) or target!=ROOT.resolve()/'.build':
        raise RuntimeError('构建缓存目标异常，拒绝清理')
    # 不允许链接把递归清理带到项目外；整个检查与删除在同一Python进程完成。
    for item in target.rglob('*'):
        if linked(item) or not item.resolve().is_relative_to(target):
            raise RuntimeError('构建缓存内有链接或越界路径，拒绝清理')
    shutil.rmtree(target)
    print('正式双产物与配置已保存，根.build临时缓存已清理。')


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="一次构建EDSlash发行件和_debug完整版，并验证全部回归。")
    parser.add_argument("--checks-only", action="store_true", help="完整构建验证，但不更新release或源码模板")
    parser.add_argument("--keep-build", action="store_true", help="发布后保留构建缓存用于增量编译；默认完成后清理")
    parser.add_argument("--upx", action="store_true", default=True, help="兼容参数；正式发行默认UPX --best --lzma，调试件不压缩")
    parser.add_argument("--jobs",type=int,default=6,help="MSVC并行编译数，默认6")
    args = parser.parse_args()
    if args.jobs<1:parser.error("--jobs必须大于0")
    # 同步器先在临时配置上回归，不能直接拿用户发布配置当测试数据。
    run([sys.executable, SOURCE / 'tools/test_sync_config.py'], os.environ.copy())
    run([sys.executable, SOURCE / 'tools/font_fix/test_font_fix.py'], os.environ.copy())
    # 真实原指令须能拒绝旧错误地图地址，不能把模拟对象回放当成档案地址正确的证据。
    run([sys.executable, SOURCE / 'tools/controller/test_profiles.py'], os.environ.copy())
    run([sys.executable, SOURCE / 'tools/qol/test_random_name.py'], os.environ.copy())
    run([sys.executable, SOURCE / 'tools/generate_focus_profiles.py'], os.environ.copy())
    run([sys.executable, SOURCE / 'tools/generate_settings_profiles.py'], os.environ.copy())
    run([sys.executable, SOURCE / 'tools/generate_random_name_profiles.py'], os.environ.copy())
    run([sys.executable, SOURCE / 'tools/generate_controller_art.py', '--check'], os.environ.copy())
    # 源码与构建输入摘要包含未提交内容，不以HEAD冒充当前产物。
    digest = hashlib.sha256()
    inputs = sorted(p for p in SOURCE.rglob("*") if p.is_file() and
                    ".build" not in p.parts and "__pycache__" not in p.parts and
                    p.suffix.lower() in {".c", ".h", ".def", ".py", ".json", ".toml", ".txt", ".bat"})
    for path in inputs:
        digest.update(path.relative_to(SOURCE).as_posix().encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
    build_id = digest.hexdigest()
    cc, environment = compiler()
    cmake = program("cmake", environment)
    ninja = program("ninja", environment)
    # MSVC同一个cl.exe编译C与SDL的C++单元；x86开发环境由探测器准备。
    cxx = cc
    upstream = ROOT / "thirdparty/SDL-release-3.4.16"
    if not (upstream / "CMakeLists.txt").is_file():
        raise RuntimeError("缺少固定SDL 3.4.16完整源码，请从完整项目包恢复thirdparty。")
    notices = ROOT / "docs/第三方许可/配置与SDL第三方许可.txt"
    if not notices.is_file():
        raise RuntimeError("缺少第三方许可文档，请从完整项目包恢复docs。")
    BUILD.mkdir(parents=True, exist_ok=True)
    run([sys.executable, SOURCE / "tools/controller/generate_profiles.py"], environment)
    metadata = json.loads((SOURCE / "tools/controller/profiles.json").read_text(encoding="utf-8"))
    samples = verify_baselines(metadata)
    run([cmake, "--log-level=WARNING", "-S", SOURCE, "-B", BUILD, "-G", "Ninja",
         f"-DCMAKE_MAKE_PROGRAM={ninja}", f"-DCMAKE_C_COMPILER={cc}",
         f"-DCMAKE_CXX_COMPILER={cxx}", "-DCMAKE_BUILD_TYPE=Release", f"-DEDSLASH_BUILD_ID={build_id}"], environment)
    run([cmake, "--build", BUILD, "--parallel", str(args.jobs), "--", "--quiet"], environment)
    ctest = str(Path(cmake).with_name("ctest.exe"))
    if not Path(ctest).is_file():
        ctest = program("ctest", environment)
    run([ctest, "--test-dir", BUILD, "--output-on-failure"], environment)

    # PDB独立于ASI；发行和调试复制同一个优化链接件，不再依赖GNU strip。
    linked, config = BUILD / "EDSlash.asi", BUILD / "EDSlash.toml"
    debug_asi = BUILD / "EDSlash_debug.asi"
    dist = BUILD / "publish"
    dist.mkdir(exist_ok=True)
    release_asi = dist / "EDSlash.asi"
    shutil.copyfile(linked, debug_asi)
    shutil.copyfile(linked, release_asi)
    pdb = BUILD / "EDSlash.pdb"
    debug_evidence = verify(debug_asi, config)
    evidence = verify(release_asi, config)
    evidence["压缩前发行与调试运行段一致"] = verify_variants(debug_asi, release_asi)
    evidence["源码调试资料与发行隔离"] = verify_debug_info(debug_asi, release_asi, pdb)
    evidence["PDB错误拒绝回放"] = test_pdb_rejections(debug_asi, release_asi, pdb)
    # CTest已加载链接件；这里再验证发行和重命名调试件都能独立加载。
    run([BUILD / "test_load.exe", debug_asi], environment)
    run([BUILD / "test_load.exe", release_asi], environment)
    evidence["完整版"] = {"文件": "debug/EDSlash_debug.asi",
                          "SHA256": debug_evidence["ASI_SHA256"],
                          "字节数": debug_evidence["ASI字节数"],
                          "源码调试信息": True, "优化": "与发行件相同"}
    evidence["完整版"]["PDB"] = {"文件": "debug/EDSlash.pdb", "SHA256": hashlib.sha256(pdb.read_bytes()).hexdigest()}
    evidence["编译器"] = "原生MSVC，Win32/x86，静态运行库/MT"
    evidence["四官方EXE静态复核"] = samples
    evidence["构建身份"] = {"源码与构建输入SHA256": build_id, "架构": "单ASI、统一TOML及日志、官方静态SDL3.4.16"}
    # 脚本只产生离线证据；历史用户反馈不附到新产物。
    evidence["本轮验证"] = {"宿主回归": "CTest全部通过", "双产物加载": "非游戏进程通过",
                            "实机": "待用户测试；历史分项反馈见docs/文档/版本与更新记录.md"}
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
            raise RuntimeError("未找到UPX；正式发行需要UPX，请加入PATH或设置UPX_BIN。")
        # 只压缩发行副本；PDB和调试ASI不经过UPX。
        packed = dist / "EDSlash-upx.asi"
        shutil.copyfile(release_asi, packed)
        run([upx, "--best", "--lzma", packed], environment)
        run([upx, "-t", packed], environment)
        run([BUILD / "test_load.exe", packed], environment)
        # 解压回放再次核对原运行段，避免只看压缩工具的退出状态就交付。
        unpacked = dist / "EDSlash-unpacked.asi"
        if unpacked.exists():unpacked.unlink()  # 仅重建本轮.build/publish内的解压验证副本。
        run([upx, "-d", "-o", unpacked, packed], environment)
        verify(unpacked, config)
        evidence["UPX解压与压缩前运行段一致"] = verify_upx_roundtrip(release_asi, unpacked)
        evidence["UPX损坏拒绝回放"] = test_upx_roundtrip_rejections(release_asi, unpacked)
        evidence["UPX空调试槽兼容回放"] = test_upx_debug_record_compatibility(release_asi, unpacked)
        evidence["压缩前发行件"] = {"SHA256": evidence["ASI_SHA256"], "字节数": evidence["ASI字节数"]}
        evidence["ASI_SHA256"] = hashlib.sha256(packed.read_bytes()).hexdigest()
        evidence["ASI字节数"] = packed.stat().st_size
        evidence["UPX"] = {"源": "MSVC优化发行副本（调试符号外置PDB）", "参数": "--best --lzma", "正式位置": "release/EDSlash.asi", "SHA256": hashlib.sha256(packed.read_bytes()).hexdigest(),
                           "字节数": packed.stat().st_size, "压缩完整性及非游戏加载": "通过",
                           "本轮新文件实机": "尚未单独复测"}
    evidence["PE静态核对对象"] = "压缩前发行件及UPX解压复核件；正式压缩件另做UPX完整性和加载检查"
    evidence["范围"] = "四官方准确EXE；新游戏两层/随机名称/设置入口/统一按钮/逐候选容量与堆叠，以及原暂停热应用/战斗/动作菜单/快捷格/360度交互完整回归"
    evidence["验收边界"] = "原EXE静态及宿主回归不能证明GUI、脚本、设备、地图或Steam DLL全部通过"
    if args.checks_only:
        cleanup_legacy_cache()
        print("发行件／完整版及全部离线检查通过；未更新发布目录。")
        return

    # 全部目录配置先通过验证，随后一起发布本轮压缩发行与debug，避免配置和二进制不配套。
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
    shutil.copyfile(packed, RELEASE / "EDSlash.asi")
    prepare_package(debug_directory, notices)
    shutil.copyfile(debug_asi, debug_directory / debug_asi.name)
    shutil.copyfile(pdb, debug_directory / pdb.name)
    if packed:
        prepare_package(packed_directory, notices)
        shutil.copyfile(packed, packed_directory / "EDSlash.asi")
    evidence["发布配置"] = "本轮压缩发行/调试及UPX同件副本补入模板新增键；原值/绑定/注释保留"
    evidence["配置新增键"] = config_changes
    evidence["默认模板SHA256"] = hashlib.sha256(config.read_bytes()).hexdigest()
    report = json.dumps(evidence, ensure_ascii=False, indent=2) + "\n"
    (RELEASE / "统一构建验证.json").write_bytes(report.replace("\n", "\r\n").encode("utf-8"))
    shutil.copyfile(config, SOURCE / "config/EDSlash.toml")
    cleanup_legacy_cache()
    print(f"双产物完成：{RELEASE / 'EDSlash.asi'} 与 {debug_directory / debug_asi.name}")
    print("两者优化代码相同；发行副本UPX --best --lzma，调试ASI配套EDSlash.pdb且不压缩；无需SDL3.dll。")
    if not args.keep_build:cleanup_build_cache()


if __name__ == "__main__":
    main()
