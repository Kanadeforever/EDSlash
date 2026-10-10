"""用隔离的发行样本验证打包边界，不改用户产物，不执行远程发布。"""
from pathlib import Path
import contextlib
import hashlib
import io
import json
import tempfile
import unittest
import zipfile

from package_release import LICENSE_FILES, PACKAGE_FILES, license_contents, package, safe_file, source_digest, write_license_files


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
            "docs/第三方许可/第三方声明.txt": b"own notices",
            "docs/证据/实机日志/原始数据.bin": b"\x00\xff\x0a\x0d",
            "release/EDSlash.asi": b"formal-build",
            "release/EDSlash.toml": config,
            "release/第三方许可.txt": b"notices",
            # 这些文件实际存在，正式包仍不能包含任何一个。
            "release/debug/EDSlash_debug.asi": b"debug-build",
            "release/EDSlash.SkillContols.toml": b"private skill bindings",
            "release/EDSlash.SkillContols.toml.DaoJian.bak": b"private game backup",
            "release/EDSlash.toml.skills-migration.bak": b"private migration backup",
            "release/upx/EDSlash.asi": b"duplicate",
            "参考资料/ComeOn.exe": b"game-original",
            ".build/cache.obj": b"compiler-cache",
            "source/__pycache__/cache.pyc": b"python-cache",
        }
        # 混合LF、CRLF和尾空格，验证原件复制不做任何排版或换行转换。
        for name, relative in LICENSE_FILES.items():
            files.setdefault(relative, (name + " original license \nsecond line\r\n").encode("utf-8"))
        files["thirdparty/SDL-release-3.4.16/src/libm/e_sqrt.c"] = b"#include <math.h>\n/* SunPro full permission \n */\r\nvoid math(void);"
        for relative, data in files.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        write_license_files(self.root, self.root / "release")
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

    def test_unresolved_root_license_copy(self):
        # 临时目录可通过另一种路径写法到达；解析输入后必须与同样解析的根比较。
        alias = self.root / "source" / ".."
        self.assertTrue((alias / "LICENSE").is_file())
        directory = self.root / "alias-output"
        write_license_files(alias, directory)
        self.assertEqual((directory / "LICENSE.txt").read_bytes(), (self.root / "LICENSE").read_bytes())

    def test_relative_root_license_copy(self):
        # 调用工具时传入相对根也必须有效，不能把实际存在的许可误报成越界。
        # CI工作区和系统临时目录可能在不同盘，不用跨盘relpath；退出时自动还原工作目录。
        with contextlib.chdir(self.root.parent):
            relative = Path(self.root.name)
            self.assertEqual(license_contents(relative), license_contents(self.root))

    def test_resolved_root_still_rejects_escape(self):
        # 规范化根目录不会放宽边界：从source向上读取项目LICENSE仍属于越界。
        with self.assertRaisesRegex(RuntimeError, "路径越界"):
            safe_file(self.root / "source", "../LICENSE")

    def test_exact_file_manifest_and_original_bytes(self):
        self.pack()
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(len(archive.namelist()), len(PACKAGE_FILES))
            self.assertEqual(set(archive.namelist()), set(PACKAGE_FILES))
            self.assertNotIn("LICENSE-SDL.txt", archive.namelist())
            for name, original in license_contents(self.root).items():
                self.assertEqual(archive.read(name), original)
            self.assertEqual(archive.read("第三方声明.txt"), b"own notices")
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

    def test_missing_license_rejected(self):
        for name, body in license_contents(self.root).items():
            with self.subTest(name=name):
                target = self.root / "release" / name
                target.unlink()
                with self.assertRaisesRegex(RuntimeError, "缺少文件"):
                    self.pack()
                target.write_bytes(body)

    def test_whitespace_only_license_change_rejected(self):
        # 连换行或尾空格的差异也不能被忽略；每份许可证及提取声明分别测试。
        for name, body in license_contents(self.root).items():
            with self.subTest(name=name):
                target = self.root / "release" / name
                target.write_bytes(body + b"\n")
                with self.assertRaisesRegex(RuntimeError, "字节不一致"):
                    self.pack()
                target.write_bytes(body)

    def test_sunpro_fragment_exact(self):
        self.assertEqual(license_contents(self.root)["licenses/SunPro-NOTICE.txt"],
                         b"/* SunPro full permission \n */\r\n")

    def test_notice_change_rejected(self):
        (self.root / "release/第三方声明.txt").write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError, "第三方声明"):
            self.pack()

    def test_old_aggregate_removed(self):
        directory = self.root / "release"
        (directory / "LICENSE-SDL.txt").write_bytes(b"old aggregate")
        (directory / "第三方许可.txt").write_bytes(b"old aggregate")
        write_license_files(self.root, directory)
        self.assertFalse((directory / "LICENSE-SDL.txt").exists())
        self.assertFalse((directory / "第三方许可.txt").exists())

    def test_output_inside_source_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "输出不能"):
            package(self.root, self.root / "source/package.zip")


if __name__ == "__main__":
    unittest.main()
