"""只读导出技能和任务日志的原生接口，证据不代表实机验收。"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from verify_profiles import PE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--learning', action='store_true', help='仅导出技能学习、自动补入第四记录和必杀说明链')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    # 输入样本始终只读，输出也不能落入参考资料目录。
    if output.is_relative_to((root / '参考资料').resolve()):
        raise SystemExit('禁止向参考资料写证据')
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    profiles = json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
    probes = [
        [(0x52A03C, '日志'), (0x52A118, '技能')],
        [(0x553354, '日志'), (0x553438, '技能')],
    ]
    helpers = [
        [('日志切分类', 0x4A7380, 0x660), ('日志选项编排', 0x4A79E0, 0xE0),
         ('技能切页', 0x4ABD50, 0x300), ('连招节点鼠标命中', 0x4AA790, 0xC0)],
        [('日志切分类', 0x4B9CB0, 0x670), ('日志选项编排', 0x4BA320, 0xE0),
         ('技能切页', 0x4BE9A0, 0x300), ('连招节点鼠标命中', 0x4BD410, 0xC0)],
    ]
    lines = ['必杀自动学习原生证据' if args.learning else '技能与日志原生界面证据',
             '双版本基线只读；地址和接口需结合完整控制流核对。', '']
    for profile, tables, regions in zip(profiles, probes, helpers):
        path = root / '参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序' / f'ComeOn-{profile["tag"]}-NonSteam.exe'
        pe = PE(path)
        if hashlib.sha256(pe.data).hexdigest() != profile['sha256']:
            raise SystemExit('基线散列不符')
        lines.extend([profile['name'], profile['sha256']])
        if args.learning:
            # 主操作内学习CALL与具体EXE分别核对，不能把同组第四记录误说成另一个加点按钮。
            call_address=0x4AAB7A if profile['tag']=='DaoJian' else 0x4BD7FA
            call=pe.read(call_address,5)
            if call[0]!=0xE8:
                raise SystemExit('学习调用不是预期CALL')
            learn_address=call_address+5+struct.unpack('<i',call[1:])[0]
            expected=0x480DA0 if profile['tag']=='DaoJian' else 0x48FBD0
            if learn_address!=expected:
                raise SystemExit('学习调用目标不符')
            getter=0x47E1F0 if profile['tag']=='DaoJian' else 0x48CF60
            regions=[('原技能主操作：只匹配84至8F学习节点',profile['addresses']['menu_skill_primary'],0x3F0),
                     ('原学习与同组第四记录自动补入',learn_address,0x260),
                     ('角色已学记录决定施放资格',getter,0x80),
                     ('必杀说明悬停分支',profile['addresses']['menu_skill_tick'],0x1D0)]
            tables=[]
        # 虚表直接从对应EXE读取，防止把另一作的函数或错误栈参数套进来。
        for table, title in tables:
            tick_address=struct.unpack('<I', pe.read(table+4,4))[0]
            draw_address=struct.unpack('<I', pe.read(table+8,4))[0]
            skill_draw_length=profile['addresses']['menu_skill_switch']-draw_address
            for slot, operation, length in [(4, '更新', draw_address-tick_address if title == '技能' else 0x160),
                                             (0x1C, '显示', 0x160), (0x24, '主操作', 0x400 if title == '技能' else 0x90),
                                             (0x2C, '辅助操作', 0xE0 if title == '技能' else 0x10),
                                             (0x30, '悬停', 0x90), (8, '绘制', skill_draw_length if title == '技能' else 0x3A0)]:
                address = struct.unpack('<I', pe.read(table + slot, 4))[0]
                regions = regions + [(title + operation, address, length)]
            lines.append(f'{title}虚表={table:08X}')
        # 套组选择只是原root+C4写入，不等于在战斗里切换Y当前选择。
        if not args.learning:
            regions += [('连招套组选择',profile['addresses']['menu_skill_slot'],0xC),
                        ('日志选择位置',profile['addresses']['menu_quest_position'],0x58)]
        for title, address, length in regions:
            lines.extend(['', f'{title} {address:08X}'])
            for instruction in decoder.disasm(pe.read(address, length), address):
                # 保留机器字节，证据导出不依赖反编译器的类型推测。
                lines.append(f'{instruction.address:08X} {instruction.bytes.hex()} {instruction.mnemonic} {instruction.op_str}'.rstrip())
        lines.append('')
    output.write_bytes(('\r\n'.join(lines).rstrip() + '\r\n').encode('utf-8'))


if __name__ == '__main__':
    main()
