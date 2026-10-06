"""真实TOML文件同步回归：保护已有设置、注释、绑定及损坏配置。"""
from pathlib import Path
import tempfile
import unittest
import tomllib
from sync_config import merged_text, plan, apply


class SyncConfigTests(unittest.TestCase):
    def test_existing_values_and_new_sections(self):
        old = b'# custom\r\n[controller]\r\nenabled = false # keep\r\n[controller.bindings.DaoJian.character_4.slot_1]\r\nmode="skill"\r\nselector=321\r\nhand="right"\r\n'
        template = b'[controller]\nenabled=true\n# new flag\nrumble=true\n[controller.interaction]\n# range\nmax_distance=160\n'
        merged, keys = merged_text(old, template)
        doc = tomllib.loads(merged.decode())
        self.assertFalse(doc['controller']['enabled'])
        self.assertEqual(doc['controller']['interaction']['max_distance'], 160)
        self.assertEqual(doc['controller']['bindings']['DaoJian']['character_4']['slot_1']['selector'], 321)
        self.assertIn(b'false # keep', merged)
        self.assertIn(b'# new flag', merged)
        self.assertEqual(keys, ['controller.rumble', 'controller.interaction.max_distance'])
        self.assertEqual(merged_text(merged, template), (merged, []))

    def test_actual_template_missing_distance(self):
        template = (Path(__file__).resolve().parents[1] / 'config/EDSlash.toml').read_bytes()
        # 移除整段后，其余真实模板保持原内容，模拟旧发布件少一个新配置段。
        old = template.decode()
        start = old.index('[controller.interaction]')
        end = old.index('\n[', start+1)
        old = (old[:start] + old[end:]).replace('free_run = true', 'free_run = false').encode()
        merged, keys = merged_text(old, template)
        self.assertEqual(keys, ['controller.interaction.max_distance'])
        self.assertFalse(tomllib.loads(merged.decode())['gameplay']['combat']['free_run'])

    def test_actual_template_new_combat(self):
        template = (Path(__file__).resolve().parents[1] / 'config/EDSlash.toml').read_bytes()
        old = template.decode()
        start=old.index('[controller.combat]');end=old.index('\n[',start+1)
        old=(old[:start]+old[end:]).replace('max_distance = 160','max_distance = 80').encode()
        merged,keys=merged_text(old,template)
        self.assertEqual(keys,['controller.combat.single_trigger_ultimate','controller.combat.combo_switch_input'])
        doc=tomllib.loads(merged.decode())
        self.assertFalse(doc['controller']['combat']['single_trigger_ultimate'])
        self.assertEqual(doc['controller']['combat']['combo_switch_input'],'face')
        self.assertEqual(doc['controller']['interaction']['max_distance'],80)

    def test_files_and_concurrent_change(self):
        scratch = Path(__file__).resolve().parents[1] / '.build'
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            path = Path(directory) / 'EDSlash.toml'
            template = b'[test]\r\na=1\r\nb=2\r\n'
            apply(plan(path, template))
            self.assertEqual(path.read_bytes(), template)
            path.write_bytes(b'[test]\r\na=5\r\n')
            planned = plan(path, template)
            path.write_bytes(b'[test]\r\na=7\r\n')
            with self.assertRaises(ValueError):
                apply(planned)
            self.assertEqual(path.read_bytes(), b'[test]\r\na=7\r\n')
            self.assertEqual(apply(plan(path, template)), ['test.b'])
            self.assertEqual(tomllib.loads(path.read_text())['test'], {'a': 7, 'b': 2})

    def test_invalid_and_unsafe_tables(self):
        for old in (b'[test]\na=', b'[test]\na=1\na=2', b'test={a=1}\n'):
            with self.assertRaises((ValueError, tomllib.TOMLDecodeError)):
                merged_text(old, b'[test]\na=1\nb=2\n')


if __name__ == '__main__':
    unittest.main()
