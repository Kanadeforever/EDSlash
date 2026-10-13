"""依次检查CI兼容编译器和本机默认编译器，不更新发行件或安装工具。"""
from pathlib import Path
import argparse
import os
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="使用已安装MSVC完成多版本离线检查")
    parser.add_argument("--versions", nargs="+", default=["14.44", "default"],
                        help="依次检查的版本，default表示本机默认版本")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    for version in args.versions:
        environment = os.environ.copy()
        # 不继承当前开发者终端的编译器选择；每次都重新探测，避免两次使用同一版本。
        environment.pop("VSCMD_ARG_TGT_ARCH", None)
        environment.pop("EDSLASH_MSVC_VERSION", None)
        if version != "default":
            environment["EDSLASH_MSVC_VERSION"] = version
        print(f"开始检查MSVC：{version}", flush=True)
        subprocess.run([sys.executable, "-B", "source/tools/build.py", "--checks-only"],
                       cwd=root, env=environment, check=True)
    print("全部指定编译器的构建及离线检查通过。")


if __name__ == "__main__":
    main()
