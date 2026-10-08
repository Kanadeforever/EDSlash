"""四版本元数据生成QOL名称框适配表，不让手柄持有QOL内部地址。"""
from pathlib import Path
import json
import sys
SOURCE=Path(__file__).resolve().parents[1]
FIELDS=('ui','get_jm','menu_newgame_vtable','menu_name_vtable','menu_name_set')
FUNCTIONS=('get_jm','menu_name_set')

def main():
    sys.stdout.reconfigure(encoding="utf-8")
    data=json.loads((SOURCE/'tools/controller/profiles.json').read_text(encoding='utf-8'))['profiles']
    lines=['/* 由tools/generate_random_name_profiles.py生成，四准确样本分别核对。 */','static const NameBackend name_profiles[2]={']
    for game in (1,2):
        p,q=[x for x in data if x['game_id']==game]
        for f in FIELDS:assert p['addresses'][f]==q['addresses'][f]
        for f in FUNCTIONS:assert p['signatures'][f]==q['signatures'][f]
        lines.append('    {')
        for f in FIELDS:lines.append(f'        .{f}=0x{p["addresses"][f]:08X}u,')
        lines.append('        .signatures={')
        for f in FUNCTIONS:lines.append('            {'+','.join(f'0x{b:02X}' for b in bytes.fromhex(p['signatures'][f]))+'},')
        lines.extend(['        }','    },'])
    lines.extend(['};','']);expected='\r\n'.join(lines).encode('utf-8')
    target=SOURCE/'src/Modules/QOL/RandomNameUIData.h'
    if '--write' in sys.argv:target.write_bytes(expected)
    else:assert target.read_bytes()==expected,'随机名称适配表未同步'
    print('四版本QOL原名称框生成表同步通过')
if __name__=='__main__':main()
