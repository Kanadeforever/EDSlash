"""核对无条件失败确实以约定状态退出，崩溃或其他错误码不能冒充通过。"""
import subprocess
import sys

result = subprocess.run([sys.argv[1], "exit"], capture_output=True)
if result.returncode != 9 or "断言失败退出回放".encode("utf-8") not in result.stderr:
    raise RuntimeError(f"失败入口返回值或诊断不符：{result.returncode}")
print("统一断言exit失败状态与诊断通过。")
