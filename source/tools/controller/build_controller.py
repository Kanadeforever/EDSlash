"""离线构建、验证独立 Controller；所有文件都归入主项目既有目录。"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
from zipfile import ZipFile

SOURCE = Path(__file__).resolve().parents[2]
ROOT = SOURCE.parent
MODULE = SOURCE / 'src/Modules/Controller'
BUILD = SOURCE / '_build_controller'
RELEASE = ROOT / 'release'


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
        assert pe.read(profile['addresses']['projection'],0x45).hex() == profile['projection_bytes']
        assert pe.read(profile['addresses']['world_to_grid'],0x35).hex() == profile['grid_bytes']
        print(f'{profile["name"]}：散列、函数签名、输入调用点和坐标转换通过')
    assert data['profiles'][0]['projection_bytes'] == data['profiles'][1]['projection_bytes']
    assert data['profiles'][0]['grid_bytes'] == data['profiles'][1]['grid_bytes']
    return True


def compiler():
    # 优先用户显式配置，然后 PATH，再探测 MSYS2 常见安装位置；不读取个人编译器地址记录。
    candidates = [os.environ.get('CONTROLLER_CC'), shutil.which('i686-w64-mingw32-gcc'), shutil.which('gcc')]
    msys = Path(os.environ.get('MSYS2_ROOT', os.environ.get('SystemDrive','C:')+'/msys64'))
    candidates.append(str(msys/'mingw32/bin/gcc.exe'))
    for candidate in candidates:
        if not candidate or not Path(candidate).is_file(): continue
        env = os.environ.copy()
        env['PATH'] = str(Path(candidate).parent)+os.pathsep+env.get('PATH','')
        result = subprocess.run([candidate,'-dumpmachine'],capture_output=True,text=True,env=env)
        if result.returncode == 0 and result.stdout.strip().startswith(('i686-','i386-')):
            return candidate, env
    raise RuntimeError('未找到 32 位 MinGW GCC；将其加入 PATH 或设置 CONTROLLER_CC。')


def verify_asi(path):
    pe = PE(path)
    assert pe.machine == 0x14c
    assert struct.unpack_from('<H',pe.data,pe.pe+22)[0] & 0x2000
    assert struct.unpack_from('<I',pe.data,pe.opt+16)[0] != 0
    export, size = pe.directory(0)
    assert export and size
    table = pe.read(pe.base+export,40)
    names_count, names = struct.unpack_from('<I',table,24)[0], struct.unpack_from('<I',table,32)[0]
    exported = [pe.string(struct.unpack('<I',pe.read(pe.base+names+i*4,4))[0]) for i in range(names_count)]
    assert 'InitializeASI' in exported
    imports=[]
    rva, _ = pe.directory(1)
    while rva:
        descriptor=pe.read(pe.base+rva,20)
        if not any(descriptor): break
        imports.append(pe.string(struct.unpack_from('<I',descriptor,12)[0]).lower())
        rva+=20
    assert not any('libgcc' in name or 'libwinpthread' in name for name in imports), imports
    print('ASI 验证通过：PE32/i386、DLL、入口、InitializeASI、无需额外 GCC 运行库')
    return imports


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    metadata=json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))
    subprocess.run([sys.executable,str(Path(__file__).with_name('generate_profiles.py'))],check=True)
    samples_verified=verify_baselines(metadata)
    cc,env=compiler()
    BUILD.mkdir(exist_ok=True); RELEASE.mkdir(exist_ok=True)
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-static-libgcc','-finput-charset=UTF-8','-fexec-charset=UTF-8']
    test=BUILD/'test_control.exe'
    subprocess.run([cc,*flags,'-I'+str(MODULE),str(MODULE/'Control.c'),str(Path(__file__).with_name('test_control.c')),'-lm','-o',str(test)],check=True,env=env)
    subprocess.run([str(test)],check=True,env=env)
    motion_test=BUILD/'test_motion.exe'
    subprocess.run([cc,*flags,'-I'+str(MODULE),str(MODULE/'Control.c'),str(Path(__file__).with_name('test_motion.c')),'-lm','-o',str(motion_test)],check=True,env=env)
    subprocess.run([str(motion_test)],check=True,env=env)
    game_test=BUILD/'test_game.exe'
    subprocess.run([cc,*flags,'-I'+str(MODULE),str(MODULE/'Control.c'),str(MODULE/'Game.c'),str(Path(__file__).with_name('test_game.c')),'-luser32','-lm','-o',str(game_test)],check=True,env=env)
    subprocess.run([str(game_test)],check=True,env=env)
    output=BUILD/'EDSlashController.asi'
    units=['Main.c','Control.c','Profile.c','Input.c','Game.c']
    subprocess.run([cc,*flags,'-shared',*[str(MODULE/name) for name in units],'-luser32','-ladvapi32','-lm','-o',str(output)],check=True,env=env)
    imports=verify_asi(output)
    archive=SOURCE/'vendor/SDL3/SDL3-3.4.14-win32-x86.zip'
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==metadata['sdl_sha256']
    # 先验证依赖和完整产物，再更新 release；绝不递归清空多模块共享的 release。
    with ZipFile(archive) as z:
        dll=z.read('SDL3.dll')
    sdl=BUILD/'SDL3.dll'; sdl.write_bytes(dll)
    assert PE(sdl).machine==0x14c
    load_test=BUILD/'test_load.exe'
    subprocess.run([cc,*flags,'-municode',str(Path(__file__).with_name('test_load.c')),'-o',str(load_test)],check=True,env=env)
    subprocess.run([str(load_test),str(output),str(sdl)],check=True,env=env)
    for src,dest in [(output,RELEASE/output.name),(sdl,RELEASE/sdl.name)]: shutil.copyfile(src,dest)
    config=RELEASE/'EDSlashController.ini'
    if not config.exists(): shutil.copyfile(SOURCE/'template/EDSlashController.ini',config)
    # 许可证放中文名文档中，发布包必须连同 docs 一起携带。
    report={'版本':'v0.1-dev3','双样本静态复核':samples_verified,'实机验收':{'本体':'待测试','外传':'待测试'},
            'ASI_SHA256':hashlib.sha256(output.read_bytes()).hexdigest(),'导入库':imports,
            'SDL_SHA256':hashlib.sha256(dll).hexdigest()}
    (RELEASE/'手柄构建验证.json').write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').replace('\n','\r\n').encode('utf-8'))
    print('独立手柄 v0.1-dev3 构建完成；两作连续转向与走跑手感仍待实机确认。')


if __name__=='__main__':
    main()
