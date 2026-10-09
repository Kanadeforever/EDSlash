"""检查统一PE32产物、导出、重定位、静态依赖和最终TOML。"""
from pathlib import Path
import hashlib
import struct
import sys
import tomllib
from controller.verify_profiles import PE

def verify(asi, config):
    pe = PE(asi)
    if pe.machine != 0x14C or not struct.unpack_from("<H", pe.data, pe.pe + 22)[0] & 0x2000:
        raise RuntimeError("产物不是Win32/i386 DLL。")
    if not struct.unpack_from("<I", pe.data, pe.opt + 16)[0]:
        raise RuntimeError("产物缺少Windows入口。")
    export, size = pe.directory(0)
    if not export or not size:
        raise RuntimeError("产物没有导出表。")
    table = pe.read(pe.base + export, 40)
    count, names = struct.unpack_from("<I", table, 24)[0], struct.unpack_from("<I", table, 32)[0]
    exports = [pe.string(struct.unpack("<I", pe.read(pe.base + names + i * 4, 4))[0]) for i in range(count)]
    if exports != ["InitializeASI"]:
        raise RuntimeError(f"生产目标导出应仅有InitializeASI，实际为{exports}。")
    imports = []
    rva, _ = pe.directory(1)
    while rva:
        descriptor = pe.read(pe.base + rva, 20)
        if not any(descriptor):
            break
        imports.append(pe.string(struct.unpack_from("<I", descriptor, 12)[0]).lower())
        rva += 20
    if any(name.startswith(("sdl", "libgcc", "libstdc++", "libwinpthread")) for name in imports):
        raise RuntimeError(f"发现外置SDL或编译器运行库依赖：{imports}")
    if not all(pe.directory(5)):
        raise RuntimeError("缺少重定位表，不能可靠加载到实际可用地址。")
    raw = Path(config).read_bytes()
    if raw.startswith(b"\xef\xbb\xbf") or b"\n" in raw.replace(b"\r\n", b""):
        raise RuntimeError("TOML模板必须UTF-8无BOM＋CRLF。")
    parsed = tomllib.loads(raw.decode("utf-8"))
    if parsed.get("meta", {}).get("schema") != 1 or parsed["controller"]["bindings"]["default"] not in {"none", "game"}:
        raise RuntimeError("模板未声明最终schema或支持的未设置默认状态。")
    print("统一PE32、唯一导出、重定位、静态SDL依赖与TOML标准解析检查通过。")
    return {"版本": "0.2.0", "SDL源码版本": "3.4.16", "ASI_SHA256": hashlib.sha256(pe.data).hexdigest(),
            "ASI字节数": len(pe.data), "导入库": imports, "导出": exports, "重定位": True,
            "配置格式": "UTF-8无BOM＋CRLF", "外置SDL3.dll": False}


def runtime_sections(path, include_debug=False):
    """按PE虚拟地址和实际加载字节核对，不把磁盘偏移变化误认为代码变化。"""
    pe = PE(path)
    count = struct.unpack_from("<H", pe.data, pe.pe + 6)[0]
    opt_size = struct.unpack_from("<H", pe.data, pe.pe + 20)[0]
    symbol_offset, symbols = struct.unpack_from("<II", pe.data, pe.pe + 12)
    strings = symbol_offset + symbols * 18 if symbol_offset else 0
    result = {}
    for index in range(count):
        offset = pe.pe + 24 + opt_size + index * 40
        name = pe.data[offset:offset + 8].split(b"\0")[0].decode("ascii")
        # GNU长段名保存在COFF字符串表里，例如.eh_frame不能只按八字节名称比较。
        if name.startswith("/") and name[1:].isdigit():
            begin = strings + int(name[1:])
            end = pe.data.index(b"\0", begin)
            name = pe.data[begin:end].decode("ascii")
        if not include_debug and name.startswith((".debug", ".gnu_debuglink")):
            continue
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", pe.data, offset + 8)
        content = pe.data[raw_offset:raw_offset + raw_size] if raw_offset else b""
        result[name] = (rva, virtual_size, content)
    return result


def verify_debug_info(debug_asi, release_asi):
    """完整件必须有源码调试段，发行件必须没有残留调试段。"""
    debug = runtime_sections(debug_asi, include_debug=True)
    release = runtime_sections(release_asi, include_debug=True)
    if any(name not in debug or not debug[name][2] for name in (".debug_info", ".debug_line")):
        raise RuntimeError("_debug缺少源码调试信息，未发布。")
    if any(name.startswith(".debug") and content[2] for name, content in release.items()):
        raise RuntimeError("发行件残留调试段，未发布。")
    print("完整版源码调试段存在，发行件调试段已剥离。")
    return True


def verify_variants(debug_asi, release_asi):
    """剥离仅改变调试与符号资料；任何运行段或虚拟布局差异都阻止发布。"""
    if runtime_sections(debug_asi) != runtime_sections(release_asi):
        raise RuntimeError("发行件与完整版的运行段字节或虚拟布局不同，未发布。")
    print("发行件／完整版运行段及虚拟布局逐段一致。")
    return True

def import_symbols(path):
    """导入描述表排列可变，真正业务依赖IAT地址及对应DLL/函数，必须逐项一致。"""
    pe=PE(path);rva,_=pe.directory(1);result=[]
    for _ in range(128):
        block=pe.read(pe.base+rva,20)
        if not any(block):return sorted(result)
        original,_,_,name,first=struct.unpack('<5I',block)
        library=pe.string(name).lower();lookup=original or first
        for i in range(4096):
            value=struct.unpack('<I',pe.read(pe.base+lookup+i*4,4))[0]
            if not value:break
            symbol=f'#{value&0xFFFF}' if value&0x80000000 else pe.string(value+2)
            result.append((first+i*4,library,symbol))
        else:raise RuntimeError('导入项数量异常，拒绝压缩件')
        rva+=20
    raise RuntimeError('导入描述表没有结束标记，拒绝压缩件')

def verify_upx_roundtrip(original,unpacked):
    """只允许UPX重排导入元数据；其余运行段字节和全部虚拟布局严格一致。"""
    before,after=runtime_sections(original),runtime_sections(unpacked)
    if before.keys()!=after.keys():raise RuntimeError('UPX解压段集合不一致')
    for name,value in before.items():
        if value[:2]!=after[name][:2] or (name!='.idata' and value[2]!=after[name][2]):
            raise RuntimeError('UPX解压运行段变化：'+name)
    a,b=PE(original),PE(unpacked)
    if a.base!=b.base or a.machine!=b.machine or a.read(a.base+a.directory(0)[0],40)!=b.read(b.base+b.directory(0)[0],40):
        raise RuntimeError('UPX解压映像或导出表不一致')
    if struct.unpack_from('<I',a.data,a.opt+16)!=struct.unpack_from('<I',b.data,b.opt+16):
        raise RuntimeError('UPX解压入口变化')
    symbols=import_symbols(original)
    if symbols!=import_symbols(unpacked):raise RuntimeError('UPX解压导入地址或DLL/函数变化')
    print(f'UPX解压非导入段字节/布局与{len(symbols)}项导入地址及函数语义一致。')
    return {'非导入运行段':'字节及虚拟布局完全一致','导入项数':len(symbols),'导入表':'IAT地址、DLL及函数语义一致，允许UPX重排元数据'}

def test_upx_roundtrip_rejections(original,unpacked):
    """用真实解压PE的两种损坏副本确认验证器拒绝代码或导入变化，不执行副本。"""
    pe=PE(unpacked);sections=runtime_sections(unpacked);raw=Path(unpacked).read_bytes()
    def offset(rva):
        for start,size,at in pe.sections:
            if start<=rva<start+size:return at+rva-start
        raise RuntimeError('损坏回放地址不在文件内')
    descriptor=pe.read(pe.base+pe.directory(1)[0],20)
    points=[('代码字节',offset(sections['.text'][0])),('导入DLL',offset(struct.unpack_from('<I',descriptor,12)[0]))]
    temporary=Path(unpacked).with_name('EDSlash-invalid-check.asi')
    for label,at in points:
        changed=bytearray(raw);changed[at]^=1;temporary.write_bytes(changed);rejected=False
        try:verify_upx_roundtrip(original,temporary)
        except RuntimeError:rejected=True
        finally:temporary.unlink()
        if not rejected:raise RuntimeError('压缩验证没有拒绝'+label+'损坏')
    return '代码字节和导入DLL变更均被拒绝；没有执行损坏副本'

if __name__ == "__main__":
    verify(Path(sys.argv[1]), Path(sys.argv[2]))
