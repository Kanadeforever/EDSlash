"""用隔离的发行样本验证打包边界，不改用户产物，不执行远程发布。"""
from pathlib import Path
import contextlib
import hashlib
import io
import json
import tempfile
import unittest
import zipfile

from package_release import package, source_digest


class PackageTests(unittest.TestCase):
    def setUp(self):
        # 所有伪造的产物和私人文件只放在临时目录，测试退出后自动清理。
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "output/EDSlash-dev-auto.zip"
        config = b"[meta]\r\nschema = 1\r\n"
        files = {
            "README.md": b"readme", "LICENSE": b"license",
            ".gitignore": b"ignore", ".gitattributes": b"attributes",
            ".github/workflows/build.yml": b"name: build\r\n",
            "source/build.bat": b"@echo off\r\n",
            "source/src/Main.c": b"int value = 1;\r\n",
            "source/config/EDSlash.toml": config,
            "thirdparty/SDL-release-3.4.16/CMakeLists.txt": b"cmake_minimum_required(VERSION 3.21)\r\n",
            "thirdparty/SDL-release-3.4.16/LICENSE.txt": b"sdl-license",
            "thirdparty/SDL-release-3.4.16/src/device.c": b"device-backend",
            "docs/文档/完整接档说明.md": "中文接档\r\n".encode("utf-8"),
            "docs/第三方许可/配置与SDL第三方许可.txt": b"notices",
            "docs/证据/实机日志/原始数据.bin": b"\x00\xff\x0a\x0d",
            "release/EDSlash.asi": b"formal-build",
            "release/EDSlash.toml": config,
            "release/第三方许可.txt": b"notices",
            # 这些文件实际存在，正式包仍不能包含任何一个。
            "release/debug/EDSlash_debug.asi": b"debug-build",
            "release/upx/EDSlash.asi": b"duplicate",
            "参考资料/ComeOn.exe": b"game-original",
            ".build/cache.obj": b"compiler-cache",
            "source/__pycache__/cache.pyc": b"python-cache",
        }
        for relative, data in files.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.report = {
            "ASI_SHA256": hashlib.sha256(files["release/EDSlash.asi"]).hexdigest(),
            "ASI字节数": len(files["release/EDSlash.asi"]),
            "UPX": {"参数": "--best --lzma"}, "外置SDL3.dll": False,
            "默认模板SHA256": hashlib.sha256(config).hexdigest(),
            "构建身份": {"源码与构建输入SHA256": source_digest(self.root)},
        }
        self.save_report()

    def save_report(self):
        (self.root / "release/统一构建验证.json").write_text(
            json.dumps(self.report, ensure_ascii=False), encoding="utf-8")

    def pack(self):
        # 隐去成功包路径，失败断言仍保留原始异常以便定位。
        with contextlib.redirect_stdout(io.StringIO()):
            package(self.root, self.output)

    def test_exactly_four_flat_files(self):
        self.pack()
        with zipfile.ZipFile(self.output) as archive:
            names = set(archive.namelist())
            self.assertEqual(len(archive.namelist()), 4)
            self.assertEqual(names, {"EDSlash.asi", "EDSlash.toml", "LICENSE.txt", "LICENSE-SDL.txt"})
            self.assertEqual(archive.read("LICENSE.txt"), b"license")
            self.assertEqual(archive.read("LICENSE-SDL.txt"), b"notices")
            self.assertEqual(archive.read("EDSlash.asi"), b"formal-build")
        self.assertEqual({p.name for p in self.output.parent.iterdir()}, {self.output.name})

    def test_changed_binary_preserves_old_package(self):
        self.pack()
        original = self.output.read_bytes()
        (self.root / "release/EDSlash.asi").write_bytes(b"tampered")
        with self.assertRaisesRegex(RuntimeError, "正式ASI"):
            self.pack()
        self.assertEqual(self.output.read_bytes(), original)

    def test_personal_config_rejected(self):
        (self.root / "release/EDSlash.toml").write_bytes(b"[personal]\r\nvalue=1\r\n")
        with self.assertRaisesRegex(RuntimeError, "个人配置"):
            self.pack()

    def test_changed_source_rejected(self):
        (self.root / "source/src/Main.c").write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError, "源码与构建报告"):
            self.pack()

    def test_missing_thirdparty_notices_rejected(self):
        (self.root / "release/第三方许可.txt").unlink()
        with self.assertRaisesRegex(RuntimeError, "缺少文件"):
            self.pack()

    def test_output_inside_source_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "输出不能"):
            package(self.root, self.root / "source/package.zip")


if __name__ == "__main__":
    unittest.main()
