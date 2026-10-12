"""离线验证嵌套覆盖、下载摘要与危险ZIP拒绝，不访问网络。"""
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from release_dependencies import dependency_contents, read_zip, verified


def archive(files):
    """用内存样本模拟官方包的目录关系，测试不写实际发行件。"""
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as output:
        for name, body in files.items():
            item = zipfile.ZipInfo(name)
            # Windows的ZipInfo构造器会规范化反斜杠；显式保留恶意输入供拒绝回归使用。
            item.filename = name
            output.writestr(item, body)
    return stream.getvalue()


class DependencyTests(unittest.TestCase):
    def test_nested_shaders_override_at_parent(self):
        nested = archive({"Shaders/crt/test.glsl": b"new", "Shaders/extra.glsl": b"extra"})
        downloads = {"cnc.zip": archive({"ddraw.dll": b"ddraw", "ddraw.ini": b"ini",
                     "cnc-ddraw config.exe": b"config", "Shaders/crt/test.glsl": b"old",
                     "Shaders/readme.txt": b"authors\r\n", "Shaders/shader-package.zip": nested}),
                     "asi.zip": archive({"dinput8.dll": b"loader"})}
        with patch("release_dependencies.verified", side_effect=lambda directory, name: downloads.get(name, b"license\n")):
            files = dependency_contents(Path("unused"))
        self.assertEqual(files["Shaders/crt/test.glsl"], b"new")
        self.assertEqual(files["Shaders/extra.glsl"], b"extra")
        self.assertEqual(files["Third_Party_Licenses/Shaders-NOTICE.txt"], b"authors\r\n")
        self.assertFalse(any("Shaders/Shaders/" in name for name in files))

    def test_paths_and_duplicate_rejected(self):
        # 不写入磁盘也必须拒绝危险路径，避免未来改成解压时扩大边界。
        for name in ("../escape", "/absolute", "Shaders/../escape", "Shaders\\escape", "C:/escape", "plugins/evil"):
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "路径异常"):
                read_zip(archive({name: b"bad"}), shaders_only=True)
        with self.assertRaisesRegex(RuntimeError, "路径异常"):
            read_zip(archive({"Shaders/A": b"one", "Shaders/a": b"two"}))

    def test_tampered_cache_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "cnc.zip").write_bytes(b"tampered")
            with self.assertRaisesRegex(RuntimeError, "SHA-256"):
                verified(directory, "cnc.zip")


if __name__ == "__main__":
    unittest.main()
