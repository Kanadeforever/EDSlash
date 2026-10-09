"""探测原生MSVC Win32工具链；不依赖本机路径记录，也不下载编译器。"""
from pathlib import Path
import os
import shutil
import subprocess

def compiler():
    # 已进入x86开发者终端时直接复用；普通终端用vswhere探测VS或Build Tools。
    environment = {key.upper(): value for key, value in os.environ.items()}
    if environment.get("VSCMD_ARG_TGT_ARCH", "").lower() != "x86":
        installation = environment.get("EDSLASH_VS")
        if not installation:
            base = Path(environment.get("PROGRAMFILES(X86)", "C:/Program Files (x86)"))
            vswhere = base / "Microsoft Visual Studio/Installer/vswhere.exe"
            if not vswhere.is_file():
                raise RuntimeError("未找到Visual Studio探测器；安装桌面C++和x86编译工具。")
            installation = subprocess.check_output(
                [str(vswhere), "-latest", "-products", "*", "-requires",
                 "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                encoding="utf-8").strip()
        script = Path(installation) / "VC/Auxiliary/Build/vcvarsall.bat"
        if not script.is_file():
            raise RuntimeError("未找到MSVC开发环境；安装桌面C++、x86工具和Windows SDK。")
        # 环境只传给构建子进程，不修改系统PATH，也不把环境中的秘密输出到日志。
        shell = environment.get("COMSPEC", str(Path(environment.get("SYSTEMROOT", "C:/Windows")) / "System32/cmd.exe"))
        codepage = Path(environment.get("SYSTEMROOT", "C:/Windows")) / "System32/chcp.com"
        command = f'"{codepage}" 65001 >nul && call "{script}" x64_x86 >nul && set'
        # cmd的/c参数使用它自己的整行引号规则，不能套用普通exe参数的反斜杠转义。
        result = subprocess.run(f'"{shell}" /d /s /c "{command}"', capture_output=True,
                                encoding="utf-8", errors="replace")
        if result.returncode:
            raise RuntimeError("MSVC开发环境初始化失败：" + result.stderr.strip())
        for line in result.stdout.splitlines():
            if "=" in line and not line.startswith("="):
                key, value = line.split("=", 1)
                environment[key.upper()] = value
        # VS初始化可能重置PATH；保留原终端工具目录，避免丢失用户配置的UPX。
        environment["PATH"] = environment.get("PATH", environment.get("Path", "")) + ";" + os.environ.get("PATH", "")
    # 某些Build Tools安装没有SDK注册信息；按已安装目录补齐UCRT/Win32头和库。
    # 选择同时包含stdio.h、Windows.h及x86库的版本，不猜固定SDK版本号。
    include = environment.get("INCLUDE", "")
    if not any((Path(item) / "stdio.h").is_file() for item in include.split(";") if item):
        kits = Path(environment.get("PROGRAMFILES(X86)", "C:/Program Files (x86)")) / "Windows Kits/10"
        versions = sorted((kits / "Include").glob("10.*"), reverse=True)
        for version in versions:
            if not (version / "ucrt/stdio.h").is_file() or not (version / "um/Windows.h").is_file():
                continue
            library = kits / "Lib" / version.name
            if not (library / "ucrt/x86/libucrt.lib").is_file() or not (library / "um/x86/kernel32.lib").is_file():
                continue
            environment["INCLUDE"] = include + ";" + ";".join(str(version / part) for part in ("ucrt", "shared", "um", "winrt"))
            environment["LIB"] = environment.get("LIB", "") + ";" + str(library / "ucrt/x86") + ";" + str(library / "um/x86")
            environment["PATH"] = str(kits / "bin" / version.name / "x64") + ";" + environment["PATH"]
            break
        else:
            raise RuntimeError("未找到完整Windows SDK/UCRT；请安装Windows SDK的x86开发库。")
    cc = shutil.which("cl.exe", path=environment["PATH"])
    if not cc or environment.get("VSCMD_ARG_TGT_ARCH", "").lower() != "x86":
        raise RuntimeError("未找到原生MSVC x86编译器；项目只构建Win32 ASI。")
    return Path(cc).resolve(), environment

def program(name, environment):
    # 使用与编译器相同的环境寻找CMake和Ninja，防止子进程找不到依赖。
    path = shutil.which(name, path=environment["PATH"])
    if not path:
        raise RuntimeError(f"未找到{name}，请安装并加入PATH；构建不会自动下载。")
    return path
