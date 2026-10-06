"""只读导出静态选择器完整返回链与NPC对话入口；不操作游戏、不改参考资料。"""
from pathlib import Path
import argparse
import hashlib
import json
from verify_profiles import PE


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[3]
    output=args.output.resolve()
    if output.is_relative_to((root/'参考资料').resolve()):
        raise SystemExit('证据输出不能写入只读参考资料')
    import capstone
    engine=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    profiles=json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
    lines=['静态资格与NPC对话原始证据','局部指令不等于真实游戏验收；参考资料只读。','']
    for profile in profiles:
        picker=[0x44E860,0x45A160][profile["game_id"]-1]
        path=root/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f'ComeOn-{profile["tag"]}-{profile["edition"]}.exe'
        pe=PE(path)
        if hashlib.sha256(pe.data).hexdigest()!=profile['sha256']:
            raise SystemExit('基线散列不匹配')
        lines.extend([profile['name'],profile['sha256']])
        regions=[('静态选择器完整返回链',picker,0x237)]
        # 原press写2C4是鼠标按下/释放配对；完整文字Tick另证明Space加速而非强制结束。
        press=int.from_bytes(pe.read(profile['addresses']['menu_talk_vtable']+0x20,4),'little')
        regions.append(('原鼠标按下配对标记',press,0x50))
        regions.append(('原文字滚动与Space加速',profile['addresses']['menu_text_tick'],0x1F0))
        for key,length in [('inspect_static_gate',0x27),('menu_talk_select',0x23),('menu_talk_tick',0x134),('menu_talk_cancel',0x20),('menu_text_next',0x40)]:
            regions.append((key,profile['addresses'][key],length))
        for title,address,length in regions:
            lines.extend(['',title])
            for instruction in engine.disasm(pe.read(address,length),address):
                lines.append(f'{instruction.address:08X} {instruction.bytes.hex(" ")} {instruction.mnemonic} {instruction.op_str}')
        lines.append('')
    output.write_bytes(('\r\n'.join(lines)+'\r\n').encode('utf-8'))


if __name__=='__main__':
    main()
