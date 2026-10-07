"""从四准确档案生成Runtime焦点绘制表；同一份元数据，不复制手写地址。"""
from pathlib import Path
import json
import sys
ROOT=Path(__file__).resolve().parents[1]

def render(data):
    keys={'hud_global':'skill_global','hud_vtable':'menu_hud_vtable','sprite_draw':'cursor_sprite_draw',
          'rect_draw':'focus_rect_draw','frame_get':'focus_frame_get','image_get':'focus_image_get'}
    profiles=data['profiles']
    lines=['/* 由tools/generate_focus_profiles.py从四准确EXE档案生成；只含共享绘制原接口。 */',
           'static const FocusBackend focus_profiles[2] = {']
    for i in range(2):
        p=profiles[i];other=profiles[i+2]
        # 折叠两发行版以前，先逐字段和签名证明当前接口完全相同，不猜测通用偏移。
        for source in keys.values():
            assert p['addresses'][source]==other['addresses'][source],source
            if source in p['signatures']:assert p['signatures'][source]==other['signatures'][source]
        lines.append('    {')
        for field,source in keys.items():lines.append(f'        .{field}=0x{p["addresses"][source]:08X}u,')
        lines.append('        .signatures={')
        for source in ('cursor_sprite_draw','focus_rect_draw','focus_frame_get','focus_image_get'):
            lines.append('            {'+', '.join(f'0x{x:02X}' for x in bytes.fromhex(p['signatures'][source]))+'},')
        lines.extend(['        }','    },'])
    lines.extend(['};',''])
    return '\r\n'.join(lines).encode('utf-8')
def main():
    sys.stdout.reconfigure(encoding="utf-8")
    data=json.loads((ROOT/'tools/controller/profiles.json').read_text(encoding='utf-8'))
    target=ROOT/'src/Runtime/FocusData.h';expected=render(data)
    if '--write' in sys.argv:target.write_bytes(expected)
    elif target.read_bytes()!=expected:raise SystemExit('Runtime焦点表未同步，核对后运行--write')
    print('四样本Runtime焦点原接口生成/同步通过')
if __name__=='__main__':main()
