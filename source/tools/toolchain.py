"""统一32位工具链探测；不读取任何个人编译器地址记录，也不自动下载依赖。"""
from pathlib import Path
import os
import shutil
import subprocess

def compiler():
    # 显式环境变量优先，其次PATH，最后是可移植的MSYS2常见布局。
    root = Path(os.environ.get("MSYS2_ROOT", os.environ.get("SystemDrive", "C:") + "/msys64"))
    candidates = [os.environ.get("EDSLASH_CC"), shutil.which("i686-w64-mingw32-gcc"),
                  shutil.which("gcc"), str(root / "mingw32/bin/gcc.exe")]
    for candidate in candidates:
        if not candidate or not Path(candidate).is_file():
            continue
        environment = os.environ.copy()
        environment["PATH"] = str(Path(candidate).parent) + os.pathsep + environment.get("PATH", "")
        result = subprocess.run([candidate, "-dumpmachine"], capture_output=True, text=True, env=environment)
        if result.returncode == 0 and result.stdout.strip().startswith(("i686-", "i386-")):
            return Path(candidate).resolve(), environment
    raise RuntimeError("未找到32位MinGW GCC；设置EDSLASH_CC、MSYS2_ROOT或将i686编译器加入PATH。")

def program(name, environment):
    # 使用与编译器相同的环境寻找CMake和Ninja，防止子进程找不到依赖。
    path = shutil.which(name, path=environment["PATH"])
    if not path:
        raise RuntimeError(f"未找到{name}，请安装并加入PATH；构建不会自动下载。")
    return path
