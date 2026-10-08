"""只读导出四版本原版设置的控件、调值、即时应用与返回保存证据。"""
from pathlib import Path
import argparse
import hashlib
import json
from verify_profiles import PE, verify_native_settings


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[3]
    output=args.output.resolve()
    for name in ('参考资料','archive'):
        if output.is_relative_to((root/name).resolve()):
            raise SystemExit('参考资料和归档只读，请输出到自有目录')
    import capstone
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    profiles=json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
    lines=['原版设置手柄接入四版本原指令证据','静态来源和离线回放不替代实机验收。']
    for profile in profiles:
        path=root/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f"ComeOn-{profile['tag']}-{profile['edition']}.exe"
        pe=PE(path)
        assert hashlib.sha256(pe.data).hexdigest()==profile['sha256']
        verify_native_settings(pe,profile)
        lines.extend(['',profile['name'],profile['sha256']])
        blocks=[('menu_settings_primary',0xDA),('menu_settings_tick',0x30),('menu_settings_close',0x50),
                ('menu_settings_show',0xB1),('menu_settings_apply',0x78),('menu_settings_slider_set',0xFD)]
        for field,length in blocks:
            start=profile['addresses'][field];lines.extend(['',f'{field} {start:08X}'])
            for ins in decoder.disasm(pe.read(start,length),start):
                lines.append(f'{ins.address:08X} {ins.bytes.hex():26s} {ins.mnemonic:8s} {ins.op_str}'.rstrip())
    output.write_bytes(('\r\n'.join(lines)+'\r\n').encode('utf-8'))
    print('四版本原设置虚表、三滑块与返回链核对通过：',output)


if __name__=='__main__':
    main()
