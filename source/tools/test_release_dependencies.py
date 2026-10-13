"""离线验证嵌套覆盖、下载摘要与危险ZIP拒绝，不访问网络。"""
import io
import hashlib
import urllib.error
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from release_dependencies import dependency_contents, read_zip, verified, download


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
    def test_timeout_then_success(self):
        """真实下载入口遭遇WinError10060同类URLError后应再次请求，而不是终止工作流。"""
        payload=b"verified download";digest=hashlib.sha256(payload).hexdigest()
        with tempfile.TemporaryDirectory() as temporary, \
             patch("release_dependencies.DOWNLOADS",{"asset":("https://example.invalid/asset",digest)}), \
             patch("release_dependencies.dependency_contents",return_value={}), \
             patch("time.sleep"), \
             patch("release_dependencies.urllib.request.urlopen",side_effect=[urllib.error.URLError(TimeoutError("WinError 10060")),io.BytesIO(payload)]) as request:
            download(Path(temporary))
            self.assertEqual(request.call_count,2)
            self.assertEqual((Path(temporary)/"asset").read_bytes(),payload)

    def test_exhausted_timeout_leaves_no_cache(self):
        """连续超时只请求四次，失败信息包含依赖名，缓存目录没有半份文件。"""
        with tempfile.TemporaryDirectory() as temporary, \
             patch("release_dependencies.DOWNLOADS",{"asset":("https://example.invalid/asset","unused")}), \
             patch("time.sleep") as sleep, \
             patch("release_dependencies.urllib.request.urlopen",side_effect=urllib.error.URLError(TimeoutError("WinError 10060"))) as request:
            with self.assertRaisesRegex(RuntimeError,"依赖下载失败：asset"):
                download(Path(temporary))
            self.assertEqual(request.call_count,4)
            self.assertEqual([call.args[0] for call in sleep.call_args_list],[2,4,8])
            self.assertEqual(list(Path(temporary).iterdir()),[])

    def test_http_and_integrity_failures(self):
        """服务端暂时故障重试；404与摘要错误立即拒绝，不能发布错误内容。"""
        payload=b"verified";digest=hashlib.sha256(payload).hexdigest()
        for status,attempts in [(503,4),(429,4),(404,1)]:
            with self.subTest(status=status), tempfile.TemporaryDirectory() as temporary, \
                 patch("release_dependencies.DOWNLOADS",{"asset":("https://example.invalid/asset",digest)}), \
                 patch("time.sleep"), \
                 patch("release_dependencies.urllib.request.urlopen",side_effect=urllib.error.HTTPError("https://example.invalid/asset",status,"HTTP failure",{},None)) as request:
                with self.assertRaisesRegex(RuntimeError,"依赖下载失败：asset"):
                    download(Path(temporary))
                self.assertEqual(request.call_count,attempts)
                self.assertEqual(list(Path(temporary).iterdir()),[])
        with tempfile.TemporaryDirectory() as temporary, \
             patch("release_dependencies.DOWNLOADS",{"asset":("https://example.invalid/asset",digest)}), \
             patch("release_dependencies.urllib.request.urlopen",return_value=io.BytesIO(b"wrong")) as request:
            with self.assertRaisesRegex(RuntimeError,"SHA-256"):
                download(Path(temporary))
            self.assertEqual(request.call_count,1)
            self.assertEqual(list(Path(temporary).iterdir()),[])

    def test_verified_cache_skips_network(self):
        """恢复的缓存也必须重新验摘要；有效缓存不请求网络。"""
        payload=b"cached";digest=hashlib.sha256(payload).hexdigest()
        with tempfile.TemporaryDirectory() as temporary, \
             patch("release_dependencies.DOWNLOADS",{"asset":("https://example.invalid/asset",digest)}), \
             patch("release_dependencies.dependency_contents",return_value={}), \
             patch("release_dependencies.urllib.request.urlopen") as request:
            (Path(temporary)/"asset").write_bytes(payload)
            download(Path(temporary))
            request.assert_not_called()

    def test_atomic_publish_failure_cleans_temporary(self):
        """磁盘提交失败不会留下目标或临时文件，也不重试网络下载。"""
        payload=b"verified";digest=hashlib.sha256(payload).hexdigest()
        with tempfile.TemporaryDirectory() as temporary, \
             patch("release_dependencies.DOWNLOADS",{"asset":("https://example.invalid/asset",digest)}), \
             patch("release_dependencies.urllib.request.urlopen",return_value=io.BytesIO(payload)) as request, \
             patch("release_dependencies.Path.replace",side_effect=PermissionError("publish denied")):
            with self.assertRaises(PermissionError):
                download(Path(temporary))
            self.assertEqual(request.call_count,1)
            self.assertEqual(list(Path(temporary).iterdir()),[])

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
        self.assertEqual(files["dsound.dll"], b"loader")
        self.assertNotIn("dinput8.dll", files)
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
