"""打包正式ASI、默认TOML、独立第三方声明和逐份保持原始字节的许可证。"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import sys
import tempfile
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[2]
# 许可证来源是只读的仓库原件，发行副本不能转换编码、换行、空白或加入说明。
LICENSE_FILES = {
    "LICENSE.txt": "LICENSE",
    "licenses/SDL-LICENSE.txt": "thirdparty/SDL-release-3.4.16/LICENSE.txt",
    "licenses/HIDAPI-LICENSE-bsd.txt": "thirdparty/SDL-release-3.4.16/src/hidapi/LICENSE-bsd.txt",
    "licenses/YUV-LICENSE.txt": "thirdparty/SDL-release-3.4.16/src/video/yuv2rgb/LICENSE",
    "licenses/CC0-1.0.txt": "docs/第三方许可/手柄图示CC0许可.txt",
}
# ZIP逐项收集，不递归包含debug、源码、游戏或私人资料。
PACKAGE_FILES = {
    "EDSlash.asi": "release/EDSlash.asi",
    "EDSlash.toml": "release/EDSlash.toml",
    "第三方声明.txt": "release/第三方声明.txt",
    **{name: "release/" + name for name in LICENSE_FILES},
    "licenses/SunPro-NOTICE.txt": "release/licenses/SunPro-NOTICE.txt",
}



def source_digest(root):
    """沿用统一构建器的输入摘要范围，用于发现构建后发生的源码变动。"""
    digest = hashlib.sha256()
    for path in sorted(p for p in (root / "source").rglob("*") if p.is_file()
                       and ".build" not in p.parts and "__pycache__" not in p.parts
                       and p.suffix.lower() in {".c", ".h", ".def", ".py", ".json", ".toml", ".txt", ".bat"}):
        digest.update(path.relative_to(root / "source").as_posix().encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def safe_file(root, relative):
    """文件必须留在输入根内，目录链接不能把私人文件引入发行包。"""
    # 文件与根必须使用相同的实际路径写法，临时目录的短路径或目录别名不能误报越界。
    root = root.resolve()
    path = root / relative
    if not path.is_file() or not path.resolve().is_relative_to(root):
        raise RuntimeError(f"缺少文件或路径越界：{relative}")
    # Windows目录联接也属于重解析点，不能只检查普通符号链接。
    for item in (path, *path.parents):
        if item == root:
            break
        if item.is_symlink() or getattr(item.lstat(), "st_file_attributes", 0) & 0x400:
            raise RuntimeError(f"发行输入包含链接：{relative}")
    return path



def license_contents(root):
    """读取原件字节；SunPro只有源码声明，保留注释块原字节并明确区别于独立许可证。"""
    contents = {name: safe_file(root, relative).read_bytes() for name, relative in LICENSE_FILES.items()}
    if any(not data for data in contents.values()):
        raise RuntimeError("许可证原件为空")
    math_source = safe_file(root, "thirdparty/SDL-release-3.4.16/src/libm/e_sqrt.c").read_bytes()
    begin = math_source.find(b"/*")
    end = math_source.find(b"*/", begin)
    if begin < 0 or end < 0 or b"Sun" not in math_source[begin:end]:
        raise RuntimeError("SunPro源码许可声明不存在")
    end += 2
    # 注释块后的原有行结束符属于提取片段，不添加或转换任何字节。
    if math_source[end:end + 2] == b"\r\n":
        end += 2
    elif math_source[end:end + 1] == b"\n":
        end += 1
    contents["licenses/SunPro-NOTICE.txt"] = math_source[begin:end]
    return contents


def write_license_files(root, directory):
    """本地发行与自动发行共享同一份原样复制清单，中文说明另存独立文件。"""
    contents = license_contents(root)
    contents["第三方声明.txt"] = safe_file(root, "docs/第三方许可/第三方声明.txt").read_bytes()
    for name, data in contents.items():
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    # 仅清理已停用的旧汇总文件，避免使用者误认为它是SDL上游原件。
    for name in ("第三方许可.txt", "LICENSE-SDL.txt"):
        old = directory / name
        if old.is_file():
            old.unlink()



def package(root, output):
    """校验报告只在打包时使用；固定文件清单不递归收集任何目录。"""
    root = root.resolve()
    output = output.resolve()
    if any(output.is_relative_to(root / tree)
           for tree in ("docs", "source", "thirdparty", ".github", "参考资料", "archive")):
        raise RuntimeError("输出不能位于文档、源码、SDL、工作流或只读参考目录内")
    if output.suffix.lower() != ".zip":
        raise RuntimeError("输出文件必须使用.zip扩展名")
    report = json.loads(safe_file(root, "release/统一构建验证.json").read_text(encoding="utf-8"))
    contents = {name: safe_file(root, relative).read_bytes() for name, relative in PACKAGE_FILES.items()}
    # 构建报告对应当前正式件，拒绝误取旧件或debug冒充发行件。
    asi = contents["EDSlash.asi"]
    if hashlib.sha256(asi).hexdigest() != report["ASI_SHA256"] or len(asi) != report["ASI字节数"]:
        raise RuntimeError("正式ASI与构建验证报告不匹配")
    if not report.get("UPX") or report.get("外置SDL3.dll") is not False:
        raise RuntimeError("发行报告未满足UPX与静态SDL契约")
    template = safe_file(root, "source/config/EDSlash.toml").read_bytes()
    config = contents["EDSlash.toml"]
    if template != config or hashlib.sha256(template).hexdigest() != report["默认模板SHA256"]:
        raise RuntimeError("自动发行只能携带与报告匹配的默认配置，不能发布个人配置")
    tomllib.loads(config.decode("utf-8"))
    originals = license_contents(root)
    for name, original in originals.items():
        if contents[name] != original:
            raise RuntimeError(f"许可证副本与原件字节不一致：{name}")
    notices = safe_file(root, "docs/第三方许可/第三方声明.txt").read_bytes()
    if contents["第三方声明.txt"] != notices:
        raise RuntimeError("第三方声明与仓库不匹配")
    if source_digest(root) != report["构建身份"]["源码与构建输入SHA256"]:
        raise RuntimeError("源码与构建报告不匹配，请先运行统一构建入口")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        # 先写临时ZIP，回读验证通过才替换目标；失败不损坏既有包。
        with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".zip", delete=False) as handle:
            temporary = Path(handle.name)
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in contents.items():
                archive.writestr(name, data)
        with zipfile.ZipFile(temporary) as archive:
            if len(archive.namelist()) != len(PACKAGE_FILES) or set(archive.namelist()) != set(PACKAGE_FILES):
                raise RuntimeError("ZIP文件清单与正式产物、独立声明及原件许可证清单不匹配")
            if archive.testzip() is not None:
                raise RuntimeError("ZIP完整性校验失败")
            for name, data in contents.items():
                if archive.read(name) != data:
                    raise RuntimeError(f"ZIP文件字节不一致：{name}")
        temporary.replace(output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    checksum = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"正式发行包完成：{output}；ASI、TOML、独立声明与许可证原件，共{len(PACKAGE_FILES)}个文件。")
    return checksum


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="打包正式ASI、默认TOML、独立第三方声明及原样许可证。")
    parser.add_argument("--output", type=Path, required=True, help="目标ZIP路径；不能放入源码或只读参考目录")
    args = parser.parse_args()
    checksum = package(ROOT, args.output)
    # 校验值只作为工作流内部输出传递，不生成玩家下载的额外文件。
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as handle:
            handle.write(f"sha256={checksum}\n")


if __name__ == "__main__":
    main()
