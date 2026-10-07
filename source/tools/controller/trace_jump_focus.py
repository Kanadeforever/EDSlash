"""只读导出四版本基础跳跃资源、投影对象和图标/说明先后关系，禁止写参考资料。"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from verify_profiles import PE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    if output.is_relative_to((root / '参考资料').resolve()):
        raise SystemExit('参考资料只读，证据必须输出至自有目录')
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    profiles = json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
    lines = ['基础跳跃及技能框图层原始证据', '仅静态指令证据；不证明具体角色资源取值或实机跳跃效果。']
    for profile in profiles:
        pe = PE(root / '参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序' / f"ComeOn-{profile['tag']}-{profile['edition']}.exe")
        if hashlib.sha256(pe.data).hexdigest() != profile['sha256']:
            raise SystemExit('EXE散列不符')
        a = profile['addresses']
        call = pe.read(a['icon_focus_call'], 5)
        if call[0] != 0xE8 or a['icon_focus_call'] + 5 + struct.unpack('<i', call[1:])[0] != a['cursor_sprite_draw']:
            raise SystemExit('图标精灵CALL目标不符')
        # 从已确认基本块起点解码，基础资源21,1由原JM83按钮分支消费。
        blocks = [('原技能操作先读取PlayerManager玩家档案', a['menu_skill_primary'], 0x24),
                  ('当前玩家档案getter', a['inventory_get'], 0x27),
                  ('JM83资源21,1及技能组表查询', a['menu_skill_primary'] + 0x35F, 0x73),
                  ('原图标先画精灵后画说明', a['icon_draw'], 0x207),
                  ('原屏幕到世界投影', a['projection'], 0x45),
                  ('原Runtime有效距离档与缓存', a['method_range'], 0x60)]
        lines.extend(['', profile['name'], profile['sha256'], f"投影对象={a['projection_global']:08X}"])
        for title, start, length in blocks:
            lines.extend(['', f'{title} {start:08X}'])
            for instruction in decoder.disasm(pe.read(start, length), start):
                lines.append(f'{instruction.address:08X} {instruction.bytes.hex():26s} {instruction.mnemonic:8s} {instruction.op_str}'.rstrip())
    output.write_bytes(('\n'.join(lines) + '\n').replace('\n', '\r\n').encode('utf-8'))
    print('四准确样本只读证据已导出：', output)


if __name__ == '__main__':
    main()
