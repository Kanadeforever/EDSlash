"""在普通测试进程验证 Runtime 写入失败与 Hook 回滚，不启动或修改游戏。"""
from pathlib import Path
import importlib.util
import subprocess
import sys


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    tools = Path(__file__).resolve().parent
    source = tools.parent
    # 只复用已稳定的 32 位编译器探测，导入构建器不会执行其 main，也不会更新 release。
    spec = importlib.util.spec_from_file_location("controller_compiler", tools / "controller/build_controller.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    compiler, environment = builder.compiler()
    # 与两个发布目标分开，测试失败时保留文件以便定位，不清空其它目标目录。
    work = source / ".build/RuntimeChecks"
    work.mkdir(parents=True, exist_ok=True)
    executable = work / "test_runtime.exe"
    subprocess.run([
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
        str(tools / "test_runtime.c"), str(source / "src/Runtime/X86Detour.c"),
        "-o", str(executable),
    ], check=True, env=environment)
    # 执行的是自有测试程序，Windows API 失败由替身提供，不向游戏安装 Hook。
    subprocess.run([str(executable)], check=True, env=environment)


if __name__ == "__main__":
    main()
