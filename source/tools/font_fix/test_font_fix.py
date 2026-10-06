"""四份真实EXE只读输入，派生件仅在.workspace临时目录生成并核对。"""
from pathlib import Path
import hashlib
import importlib.util
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / "参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序"


def load_tool(game):
    # 两份工具保持独立可带走；测试按文件加载，避免同名模块互相覆盖。
    path = Path(__file__).parent / game / "apply_dpi_font_fix.py"
    spec = importlib.util.spec_from_file_location(game, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FontFixTests(unittest.TestCase):
    def test_four_originals_and_boundaries(self):
        paths = list(BASE.glob("ComeOn-*.exe"))
        if len(paths) != 4:
            self.skipTest("未提供完整四官方EXE，不能宣称四样本字体验证通过")
        workspace = ROOT / ".workspace"
        workspace.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=workspace) as temporary:
            for index, path in enumerate(sorted(paths)):
                with self.subTest(sample=path.name):
                    tool = load_tool("WaiZhuan" if "waizhuan" in path.name.lower() else "DaoJian")
                    original = path.read_bytes()
                    sha = hashlib.sha256(original).hexdigest()
                    output_dir = Path(temporary) / str(index)
                    status, output, offset, before, after = tool.patch_exe(path, output_dir)
                    self.assertEqual(status, "patched")
                    self.assertEqual(before, sha)
                    self.assertNotEqual(before, after)
                    changed = output.read_bytes()
                    self.assertEqual(len(changed), len(original))
                    allowed = range(offset + tool.PATCH_RELATIVE_OFFSET,
                                    offset + tool.PATCH_RELATIVE_OFFSET + 6)
                    self.assertTrue(all(a == b or i in allowed for i, (a, b) in enumerate(zip(original, changed))))
                    self.assertEqual(tool.locate_font_dpi_code(changed), ("patched", offset))
                    # 再次运行不能覆盖已有派生件，输入、输出均保持原样。
                    with self.assertRaises(FileExistsError):
                        tool.patch_exe(path, output_dir)
                    self.assertEqual(output.read_bytes(), changed)
                    self.assertEqual(path.read_bytes(), original)
                    # 真实参考资料输出目录被拒绝；不创建目录或文件。
                    forbidden = ROOT / "参考资料/禁止字体测试输出"
                    with self.assertRaises(RuntimeError):
                        tool.patch_exe(path, forbidden)
                    self.assertFalse(forbidden.exists())
                    self.assertEqual(tool.patch_exe(output, output_dir)[0], "already_patched")


if __name__ == "__main__":
    unittest.main()
