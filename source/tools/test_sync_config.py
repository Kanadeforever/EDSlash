"""真实TOML文件同步回归：保护已有设置、注释、绑定及损坏配置。"""
from pathlib import Path
import tempfile
import unittest
import tomllib
from sync_config import merged_text, plan, apply


class SyncConfigTests(unittest.TestCase):
    def test_log_switch_first_table_preserves_root_values(self):
        old=b'# keep\r\nroot_value=7\r\n[meta]\r\nschema=1\r\n'
        merged,keys=merged_text(old,b'[logging]\r\nenabled=true\r\n[meta]\r\nschema=1\r\n')
        result=tomllib.loads(merged.decode())
        self.assertEqual(result['root_value'],7)
        self.assertTrue(result['logging']['enabled'])
        self.assertLess(merged.index(b'[logging]'),merged.index(b'[meta]'))
        self.assertEqual(keys,['logging.enabled'])

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

    def test_run_and_grace_upgrade(self):
        template=(Path(__file__).resolve().parents[1]/'config/EDSlash.toml').read_bytes()
        old=template.replace(b'run_mode = "modern"', b'# old movement mode').replace(b'offset_grace_ms = 250', b'# old offset')
        merged,keys=merged_text(old,template)
        doc=tomllib.loads(merged.decode())
        self.assertEqual(doc['controller']['movement']['run_mode'],'modern')
        self.assertEqual(doc['controller']['combat']['offset_grace_ms'],250)
        self.assertEqual(len(keys),2)
        explicit=template.replace(b'run_mode = "modern"',b'run_mode = "toggle"').replace(b'offset_grace_ms = 250',b'offset_grace_ms = 300')
        merged,keys=merged_text(explicit,template)
        self.assertEqual(merged,explicit)
        self.assertEqual(keys,[])

    def test_actual_template_new_combat(self):
        template = (Path(__file__).resolve().parents[1] / 'config/EDSlash.toml').read_bytes()
        old = template.decode()
        start=old.index('[controller.combat]');end=old.index('\n[',start+1)
        old=(old[:start]+old[end:]).replace('max_distance = 160','max_distance = 80').encode()
        merged,keys=merged_text(old,template)
        self.assertEqual(keys,['controller.combat.single_trigger_ultimate','controller.combat.combo_switch_input','controller.combat.offset','controller.combat.offset_grace_ms','controller.combat.targeting'])
        doc=tomllib.loads(merged.decode())
        self.assertFalse(doc['controller']['combat']['single_trigger_ultimate'])
        self.assertEqual(doc['controller']['combat']['combo_switch_input'],'dpad')
        self.assertEqual(doc['controller']['interaction']['max_distance'],80)
        self.assertEqual(doc['controller']['combat']['offset'],'dodge')
        self.assertEqual(doc['controller']['combat']['targeting'],'rebuilt')

    def test_targeting_upgrade_and_explicit_original(self):
        # 缺失锁定配置时补入重编；已有明确选择及注释不得被默认值覆盖。
        template=(Path(__file__).resolve().parents[1] / 'config/EDSlash.toml').read_bytes()
        old=template.decode().replace('targeting = "rebuilt"\r\n','').encode()
        merged,keys=merged_text(old,template)
        self.assertEqual(keys,['controller.combat.targeting'])
        self.assertEqual(tomllib.loads(merged.decode())['controller']['combat']['targeting'],'rebuilt')
        explicit=template.replace(b'targeting = "rebuilt"',b'targeting = "original" # keep')
        self.assertEqual(merged_text(explicit,template),(explicit,[]))

    def test_offset_upgrade_and_explicit_original(self):
        # 旧文件只补默认档；已经选择原版的文件和注释必须保持原字节。
        template=(Path(__file__).resolve().parents[1] / 'config/EDSlash.toml').read_bytes()
        old=template.decode().replace('offset = "dodge"\r\n','').encode()
        merged,keys=merged_text(old,template)
        self.assertEqual(keys,['controller.combat.offset'])
        self.assertEqual(tomllib.loads(merged.decode())['controller']['combat']['offset'],'dodge')
        explicit=template.replace(b'offset = "dodge"',b'offset = "original" # keep')
        self.assertEqual(merged_text(explicit,template),(explicit,[]))

    def test_files_and_concurrent_change(self):
        scratch = Path(__file__).resolve().parents[2] / '.build'
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
