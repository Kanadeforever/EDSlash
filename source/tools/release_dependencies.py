"""读取固定官方发行件，校验下载字节并在内存中合并着色器包。"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import io
import urllib.request
import urllib.error
import http.client
import time
import tempfile
import os
import zipfile

# 每份下载都固定版本及摘要；缓存损坏与上游替换均停止打包，不降级取最新版本。
DOWNLOADS = {
    "cnc.zip": ("https://github.com/FunkyFr3sh/cnc-ddraw/releases/download/v7.1.0.0/cnc-ddraw.zip", "0b13ab89a64c9918189b1dadd449ef6ed3cb3b7b19cabd96d8adbd95505bb908"),
    "asi.zip": ("https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.0.0/Ultimate-ASI-Loader.zip", "915ef34e75177c434b7b41d9eae195af4d9f1ea1fab5d119d2104f5bd3263d05"),
    "cnc-license": ("https://raw.githubusercontent.com/FunkyFr3sh/cnc-ddraw/v7.1.0.0/LICENSE", "a705144535bbfccc05e8310c4f8ab5f0b6f0ec2c9371b76ca5a522cf90e031f5"),
    "asi-license": ("https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/v9.0.0/license", "ceca73c504e39084b0b80d2a437aea9988313d1915418d3c629c3e4a8313f5a1"),
    "gpl2": ("https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt", "edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6"),
    "cc-nc": ("https://creativecommons.org/licenses/by-nc-sa/3.0/legalcode.txt", "8812f83442fd0eca14eb0208988e190fdcbfebec58fa5459d3218edfdfdc5a32"),
}


def verified(directory, name):
    """只读取本轮缓存中的普通文件，逐字节摘要通过后才使用。"""
    path = directory / name
    if path.is_symlink() or not path.is_file() or path.resolve().parent != directory.resolve():
        raise RuntimeError(f"依赖缓存缺失或越界：{name}")
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != DOWNLOADS[name][1]:
        raise RuntimeError(f"依赖SHA-256不一致：{name}")
    return data


def read_zip(data, shaders_only=False):
    """拒绝越界、链接、重复名称和超大解压；嵌套包只能写Shaders目录。"""
    result = {}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        seen = set()
        if sum(item.file_size for item in archive.infolist()) > 64 * 1024 * 1024:
            raise RuntimeError("依赖ZIP解压体积异常")
        for item in archive.infolist():
            # orig_filename保留ZIP原始名称，避免Windows自动规范化掩盖反斜杠。
            name = item.orig_filename
            path = PurePosixPath(name)
            if (path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name
                    or name.casefold() in seen or (item.external_attr >> 16) & 0o170000 == 0o120000
                    or (shaders_only and (not path.parts or path.parts[0] != "Shaders"))):
                raise RuntimeError(f"依赖ZIP路径异常：{name}")
            seen.add(name.casefold())
            if not item.is_dir():
                result[name] = archive.read(item)
    return result


def dependency_contents(directory):
    """外层Shaders合并内层Shaders，内层同名文件覆盖，保留官方内嵌ZIP。"""
    directory = Path(directory)
    files = read_zip(verified(directory, "cnc.zip"))
    required = {"cnc-ddraw config.exe", "ddraw.dll", "ddraw.ini"}
    if not required.issubset(files) or any(name not in required and not name.startswith("Shaders/") for name in files):
        raise RuntimeError("cnc-ddraw发行清单异常")
    files.update(read_zip(files["Shaders/shader-package.zip"], shaders_only=True))
    loader = read_zip(verified(directory, "asi.zip"))
    if set(loader) != {"dinput8.dll"}:
        raise RuntimeError("ASI加载器必须是官方32位dinput8包")
    # 官方包使用dinput8名称；发行时仅改为游戏使用的dsound代理名，DLL字节保持原样。
    files["dsound.dll"] = loader["dinput8.dll"]
    for name, source in {
        "cnc-ddraw-LICENSE.txt": "cnc-license", "Ultimate-ASI-Loader-LICENSE.txt": "asi-license",
        "Shaders-GPL-2.0.txt": "gpl2", "Shaders-CC-BY-NC-SA-3.0.txt": "cc-nc",
    }.items():
        files["Third_Party_Licenses/" + name] = verified(directory, source)
    # 作者声明来自分发原件；各着色器源文件自身的许可头也按原字节保留。
    files["Third_Party_Licenses/Shaders-NOTICE.txt"] = files["Shaders/readme.txt"]
    return files


def fetch_verified(name, url, digest):
    """只对临时网络故障重试；错误摘要和永久HTTP错误立即拒绝。"""
    attempts = 4
    for attempt in range(1, attempts + 1):
        print(f"[依赖下载] {name}：第{attempt}/{attempts}次，{url}", flush=True)
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "EDSlash-release"})
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read(32 * 1024 * 1024 + 1)
        except (urllib.error.URLError, TimeoutError, ConnectionError, http.client.IncompleteRead) as error:
            # HTTPError是URLError的子类；只重试超时、限流和服务端暂时故障。
            transient = not isinstance(error, urllib.error.HTTPError) or error.code in (408, 429, 500, 502, 503, 504)
            if not transient or attempt == attempts:
                raise RuntimeError(f"依赖下载失败：{name}，地址={url}，尝试={attempt}/{attempts}，原因={error}") from error
            delay = 2 ** attempt
            print(f"[依赖重试] {name}：{error}；{delay}秒后重试", flush=True)
            time.sleep(delay)
            continue
        if len(data) > 32 * 1024 * 1024:
            raise RuntimeError(f"依赖下载体积异常：{name}")
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f"依赖下载SHA-256不一致：{name}")
        return data
    raise AssertionError("下载重试未返回结果")


def download(directory):
    """复用经过摘要核验的缓存；下载完成并核验后才原子替换目标文件。"""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    for name, (url, digest) in DOWNLOADS.items():
        path = directory / name
        if path.exists():
            verified(directory, name)
            print(f"[依赖缓存] {name}：SHA-256通过", flush=True)
            continue
        data = fetch_verified(name, url, digest)
        # 网络重试期间不创建目标文件；写入临时文件失败也不会留下半份有效缓存。
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(prefix=".download-", dir=directory, delete=False) as output:
                temporary = Path(output.name)
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            temporary.replace(path)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
    contents = dependency_contents(directory)
    print(f"固定依赖已验证，合并后共{len(contents)}个文件。")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="下载并验证固定发行依赖与原样许可。")
    parser.add_argument("--directory", type=Path, required=True)
    download(parser.parse_args().directory)
