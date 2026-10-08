"""只读四份官方EXE，导出动作选择菜单和快捷格的原指令；不写参考资料。"""
from pathlib import Path
import json
import hashlib
import capstone
from verify_profiles import PE, ROOT, verify_action_sources, verify_focus_sources

def main():
    # 每份准确散列独立验证，不能因为两版某段相同而借用另一份游戏地址。
    data=json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    lines=['四版本动作菜单与快捷格原始证据','范围：原EXE静态窗口及调用来源，不等于菜单/脚本实机验收。',
           '生产：HUD槽13C+i*E4，CC映射50..61；动作菜单E8节点链、C0候选指针、C8当前左右侧。',
           '节点rect14/18/1C/20，selector28；浏览调用rebuild，不调用left_set/right_set。']
    for p in data['profiles']:
        path=ROOT/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f'ComeOn-{p["tag"]}-{p["edition"]}.exe'
        pe=PE(path)
        assert hashlib.sha256(pe.data).hexdigest()==p['sha256']
        verify_action_sources(pe,p)
        verify_focus_sources(pe,p)
        lines += ['',p['name'],f'SHA256={p["sha256"]}','HUD点击→菜单全局→open→rebuild、菜单原点击→commit来源均通过独立解码。']
        for field in ('menu_hud_vtable','menu_action_vtable','menu_action_global'):
            lines.append(f'{field}={p["addresses"][field]:08X}')
        # 导出固定长度窗口，允许包含邻接函数；必须保留地址和字节，不把首个ret当所有分支终点。
        for field,size in [('menu_hud_primary',576),('menu_action_open',48),('menu_action_rebuild',896),
                           ('menu_action_commit',160),('menu_action_tick',384),('menu_hud_hit',192),('focus_rect_draw',80),('focus_frame_get',24),('focus_image_get',32),('menu_item_drop',208)]:
            address=p['addresses'][field];lines.append(f'[{field} {address:08X} 窗口{size}字节]')
            for instruction in decoder.disasm(pe.read(address,size),address):
                lines.append(f'{instruction.address:08X} {instruction.bytes.hex():24} {instruction.mnemonic:8} {instruction.op_str}'.rstrip())
        # 对应原Frame普通/矩形wrapper的坐标偏移证据，不能只依赖目标矩形。
        ordinary=0x4F4560 if p['game_id']==1 else 0x50B440
        for title,address,size in [('原普通绘制原点',ordinary,112),('原裁取绘制原点',ordinary+0x70,112)]:
            lines.append(f'[{title} {address:08X} 窗口{size}字节]')
            for instruction in decoder.disasm(pe.read(address,size),address):
                lines.append(f'{instruction.address:08X} {instruction.bytes.hex():24} {instruction.mnemonic:8} {instruction.op_str}'.rstrip())
        address=0x4C4860 if p['game_id']==1 else 0x4D8E40
        lines.append(f'[原HUD槽更新 {address:08X} 窗口256字节]')
        for instruction in decoder.disasm(pe.read(address,256),address):
            lines.append(f'{instruction.address:08X} {instruction.bytes.hex():24} {instruction.mnemonic:8} {instruction.op_str}'.rstrip())
    output=ROOT/'docs/证据/逆向分析/手柄动作菜单与快捷格原始证据.txt'
    output.write_bytes(('\n'.join(lines)+'\n').replace('\n','\r\n').encode('utf-8'))
    print('四份原始证据已写入',output)
if __name__=='__main__':
    main()
