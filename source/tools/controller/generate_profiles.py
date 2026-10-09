"""把冻结的四样本元数据转换为 C 表；默认检查同步，--write 才写文件。"""
from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[2]


def render(data):
    # 地址用字段名指定，避免位置式初始化在增添字段时悄悄错位。
    lines = ['/* 四样本基线表由 tools/controller/generate_profiles.py 生成；来源见 profiles.json。 */',
             'static const Profile profiles[] = {']
    profiles = data['profiles']
    if [(p['tag'],p['edition']) for p in profiles] != [('DaoJian','NonSteam'),('WaiZhuan','NonSteam'),('DaoJian','Steam'),('WaiZhuan','Steam')]:
        raise ValueError('必须按顺序提供本体/外传的非Steam及Steam四个档案')
    for p in profiles:
        lines.extend(['    {', f'        .name = ControllerText_Profile_{p["tag"]}{p["edition"]}Name, .sha256 = "{p["sha256"]}",'])
        lines.append(f'        .game_id = {p["game_id"]}u,')
        lines.append(f'        .entry = 0x{p["entry"]:X}u, .size = 0x{p["size"]:X}u,')
        for key, value in (p['addresses'] | p['offsets']).items():
            lines.append(f'        .{key} = 0x{value:08X}u,')
        lines.append('    },')
    count = len(profiles[0]['signatures'])
    if any(count!=len(p['signatures']) for p in profiles):
        raise ValueError('两版签名覆盖数量不同')
    lines.extend(['};', f'static const Signature signatures[][{count}] = {{'])
    for p in profiles:
        lines.append('    {')
        for key, hex_text in p['signatures'].items():
            values = ', '.join(f'0x{v:02X}' for v in bytes.fromhex(hex_text))
            lines.append(f'        {{0x{p["addresses"][key]:08X}u, {{{values}}}}}, /* {key} */')
        lines.append('    },')
    lines.append('};')
    return ('\r\n'.join(lines) + '\r\n').encode('utf-8')


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    data = json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))
    target = ROOT / 'src/Modules/Controller/ProfileData.h'
    expected = render(data)
    if '--write' in sys.argv:
        target.write_bytes(expected)
    elif target.read_bytes() != expected:
        raise SystemExit('地址表未同步，请先检查元数据再运行 --write')
    print('四样本地址表同步检查通过')


if __name__ == '__main__':
    main()
