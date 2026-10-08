"""只读核对空技能内容字段、右键灰色参数来源和方向闪避前置门。"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from verify_profiles import PE


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=Path(__file__).resolve().parents[3];output=args.output.resolve()
    for name in ('参考资料','archive'):
        if output.is_relative_to((root/name).resolve()):raise SystemExit('参考资料和历史归档只读')
    import capstone
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    profiles=json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
    lines=['空技能候选、快捷图标与闪避资格四版本证据','仅静态原指令；不等于所有场景实机验收。']
    for profile in profiles:
        pe=PE(root/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f"ComeOn-{profile['tag']}-{profile['edition']}.exe")
        assert hashlib.sha256(pe.data).hexdigest()==profile['sha256'];a=profile['addresses']
        def call(address):
            b=pe.read(address,5);assert b[0]==0xE8
            return address+5+struct.unpack('<i',b[1:])[0]
        offset=0x452 if profile['game_id']==1 else 0x473
        assert pe.read(a['menu_skill_tick']+offset-2,2)==b'\x6A\x13'
        assert call(a['menu_skill_tick']+offset)==a['ui_property']
        assert call(a['right_icon_call']-0x3A)==a['method_usable']
        lines.extend(['',profile['name'],profile['sha256']])
        blocks=[('连招候选资源19及已学记录查询',a['menu_skill_tick']+offset-0x31,0xB0),
                ('原右槽资格与灰色模式4/透明度32',a['right_icon_call']-0x68,0x72),
                ('原闪避冷却/硬直/活动技能/目标检查',a['dodge_start'],0xF0)]
        for name,start,size in blocks:
            lines.extend(['',f'{name} {start:08X}'])
            for ins in decoder.disasm(pe.read(start,size),start):
                lines.append(f'{ins.address:08X} {ins.bytes.hex():26s} {ins.mnemonic:8s} {ins.op_str}'.rstrip())
    output.write_bytes(('\r\n'.join(lines)+'\r\n').encode('utf-8'));print('四版本原内容字段/灰色参数/闪避前置检查已核对')


if __name__=='__main__':main()
