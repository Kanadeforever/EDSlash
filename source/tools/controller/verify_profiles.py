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


def verify_baselines(data):
    base = ROOT / '参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'
    # 两份 EXE 必须同时存在才做本地证据检查。源码构建不携带游戏 EXE，也不下载游戏。
    paths = [base / f'ComeOn-{p["tag"]}-NonSteam.exe' for p in data['profiles']]
    if not all(p.is_file() for p in paths):
        print('未提供成对基线 EXE：跳过样本复核，不能将本次构建称为双样本验证通过。')
        return False
    for profile, path in zip(data['profiles'], paths):
        pe = PE(path)
        assert hashlib.sha256(pe.data).hexdigest() == profile['sha256'], path
        assert pe.machine == 0x14c
        for key, expected in profile['signatures'].items():
            assert pe.read(profile['addresses'][key], 12).hex() == expected, key
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
        for call_key,callee_key in [('right_icon_call','icon_draw'),('dodge_init_call','dodge_init'),('dodge_motion_call','dodge_motion')]:
            call=pe.read(profile['addresses'][call_key],5)
            assert call[0]==0xE8 and profile['addresses'][call_key]+5+struct.unpack_from('<i',call,1)[0]==profile['addresses'][callee_key]
        assert pe.read(profile['addresses']['projection'],0x45).hex() == profile['projection_bytes']
        assert pe.read(profile['addresses']['world_to_grid'],0x35).hex() == profile['grid_bytes']
        print(f'{profile["name"]}：散列、函数签名、输入调用点和坐标转换通过')
    assert data['profiles'][0]['projection_bytes'] == data['profiles'][1]['projection_bytes']
    assert data['profiles'][0]['grid_bytes'] == data['profiles'][1]['grid_bytes']
    return True



def main():
    data=json.loads(Path(__file__).with_name("profiles.json").read_text(encoding="utf-8"))
    verify_baselines(data)
if __name__=="__main__":
    main()
