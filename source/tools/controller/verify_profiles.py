"""只读核对两作Controller基线、局部签名和CALL目标，不生成独立插件。"""
from pathlib import Path
import hashlib
import json
import struct
SOURCE=Path(__file__).resolve().parents[2]
ROOT=SOURCE.parent
class PE:
    """只读 PE32 查询，不需要额外安装 pefile。"""
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        assert self.data[:2] == b'MZ' and self.data[self.pe:self.pe+4] == b'PE\0\0'
        self.machine, count = struct.unpack_from('<HH', self.data, self.pe+4)
        self.opt = self.pe+24
        assert struct.unpack_from('<H', self.data, self.opt)[0] == 0x10b
        self.base = struct.unpack_from('<I', self.data, self.opt+28)[0]
        opt_size = struct.unpack_from('<H', self.data, self.pe+20)[0]
        self.sections = []
        for i in range(count):
            pos = self.opt+opt_size+i*40
            vs, rva, size, offset = struct.unpack_from('<IIII', self.data, pos+8)
            self.sections.append((rva, size, offset))

    def read(self, address, count):
        rva = address-self.base
        for start, size, offset in self.sections:
            if start <= rva and rva+count <= start+size:
                return self.data[offset+rva-start:offset+rva-start+count]
        raise ValueError(f'地址不在文件内：{address:#x}')

    def directory(self, index):
        return struct.unpack_from('<II', self.data, self.opt+96+index*8)

    def string(self, rva):
        result = bytearray()
        while True:
            c = self.read(self.base+rva+len(result), 1)
            if c == b'\0': return result.decode('ascii')
            result += c


def verify_static_sources(pe, profile):
    """从原指令交叉核对静态地图、句柄表及资格回调的来源。"""
    # 地址表可以语法正确但指向错误全局。原选择器两次读取同一地图，必须分别相符。
    picker=profile['addresses']['inspect_static_picker']
    for offset,opcode,field in [(0,b'\x8b\x15','inspect_map_global'),
                                (0xE4,b'\x8b\x35','inspect_map_global'),
                                (0x77,b'\x8b\x0d','handles_global')]:
        instruction=pe.read(picker+offset,6)
        assert instruction[:2]==opcode, (profile['name'],field,'原读取指令改变')
        actual=struct.unpack('<I',instruction[2:])[0]
        assert actual==profile['addresses'][field], (profile['name'],field,hex(actual),hex(profile['addresses'][field]))
    # 原完整返回链前后都会询问资格，不能只验证某个函数头存在。
    for offset in (0xB7,0x229):
        call=pe.read(picker+offset,5)
        assert call[0]==0xE8 and picker+offset+5+struct.unpack('<i',call[1:])[0]==profile['addresses']['inspect_static_gate']


def verify_baselines(data):
    base = ROOT / '参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'
    # 四份 EXE 必须同时存在才做本地证据检查。源码构建不携带游戏 EXE，也不下载游戏。
    paths = [base / f'ComeOn-{p["tag"]}-{p["edition"]}.exe' for p in data['profiles']]
    if not all(p.is_file() for p in paths):
        print('未提供完整四份基线 EXE：跳过样本复核，不能称为四样本验证通过。')
        return False
    for profile, path in zip(data['profiles'], paths):
        pe = PE(path)
        assert hashlib.sha256(pe.data).hexdigest() == profile['sha256'], path
        assert pe.machine == 0x14c
        for key, expected in profile['signatures'].items():
            assert pe.read(profile['addresses'][key], 12).hex() == expected, key
        # 从原EXE指令独立取得地图/句柄表地址，不能只验证由同一元数据生成的C表。
        # 模拟对象能够提供正确地图，无法发现档案全局地址误写，因此这里必须核对原指令。
        verify_static_sources(pe,profile)
        assert pe.read(profile['addresses']['resolver_call'], 5).hex() == profile['call_bytes']
        # 把调用点真正解码回目标，而不是只核对一串由同一数据源复制的字节。
        call = pe.read(profile['addresses']['resolver_call'], 5)
        assert call[0] == 0xe8
        assert profile['addresses']['resolver_call']+5+struct.unpack_from('<i',call,1)[0] == profile['addresses']['resolver']
        history_call=pe.read(profile['addresses']['history_call'],5)
        assert history_call.hex()==profile['history_call_bytes']
        assert history_call[0]==0xe8
        assert profile['addresses']['history_call']+5+struct.unpack_from('<i',history_call,1)[0]==profile['addresses']['history_record']
        retry_call=pe.read(profile['addresses']['retry_call'],5)
        assert retry_call.hex()==profile['retry_call_bytes'] and retry_call[0]==0xe8
        assert profile['addresses']['retry_call']+5+struct.unpack_from('<i',retry_call,1)[0]==profile['addresses']['skill_release']
        end_call=pe.read(profile['addresses']['end_call'],5)
        assert end_call.hex()==profile['end_call_bytes'] and end_call[0]==0xe8
        assert profile['addresses']['end_call']+5+struct.unpack_from('<i',end_call,1)[0]==profile['addresses']['end_record']
        # 新CALL包装也必须解码回真实目标；只验证函数头不能证明调用点选对。
        for call_key,callee_key in [('right_icon_call','icon_draw'),('dodge_init_call','dodge_init'),('dodge_motion_call','dodge_motion'),('inspect_hover_call','inspect_hover'),('menu_talk_picker_call','menu_talk_picker'),('cursor_sprite_call1','cursor_sprite_draw'),('cursor_sprite_call2','cursor_sprite_draw')]:
            call=pe.read(profile['addresses'][call_key],5)
            assert call[0]==0xE8 and profile['addresses'][call_key]+5+struct.unpack_from('<i',call,1)[0]==profile['addresses'][callee_key]
        assert struct.unpack('<I',pe.read(profile['addresses']['menu_system_vtable']+0x24,4))[0]==profile['addresses']['menu_system_primary']
        assert struct.unpack('<I',pe.read(profile['addresses']['menu_confirm_vtable']+0x3C,4))[0]==profile['addresses']['menu_confirm_submit']
        position=pe.read(profile['addresses']['cursor_position_call'],6)
        assert position[:2]==b'\xff\x15' and struct.unpack('<I',position[2:])[0]==profile['addresses']['cursor_position_iat']
        assert struct.unpack('<I',pe.read(profile['addresses']['menu_load_vtable']+0x24,4))[0]==profile['addresses']['menu_load_primary']
        assert struct.unpack('<I',pe.read(profile['addresses']['menu_message_vtable']+0x24,4))[0]==profile['addresses']['menu_message_primary']
        # 菜单只占Tick/Show/hover，Draw槽属于DisplayFix，不能以整张虚表签名拒绝合法绘制包装。
        for kind in ('title','system','confirm','load','message','talk','text','quest','skill','bag','storage','shop','craft','inlay','charm'):
            table=profile['addresses'][f'menu_{kind}_vtable']
            for operation,slot in (('tick',4),('show',0x1C),('hover',0x30)):
                expected=profile['addresses'][f'menu_{kind}_{operation}']
                assert struct.unpack('<I',pe.read(table+slot,4))[0]==expected,(kind,operation)
        for call_key, target_key in [('menu_skill_base_call','menu_message_base_tick'),
                                     ('menu_skill_combo_call1','menu_skill_combo_hit'),
                                     ('menu_skill_combo_call2','menu_skill_combo_hit')]:
            call=pe.read(profile['addresses'][call_key],5)
            assert call[0]==0xE8 and profile['addresses'][call_key]+5+struct.unpack_from('<i',call,1)[0]==profile['addresses'][target_key]
        for kind, operation, slot in [('quest','primary',0x24),('skill','primary',0x24),('skill','secondary',0x2C),
                                      ('bag','primary',0x24),('storage','primary',0x24),('bag','secondary',0x2C),('storage','secondary',0x2C)]:
            assert struct.unpack('<I',pe.read(profile['addresses'][f'menu_{kind}_vtable']+slot,4))[0]==profile['addresses'][f'menu_{kind}_{operation}']
        for kind in ('shop','craft','inlay','charm'):
            assert struct.unpack('<I',pe.read(profile['addresses'][f'menu_{kind}_vtable']+0x24,4))[0]==profile['addresses'][f'menu_{kind}_primary']
        for key in ('menu_shop_position_call1','menu_shop_position_call2','menu_inlay_position_call'):
            call=pe.read(profile['addresses'][key],6)
            assert call[:2]==b'\xff\x15' and struct.unpack('<I',call[2:])[0]==profile['addresses']['cursor_position_iat']
        assert pe.read(profile['addresses']['projection'],0x45).hex() == profile['projection_bytes']
        assert pe.read(profile['addresses']['world_to_grid'],0x35).hex() == profile['grid_bytes']
        print(f'{profile["name"]}：散列、函数签名、静态地图来源、输入调用点和坐标转换通过')
    assert all(p['projection_bytes']==data['profiles'][0]['projection_bytes'] for p in data['profiles'])
    assert all(p['grid_bytes']==data['profiles'][0]['grid_bytes'] for p in data['profiles'])
    return True



def main():
    data=json.loads(Path(__file__).with_name("profiles.json").read_text(encoding="utf-8"))
    verify_baselines(data)
if __name__=="__main__":
    main()
