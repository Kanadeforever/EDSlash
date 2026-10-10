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
    # 独立地图接口必须与原Tab及其锁存分支读取的对象一致，不能只核函数首字节。
    map_evidence=profile.get('minimap_evidence')
    if map_evidence:
        branch=map_evidence['tab_branch'];keyboard=profile['addresses']['keyboard_buffer']
        assert pe.read(branch,6)==b'\x84\x1d'+struct.pack('<I',keyboard+9)
        assert pe.read(branch+8,5)==b'\xa0'+struct.pack('<I',keyboard+0x100+9)
        assert pe.read(branch+17,6)==b'\x8b\x0d'+struct.pack('<I',profile['addresses']['menu_map_global'])
        assert struct.unpack('<I',pe.read(profile['addresses']['menu_map_vtable']+0x1C,4))[0]==profile['addresses']['menu_map_show']
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


def verify_action_sources(pe, profile):
    """从四份原程序的HUD点击链独立核对菜单、全局来源和提交入口。"""
    a=profile['addresses']
    for kind, operation, offset in [('hud','tick',4),('hud','show',0x1C),('hud','hover',0x30),
                                    ('hud','primary',0x24),('action','tick',4),('action','hover',0x30)]:
        actual=struct.unpack('<I',pe.read(a[f'menu_{kind}_vtable']+offset,4))[0]
        assert actual==a[f'menu_{kind}_{operation}'], (profile['name'],kind,operation,hex(actual))
    primary=a['menu_hud_primary']
    load=pe.read(primary+0xE6,6)
    assert load[:2]==b'\x8b\x0d' and struct.unpack('<I',load[2:])[0]==a['menu_action_global']
    # 原HUD主操作交换后继续消费A8；不能把内嵌快捷格当成主按钮上下文。
    assert pe.read(primary+0xD0,8)==b'\x8b\xbf\xa8\x00\x00\x00\x85\xff'
    assert pe.read(primary+0x20F,5)==b'\x8b\x4f\x50\x6a\x02'
    action_primary=struct.unpack('<I',pe.read(a['menu_action_vtable']+0x24,4))[0]
    for source,offset,target in [(primary,0xEE,'menu_action_open'),(primary,0x3E,'menu_hud_hit'),
                                 (a['menu_action_open'],8,'menu_action_rebuild'),(action_primary,0x25,'menu_action_commit')]:
        call=pe.read(source+offset,5)
        assert call[0]==0xE8 and source+offset+5+struct.unpack('<i',call[1:])[0]==a[target], (profile['name'],target)


def verify_focus_sources(pe,profile):
    """不依赖字段自身签名，核对原矩形裁取调用和世界持有物丢弃调用的真实来源。"""
    a=profile['addresses'];main=profile['game_id']==1
    for call_site,target in [(0x4A9647 if main else 0x4BC1A1,'focus_rect_draw'),
                             ((0x473F10 if main else 0x482790)+0x70,'menu_item_drop')]:
        call=pe.read(call_site,5)
        assert call[0]==0xE8 and call_site+5+struct.unpack('<i',call[1:])[0]==a[target], (profile['name'],target)
    # 矩形裁取有10个栈参数，丢弃有1个，不能把普通六参数精灵绘制混入。
    assert pe.read(a['focus_rect_draw']+0x3E,3)==b'\xc2\x28\x00'
    call=pe.read(a['focus_rect_draw']+0x32,5)
    assert call[0]==0xE8 and a['focus_rect_draw']+0x37+struct.unpack('<i',call[1:])[0]==a['focus_frame_get']
    call=pe.read(a['focus_rect_draw']+0x39,5)
    frame_rect=a['focus_rect_draw']+0x3E+struct.unpack('<i',call[1:])[0]
    call=pe.read(frame_rect+4,5)
    assert call[0]==0xE8 and frame_rect+9+struct.unpack('<i',call[1:])[0]==a['focus_image_get']
    # 矩形绘制会将帧E/10减去12/14原点，Runtime最终可见矩形必须补偿。
    for offset,expected in [(0x23,b'\x0f\xbf\x7e\x10'),(0x32,b'\x0f\xbf\x4e\x14'),
                            (0x3E,b'\x0f\xbf\x4e\x12'),(0x42,b'\x0f\xbf\x76\x0e')]:
        assert pe.read(frame_rect+offset,4)==expected,(profile['name'],'帧原点来源')
    call=pe.read(a['cursor_sprite_draw']+0x25,5)
    ordinary=a['cursor_sprite_draw']+0x2A+struct.unpack('<i',call[1:])[0]
    assert call[0]==0xE8
    for offset,expected in [(0x31,b'\x0f\xbf\x4e\x14'),(0x38,b'\x0f\xbf\x7e\x10'),
                            (0x45,b'\x0f\xbf\x4e\x12'),(0x49,b'\x0f\xbf\x76\x0e')]:
        assert pe.read(ordinary+offset,4)==expected,(profile['name'],'普通绘制原点来源')


    assert pe.read(a['menu_item_drop']+0xC5,3)==b'\xc2\x04\x00'


def verify_native_settings(pe, profile):
    """从原设置构造/虚表/子控件调用证明四版本地址，拒绝跨作混用。"""
    a=profile['addresses']
    constructor=0x4A55B0 if profile['game_id']==1 else 0x4B7D10
    assert pe.read(constructor+8,2)==b'\xC7\x06'
    assert struct.unpack('<I',pe.read(constructor+10,4))[0]==a['menu_settings_vtable']
    for slot,key in [(4,'tick'),(0x1C,'show'),(0x24,'primary'),(0x30,'hover'),(0x20,'apply')]:
        assert struct.unpack('<I',pe.read(a['menu_settings_vtable']+slot,4))[0]==a['menu_settings_'+key]
    def called(address):
        b=pe.read(address,5)
        assert b[0]==0xE8
        return address+5+struct.unpack('<i',b[1:])[0]
    # Show读取原运行值后，三个位置均调用同一个滑块setter。
    for offset in (0x41,0x5F,0x7C):
        assert called(a['menu_settings_show']+offset)==a['menu_settings_slider_set']
    # 返回按钮和Esc Tick必须抵达同一个原关闭函数。
    assert called(a['menu_settings_primary']+0x33)==a['menu_settings_close']
    assert called(a['menu_settings_tick']+0x1C)==a['menu_settings_close']
    slider=a['menu_settings_slider_set']
    assert pe.read(slider+6,6)==b'\x8B\x86\xD8\0\0\0'
    assert pe.read(slider+0x21,6)==b'\x89\x8E\xDC\0\0\0'


def verify_newgame_sources(pe,profile):
    """从原提交与子控件实现核对角色/名称两层，不以相同函数头替代ABI事实。"""
    a=profile['addresses'];main=profile['game_id']==1
    for name,constructor in [('character',0x499CC0 if main else 0x4AA090),('newgame',0x4A2D10 if main else 0x4B4E80)]:
        assert b'\xC7\x06'+struct.pack('<I',a[f'menu_{name}_vtable']) in pe.read(constructor,0x80)
        for key,slot in [('tick',4),('show',0x1C),('hover',0x30),('primary',0x24)]:
            assert struct.unpack('<I',pe.read(a[f'menu_{name}_vtable']+slot,4))[0]==a[f'menu_{name}_{key}']
    primary=a['menu_character_primary']
    assert pe.read(primary+3,6)==b'\x8B\x86\xA8\0\0\0'
    assert pe.read(primary+0x3D,3)==b'\xFF\x50\x1C'
    assert pe.read(primary+0x40,6)==b'\x8B\x8E\xA8\0\0\0'
    def called(address):
        b=pe.read(address,5);assert b[0]==0xE8
        return address+5+struct.unpack('<i',b[1:])[0]
    p=a['menu_newgame_primary'];extra=0 if main else 4
    assert called(p+0x19+extra)==a['menu_newgame_cycle']
    assert called(p+0x2D+extra)==a['menu_newgame_cycle']
    setter=a['menu_name_set']
    # 两作setter都同步CString D4与真实编辑框F4，但外传额外恢复光标选择。
    code=pe.read(setter,0x70)
    assert b'\xD4\0\0\0' in code and b'\xF4\0\0\0' in code
    assert pe.read(a['menu_name_vtable']+0x48,4)==struct.pack('<I',0x4BBFD0 if main else 0x4CFD80)


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
        verify_action_sources(pe,profile)
        verify_focus_sources(pe,profile)
        verify_native_settings(pe,profile)
        verify_newgame_sources(pe,profile)
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
        for call_key,callee_key in [('icon_focus_call','cursor_sprite_draw'),('right_icon_call','icon_draw'),('dodge_init_call','dodge_init'),('dodge_motion_call','dodge_motion'),('inspect_hover_call','inspect_hover'),('menu_talk_picker_call','menu_talk_picker'),('cursor_sprite_call1','cursor_sprite_draw'),('cursor_sprite_call2','cursor_sprite_draw')]:
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
        # 原屏幕投影生产点的ECX对象与基础JM83的资源21,1取值链，两作分别从原EXE验证。
        projection_call=0x474533 if profile['game_id']==1 else 0x482DB3
        assert pe.read(projection_call-5,5)==b'\xB9'+struct.pack('<I',profile['addresses']['projection_global'])
        primary=profile['addresses']['menu_skill_primary']
        assert pe.read(primary+9,5)==b'\xB9'+struct.pack('<I',profile['addresses']['inventory_root'])
        player_call=pe.read(primary+14,5)
        assert player_call[0]==0xE8 and primary+19+struct.unpack('<i',player_call[1:])[0]==profile['addresses']['inventory_get']
        basic=profile['addresses']['menu_skill_primary']+0x376
        assert pe.read(basic,10)==bytes.fromhex('8b8d080300006a016a21')
        basic_call=pe.read(basic+10,5)
        assert basic_call[0]==0xE8 and basic+15+struct.unpack('<i',basic_call[1:])[0]==profile['addresses']['template_value']
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
