"""只读核对四版本系统菜单暂停链，输出可复查的原指令证据。"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from verify_profiles import PE


def target(pe, address):
    instruction = pe.read(address, 5)
    assert instruction[0] == 0xE8, f'原调用不是CALL：{address:08X}'
    return address + 5 + struct.unpack('<i', instruction[1:])[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    for readonly in ('参考资料', 'archive'):
        if output.is_relative_to((root / readonly).resolve()):
            raise SystemExit('参考资料与历史归档只读，证据请输出到自有目录')
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    profiles = json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
    lines = ['模组设置原生暂停与绘制接口证据', '仅原EXE静态证据；不替代设置界面实机验收。']
    for profile in profiles:
        path = root / '参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序' / f"ComeOn-{profile['tag']}-{profile['edition']}.exe"
        pe = PE(path)
        assert hashlib.sha256(pe.data).hexdigest() == profile['sha256']
        a = profile['addresses']
        show = a['menu_system_show']
        capture = target(pe, show + 0x1F)
        assert target(pe, show + 0x31) == capture
        pause = target(pe, capture + 0x3D)
        assert target(pe, capture + 0x69) == pause
        # 打开/关闭读同一个PlayerManager全局；暂停函数写manager+0C，不能混成Actor动作状态。
        assert pe.read(capture + 0x38, 5) == b'\xB9' + struct.pack('<I', a['inventory_root'])
        assert pe.read(capture + 0x5D, 5) == b'\xB9' + struct.pack('<I', a['inventory_root'])
        assert pe.read(pause + 7, 3) == b'\x89\x46\x0C'
        assert pe.read(a['menu_action_rebuild'] + 0xFD, 4) == b'\x66\x8B\x47\x24'
        # 直接从原技能页调用点证明使用同一姓名/已学记录/悬停说明接口，不能只认函数头。
        name_offset,query_offset,description_offset=(0x2A4,0x464,0x490) if profile['game_id']==1 else (0x2C5,0x485,0x4B1)
        assert target(pe,a['menu_skill_tick']+name_offset)==a['settings_skill_name']
        assert target(pe,a['menu_skill_tick']+query_offset)==a['settings_query_skill']
        assert target(pe,a['menu_skill_tick']+description_offset)==a['settings_skill_description']
        narrative_offset=0x2DD if profile['game_id']==1 else 0x2FE
        table_offset=0x2D9 if profile['game_id']==1 else 0x2FA
        assert target(pe,a['menu_skill_tick']+narrative_offset)==a['settings_text_get']
        assert struct.unpack('<I',pe.read(a['menu_skill_tick']+table_offset,4))[0]==a['settings_text_table']
        empty_offset=0x46B if profile['game_id']==1 else 0x48C
        assert struct.unpack('<I',pe.read(a['menu_skill_tick']+empty_offset,4))[0]==a['settings_empty_string']
        # JM2D原绘制/主操作虚表槽核对；自建窗口不改变其它菜单的虚表。
        draw = struct.unpack('<I', pe.read(a['menu_system_vtable'] + 8, 4))[0]
        assert struct.unpack('<I', pe.read(a['menu_system_vtable'] + 0x24, 4))[0] == a['menu_system_primary']
        blocks = [('系统菜单Show', show, 0x39), ('UI捕获与解除', capture, 0x71),
                  ('玩家管理器暂停', pause, 0x72), ('原系统菜单绘制', draw, 0x50),
                  ('当前场景Actor getter', a['settings_actor_get'], 0x18),
                  ('当前玩家档案getter', a['inventory_get'], 0x27),
                  ('原字符串及长度前缀', a['settings_string_get'], 0x24)]
        blocks.append(('原动作候选资格/组分类/16位selector', a['menu_action_rebuild'] + 0x79, 0x90))
        blocks.extend([('原技能页取姓名',a['menu_skill_tick']+name_offset-0x16,0x27),
                       ('原技能页已学记录及完整悬停说明',a['menu_skill_tick']+query_offset-0x20,0x55),
                       ('原学习页追加第15项描述句',a['menu_skill_tick']+narrative_offset-0x21,0x40),
                       ('原姓名读取器',a['settings_skill_name'],0xA5),
                       ('原已学说明生成器',a['settings_skill_description'],0xB0),
                       ('原游戏CString析构',a['settings_string_destroy'],0x28)])
        lines.extend(['', profile['name'], profile['sha256']])
        for title, start, size in blocks:
            lines.extend(['', f'{title} {start:08X}'])
            for instruction in decoder.disasm(pe.read(start, size), start):
                lines.append(f'{instruction.address:08X} {instruction.bytes.hex():26s} {instruction.mnemonic:8s} {instruction.op_str}'.rstrip())
    output.write_bytes(('\r\n'.join(lines) + '\r\n').encode('utf-8'))
    print('四准确EXE的系统菜单暂停/恢复链已只读核对：', output)


if __name__ == '__main__':
    main()
