#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
统一兼容性验证入口。

用户只需要把一个或多个 ComeOn.exe 路径交给本脚本。脚本先只读 PE 头判断本体/外传，
然后调用对应 Profile 的成熟深度验证器。这样统一工程不需要用户手工记住应该运行哪一份旧工具。
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

from compat.compat_daojian import verify_one as verify_daojian
from compat.compat_waizhuan import verify_one as verify_waizhuan


def detect_profile(path: Path) -> str:
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError("不是有效的 PE/MZ 文件")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 0x100 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("PE 头无效")
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x10B:
        raise ValueError("不是 PE32")
    entry = struct.unpack_from("<I", data, opt + 16)[0]
    image_size = struct.unpack_from("<I", data, opt + 56)[0]
    if entry == 0x0010F0EF and image_size == 0x00173000:
        return "DaoJian"
    if entry == 0x00127BCF and image_size == 0x001A5000:
        return "WaiZhuan"
    raise ValueError(f"未知 Profile：EntryPoint=0x{entry:08X}, SizeOfImage=0x{image_size:08X}")


def main() -> int:
    if len(sys.argv) < 2:
        print("用法：python source\\BladeSwordQOL\\tools\\verify_compatibility.py <ComeOn.exe> [更多 EXE ...]")
        return 1

    failed = False
    for raw in sys.argv[1:]:
        path = Path(raw).resolve()
        print(f"\n[验证目标] {path}")
        try:
            profile = detect_profile(path)
            print(f"[识别] Profile={profile}")
            lines = verify_daojian(path) if profile == "DaoJian" else verify_waizhuan(path)
            for line in lines:
                print(f"[通过] {line}")
        except Exception as exc:
            failed = True
            print(f"[失败] {exc}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
