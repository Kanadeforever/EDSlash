"""只读准确EXE，验证静态地址交叉检查能拒绝此前实际出现的错误。"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import unittest
from verify_profiles import PE, verify_static_sources, verify_action_sources, verify_focus_sources


class StaticSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # 不下载/修改游戏。完整本地资料可验证四样本；源码包没有游戏时明确跳过。
        root=Path(__file__).resolve().parents[3]
        profiles=json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
        base=root/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'
        paths=[base/f'ComeOn-{p["tag"]}-{p["edition"]}.exe' for p in profiles]
        if not all(p.is_file() for p in paths):
            raise unittest.SkipTest('没有完整四份游戏EXE，不能执行真实样本地址回归')
        cls.samples=[]
        for profile,path in zip(profiles,paths):
            pe=PE(path)
            # 错误样本不能充当成功证据，先要求磁盘内容与档案完整散列一致。
            if hashlib.sha256(pe.data).hexdigest()!=profile['sha256']:
                raise AssertionError(f'样本散列不符：{path.name}')
            cls.samples.append((profile,pe))

    def test_four_native_sources(self):
        for profile,pe in self.samples:
            with self.subTest(profile=profile['name']):
                verify_static_sources(pe,profile)

    def test_four_action_sources(self):
        for profile,pe in self.samples:
            with self.subTest(profile=profile['name']):
                verify_action_sources(pe,profile)

    def test_reject_action_cross_fields(self):
        # 保持原EXE和签名不变，只改一个菜单字段，原指令来源检查必须拒绝。
        fields=('menu_hud_vtable','menu_hud_primary','menu_hud_hit','menu_action_vtable',
                'menu_action_global','menu_action_open','menu_action_rebuild','menu_action_commit')
        for profile,pe in self.samples:
            for field in fields:
                with self.subTest(profile=profile['name'],field=field):
                    wrong=deepcopy(profile);wrong['addresses'][field]+=4
                    with self.assertRaises(AssertionError):
                        verify_action_sources(pe,wrong)

    def test_focus_draw_and_drop_sources(self):
        for profile,pe in self.samples:
            verify_focus_sources(pe,profile)
            for field in ('focus_rect_draw','focus_frame_get','focus_image_get','menu_item_drop'):
                with self.subTest(profile=profile['name'],field=field):
                    wrong=deepcopy(profile);wrong['addresses'][field]+=4
                    with self.assertRaises(AssertionError):verify_focus_sources(pe,wrong)

    def test_reject_old_waizhuan_map(self):
        # 原宿主回放给了正确模拟指针，所以无法发现这项真实档案错误。
        # 保持所有函数签名、EXE和其它字段不变，只恢复旧589F28，检查必须失败。
        for profile,pe in self.samples:
            if profile['game_id']!=2:
                continue
            with self.subTest(profile=profile['name']):
                wrong=deepcopy(profile)
                wrong['addresses']['inspect_map_global']=0x589F28
                with self.assertRaises(AssertionError):
                    verify_static_sources(pe,wrong)

    def test_reject_wrong_handles(self):
        # 地图正确也不能搭配另一张句柄表，否则机关记录依然解析不到对象。
        for profile,pe in self.samples:
            with self.subTest(profile=profile['name']):
                wrong=deepcopy(profile)
                wrong['addresses']['handles_global']+=4
                with self.assertRaises(AssertionError):
                    verify_static_sources(pe,wrong)

    def test_reject_wrong_gate(self):
        # 正确函数头不等于原选择器实际调用它，必须检查rel32指向的资格入口。
        for profile,pe in self.samples:
            with self.subTest(profile=profile['name']):
                wrong=deepcopy(profile)
                wrong['addresses']['inspect_static_gate']+=1
                with self.assertRaises(AssertionError):
                    verify_static_sources(pe,wrong)


if __name__=='__main__':
    unittest.main()
