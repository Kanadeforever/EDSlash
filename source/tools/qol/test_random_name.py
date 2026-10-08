"""单独编译并运行随机姓名生产核心；不构建 ASI，不读取游戏或写入发行目录。"""
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
SOURCE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SOURCE / "tools"))
from toolchain import compiler


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    # 数据文件必须能以标准 GBK 无损表示；这仍不等同游戏字体有对应字形。
    data = (SOURCE / "src/Modules/QOL/RandomNameData.h").read_text(encoding="utf-8")
    pools = {}
    for key, body in re.findall(r"random_(\w+)\[\] = \{(.*?)\};", data, re.S):
        entries = re.findall(r'"([^"]+)"', body)
        assert entries and len(entries) == len(set(entries)), key
        for entry in entries:
            assert 1 <= len(entry) <= 2 and all("\u4e00" <= c <= "\u9fff" for c in entry)
            assert entry.encode("gbk").decode("gbk") == entry
        pools[key] = entries
    assert set(pools) == {"surnames", "compound", "neutral", "male", "female"}
    assert not set(pools["neutral"]) & (set(pools["male"]) | set(pools["female"]))
    assert not set(pools["male"]) & set(pools["female"])
    print("词库项数：", {key: len(value) for key, value in pools.items()}, flush=True)
    cc, environment = compiler()
    output = SOURCE.parent / ".build/qol/random_name_test.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    # 明确源编码与执行编码，避免中文常量受编译机器区域设置影响。
    subprocess.run([str(cc), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
                    str(Path(__file__).with_suffix(".c")), "-o", str(output)],
                   check=True, env=environment)
    subprocess.run([str(output)], check=True, env=environment)


if __name__ == "__main__":
    main()
