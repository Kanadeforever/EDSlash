"""从四份准确档案生成设置窗口原接口，阻止本体与外传地址交叉使用。"""
from pathlib import Path
import json
import sys

SOURCE = Path(__file__).resolve().parents[1]
FIELDS = ('world_global', 'ui', 'skill_global', 'inventory_root', 'inventory_get',
          'get_jm', 'menu_map_global', 'menu_map_vtable', 'menu_map_show', 'menu_system_vtable', 'menu_system_show', 'menu_system_primary',
          'settings_actor_get', 'settings_string_get', 'settings_icon_global',
          'ui_property', 'skill_groups', 'methods', 'lookup', 'skill_eligibility', 'icon_resolve', 'icon_draw',
          'settings_skill_name', 'settings_skill_description', 'settings_string_destroy', 'settings_query_skill', 'settings_empty_string', 'settings_text_get', 'settings_text_table', 'focus_frame_get', 'focus_image_get', 'menu_settings_vtable', 'menu_settings_show', 'menu_settings_primary', 'menu_native_text_draw')
FUNCTIONS = ('menu_map_show', 'inventory_get', 'get_jm', 'menu_system_show', 'menu_system_primary',
             'settings_actor_get', 'settings_string_get', 'ui_property', 'lookup',
             'skill_eligibility', 'icon_resolve', 'icon_draw',
             'settings_skill_name', 'settings_skill_description', 'settings_string_destroy', 'settings_query_skill', 'settings_text_get', 'focus_frame_get', 'focus_image_get', 'menu_settings_show', 'menu_settings_primary', 'menu_native_text_draw')


def render(profiles):
    lines = ['/* 由tools/generate_settings_profiles.py生成；四准确EXE共享元数据。 */',
             'static const SettingsBackend settings_profiles[2] = {']
    for game in (1, 2):
        pair = [p for p in profiles if p['game_id'] == game]
        assert len(pair) == 2 and len({p['edition'] for p in pair}) == 2
        p, other = pair
        # 相同游戏的两种发行版逐字段证明一致，再折叠为一行；不使用统一增量偏移。
        for field in FIELDS:
            assert p['addresses'][field] == other['addresses'][field], field
        for field in FUNCTIONS:
            assert p['signatures'][field] == other['signatures'][field], field
        lines.append('    {')
        for field in FIELDS:
            lines.append(f'        .{field}=0x{p["addresses"][field]:08X}u,')
        for field in ('active_offset', 'invalid_offset'):
            assert p['offsets'][field] == other['offsets'][field], field
            lines.append(f'        .{field}=0x{p["offsets"][field]:08X}u,')
        lines.append('        .signatures={')
        for field in FUNCTIONS:
            signature = bytes.fromhex(p['signatures'][field])
            assert len(signature) == 12
            lines.append('            {' + ','.join(f'0x{x:02X}' for x in signature) + '},')
        lines.extend(['        }', '    },'])
    lines.extend(['};', ''])
    return '\r\n'.join(lines).encode('utf-8')


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    profiles = json.loads((SOURCE / 'tools/controller/profiles.json').read_text(encoding='utf-8'))['profiles']
    target = SOURCE / 'src/Runtime/SettingsData.h'
    expected = render(profiles)
    if '--write' in sys.argv:
        target.write_bytes(expected)
    elif not target.is_file() or target.read_bytes() != expected:
        raise SystemExit('设置窗口地址表未同步，核对后运行 --write')
    print('四准确样本设置窗口原接口生成/同步通过')


if __name__ == '__main__':
    main()
