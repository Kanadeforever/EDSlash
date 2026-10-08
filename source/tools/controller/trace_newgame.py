"""四版本新游戏、名称写入与切页期间焦点生命周期原始证据。"""
from pathlib import Path
import argparse
import hashlib
import json
from verify_profiles import PE,verify_newgame_sources

def main():
    import sys
    sys.stdout.reconfigure(encoding='utf-8')
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    root=Path(__file__).resolve().parents[3];output=args.output.resolve()
    for name in ('参考资料','archive'):
        if output.is_relative_to((root/name).resolve()):raise SystemExit('输入参考资料和归档只读')
    import capstone
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    profiles=json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
    lines=['新游戏两层与原名称写入四版本证据','只读静态指令，不能代替实机验收。']
    for profile in profiles:
        pe=PE(root/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f"ComeOn-{profile['tag']}-{profile['edition']}.exe")
        assert hashlib.sha256(pe.data).hexdigest()==profile['sha256'];verify_newgame_sources(pe,profile);a=profile['addresses']
        lines.extend(['',profile['name'],profile['sha256']])
        blocks=[('角色提交：Show后再次读取旧A8',a['menu_character_primary'],0x80),('角色高亮Tick',a['menu_character_tick'],0x90),
                ('名称/难度主操作',a['menu_newgame_primary'],0xA0),('原难度循环',a['menu_newgame_cycle'],0x160),
                ('原名称CString及EDIT同步',a['menu_name_set'],0x80),('原文字绘制',a['menu_native_text_draw'],0x90),
                ('原背包50格空位查询',a['menu_bag_empty'],0x40),
                ('原单槽堆叠资格与9件上限',0x47ED90 if profile['game_id']==1 else 0x48DB80,0x60),
                ('原62槽可合并数量查询',0x47EDF0 if profile['game_id']==1 else 0x48DBE0,0x40),
                ('原金钱增加与有符号上限',0x47EE70 if profile['game_id']==1 else 0x48DC60,0x50)]
        for title,start,length in blocks:
            lines.extend(['',f'{title} {start:08X}'])
            for ins in decoder.disasm(pe.read(start,length),start):lines.append(f'{ins.address:08X} {ins.bytes.hex():26s} {ins.mnemonic:8s} {ins.op_str}'.rstrip())
    output.write_bytes(('\r\n'.join(lines)+'\r\n').encode('utf-8'));print('四版本新游戏/切页A8/名称setter/容量入口核对通过')
if __name__=='__main__':main()
