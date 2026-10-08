"""只读核对名称的游戏绘制、隐藏EDIT创建和外传生日校验。"""
from pathlib import Path
import json, struct, hashlib, argparse
from verify_profiles import PE

def main():
    import capstone
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    root=Path(__file__).resolve().parents[3];output=args.output.resolve()
    if any(output.is_relative_to((root/p).resolve()) for p in ('archive','参考资料')):raise SystemExit('参考输入只读')
    data=json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))['profiles']
    lines=['名称显示与外传生日原始指令证据','静态证据不等于实机验收。']
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for profile in data:
        pe=PE(root/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f"ComeOn-{profile['tag']}-{profile['edition']}.exe")
        assert hashlib.sha256(pe.data).hexdigest()==profile['sha256']
        a=profile['addresses'];vt=a['menu_name_vtable']
        draw,show=[struct.unpack('<I',pe.read(vt+off,4))[0] for off in (8,0x1c)]
        blocks=[('名称从D4 CString调用游戏绘制',draw,0xD0),('原EDIT创建不设置WM_SETFONT',show,0xB0)]
        if profile['game_id']==2:
            # 日期没有年份；原校验表分别指向31天、30天和29天分支。
            table=struct.unpack('<12I',pe.read(0x46111C,48))
            days=[31 if v==0x4610E9 else 30 if v==0x4610F8 else 29 if v==0x461107 else 0 for v in table]
            assert days==[31,29,31,30,31,30,31,31,30,31,30,31],days
            blocks.extend([('月份日期只读合法性',0x4610C0,0x5C),('确认时读取DF/E0 CString并校验',0x4B5150,0x48),('生日默认文本同步',a['menu_newgame_show'],0x80)])
            lines.append('外传各月天数：'+str(days))
        lines.extend(['',profile['name'],profile['sha256']])
        for title,start,size in blocks:
            lines.append(f'{title} {start:08X}')
            for ins in decoder.disasm(pe.read(start,size),start):lines.append(f'{ins.address:08X} {ins.bytes.hex():24s} {ins.mnemonic} {ins.op_str}')
    output.write_bytes(('\r\n'.join(lines)+'\r\n').encode('utf-8'))
    print('四版本名称绘制与外传生日原规则核对通过')
if __name__=='__main__':main()
