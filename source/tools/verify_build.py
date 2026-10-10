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
    if any(name.startswith(("sdl", "libgcc", "libstdc++", "libwinpthread", "vcruntime", "msvcp", "ucrtbase", "api-ms-win-crt")) for name in imports):
        raise RuntimeError(f"发现外置SDL或编译器运行库依赖：{imports}")
    if not all(pe.directory(5)):
        raise RuntimeError("缺少重定位表，不能可靠加载到实际可用地址。")
    raw = Path(config).read_bytes()
    if raw.startswith(b"\xef\xbb\xbf") or b"\n" in raw.replace(b"\r\n", b""):
        raise RuntimeError("TOML模板必须UTF-8无BOM＋CRLF。")
    parsed = tomllib.loads(raw.decode("utf-8"))
    if parsed.get("meta", {}).get("schema") != 1 or "bindings" in parsed.get("controller", {}):
        raise RuntimeError("模板schema错误或仍内置个人技能绑定；技能默认由缺省独立文件表示。")
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


def verify_debug_info(debug_asi, release_asi, pdb_path):
    """核对外置PDB的GUID/age与ASI的CodeView记录，拒绝误用旧PDB。"""
    pe = PE(debug_asi)
    rva, length = pe.directory(6)
    identity = None
    for offset in range(0, length, 28):
        record = pe.read(pe.base + rva + offset, 28)
        kind, size, raw = struct.unpack_from("<I", record, 12)[0], struct.unpack_from("<I", record, 16)[0], struct.unpack_from("<I", record, 24)[0]
        if kind == 2 and pe.data[raw:raw + 4] == b"RSDS":
            data = pe.data[raw:raw + size]
            identity = (data[4:20], struct.unpack_from("<I", data, 20)[0])
            if data[24:].split(b"\0", 1)[0] != b"EDSlash.pdb":
                raise RuntimeError("ASI的PDB引用必须为EDSlash.pdb，不得写入本机绝对路径。")
    if not identity:
        raise RuntimeError("调试ASI缺少MSVC CodeView身份。")
    pdb = Path(pdb_path).read_bytes()
    if not pdb.startswith(b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"):
        raise RuntimeError("PDB格式无效。")
    block_size, _, blocks, directory_size, _, block_map = struct.unpack_from("<6I", pdb, 32)
    if block_size < 512 or block_size > 65536 or blocks * block_size > len(pdb):
        raise RuntimeError("PDB块目录无效。")
    # MSF文件把流目录和各流分散到块中；先按映射重建目录，再读取身份流。
    block_count = (directory_size + block_size - 1) // block_size
    directory_blocks = struct.unpack_from(f"<{block_count}I", pdb, block_map * block_size)
    directory = b"".join(pdb[b * block_size:(b + 1) * block_size] for b in directory_blocks)[:directory_size]
    stream_count = struct.unpack_from("<I", directory)[0]
    if stream_count < 2 or stream_count > 65536:
        raise RuntimeError("PDB流目录无效。")
    sizes = struct.unpack_from(f"<{stream_count}I", directory, 4)
    cursor = 4 + stream_count * 4
    info = None
    for index, size in enumerate(sizes):
        count = 0 if size == 0xFFFFFFFF else (size + block_size - 1) // block_size
        mapping = struct.unpack_from(f"<{count}I", directory, cursor)
        cursor += count * 4
        if index == 1:
            info = b"".join(pdb[b * block_size:(b + 1) * block_size] for b in mapping)[:size]
            break
    if not info or len(info) < 28 or (info[12:28], struct.unpack_from("<I", info, 8)[0]) != identity:
        raise RuntimeError("PDB与ASI的GUID/age不匹配，不能交付错误调试资料。")
    if Path(debug_asi).read_bytes() != Path(release_asi).read_bytes():
        raise RuntimeError("压缩前发行与调试ASI必须来自同一链接件。")
    print("MSVC外置PDB身份与ASI一致，发行／调试ASI字节完全相同。")
    return True


def verify_variants(debug_asi, release_asi):
    """剥离仅改变调试与符号资料；任何运行段或虚拟布局差异都阻止发布。"""
    if runtime_sections(debug_asi) != runtime_sections(release_asi):
        raise RuntimeError("发行件与完整版的运行段字节或虚拟布局不同，未发布。")
    print("发行件／完整版运行段及虚拟布局逐段一致。")
    return True

def test_pdb_rejections(debug_asi, release_asi, pdb_path):
    """用真实ASI/PDB的损坏副本证明身份和文件格式检查会拒绝错误调试资料。"""
    pe=PE(debug_asi);rva,length=pe.directory(6);guid_offset=None
    for offset in range(0,length,28):
        record=pe.read(pe.base+rva+offset,28)
        raw=struct.unpack_from('<I',record,24)[0]
        if struct.unpack_from('<I',record,12)[0]==2 and pe.data[raw:raw+4]==b'RSDS':
            guid_offset=raw+4;break
    if guid_offset is None:raise RuntimeError('PDB身份反例缺少CodeView记录')
    temporary_asi=Path(debug_asi).with_name('EDSlash-pdb-rejection.asi')
    temporary_pdb=Path(pdb_path).with_name('EDSlash-pdb-rejection.pdb')
    changed=bytearray(pe.data);changed[guid_offset]^=1;temporary_asi.write_bytes(changed)
    changed_pdb=bytearray(Path(pdb_path).read_bytes());changed_pdb[0]^=1;temporary_pdb.write_bytes(changed_pdb)
    try:
        for label,debug,release,symbols in [('身份不匹配',temporary_asi,temporary_asi,pdb_path),
                                           ('格式损坏',debug_asi,release_asi,temporary_pdb)]:
            try:verify_debug_info(debug,release,symbols)
            except RuntimeError:continue
            raise RuntimeError('PDB验证没有拒绝'+label)
    finally:
        temporary_asi.unlink();temporary_pdb.unlink()
    return 'CodeView GUID不匹配和PDB格式损坏均被拒绝；未执行损坏副本'

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
    """只允许UPX重排导入及移除已解析的调试元数据，其余字节/布局严格一致。"""
    before,after=runtime_sections(original),runtime_sections(unpacked)
    a,b=PE(original),PE(unpacked)
    # MSVC把CodeView、VCFeature及PGO调试记录放在.rdata，UPX可清除它们。
    # 只屏蔽PE调试目录实际指向的元数据字节，不能跳过整个只读数据段。
    ignored=[]
    for pe in (a,b):
        # MSVC的导入描述符/名称/查找表同样位于.rdata，UPX可能重排这些元数据。
        # IAT的RVA、DLL及函数/序号在后面逐项核对，普通常量不在屏蔽范围内。
        import_rva,_=pe.directory(1)
        descriptor_rva=import_rva
        while descriptor_rva:
            record=pe.read(pe.base+descriptor_rva,20)
            ignored.append((descriptor_rva,descriptor_rva+20))
            if not any(record):break
            lookup,_,_,dll_name,iat=struct.unpack('<5I',record)
            ignored.append((dll_name,dll_name+len(pe.string(dll_name))+1))
            lookup=lookup or iat;index=0
            while True:
                entry=struct.unpack('<I',pe.read(pe.base+lookup+index*4,4))[0]
                ignored.append((lookup+index*4,lookup+index*4+4))
                ignored.append((iat+index*4,iat+index*4+4))
                if not entry:break
                if not entry&0x80000000:
                    ignored.append((entry,entry+2+len(pe.string(entry+2))+1))
                index+=1
            descriptor_rva+=20
        debug_rva,debug_size=pe.directory(6)
        if debug_rva and debug_size:
            ignored.append((debug_rva,debug_rva+debug_size))
            for offset in range(0,debug_size,28):
                record=pe.read(pe.base+debug_rva+offset,28)
                # UPX可保留目录位置/长度并把槽位全部清零；空记录没有数据区可忽略。
                # 不能仅按Type=0放行，因为非空未知记录可能指向普通业务数据。
                if not any(record):continue
                kind,size,data_rva=struct.unpack_from('<III',record,12)
                if kind not in (2,12,13):
                    raise RuntimeError('UPX调试元数据包含未支持类型，不能跳过：'+str(kind))
                if size and data_rva:ignored.append((data_rva,data_rva+size))
    def without_debug(section):
        rva,virtual_size,content=section
        value=bytearray(content)
        for start,end in ignored:
            begin=max(start,rva)-rva;finish=min(end,rva+len(value))-rva
            if begin<finish:value[begin:finish]=b'\0'*(finish-begin)
        return rva,virtual_size,bytes(value)
    before={name:without_debug(section) for name,section in before.items()}
    after={name:without_debug(section) for name,section in after.items()}
    if before.keys()!=after.keys():raise RuntimeError('UPX解压段集合不一致')
    for name,value in before.items():
        if value[:2]!=after[name][:2] or value[2]!=after[name][2]:
            raise RuntimeError('UPX解压运行段变化：'+name)
    if a.base!=b.base or a.machine!=b.machine or a.read(a.base+a.directory(0)[0],40)!=b.read(b.base+b.directory(0)[0],40):
        raise RuntimeError('UPX解压映像或导出表不一致')
    if struct.unpack_from('<I',a.data,a.opt+16)!=struct.unpack_from('<I',b.data,b.opt+16):
        raise RuntimeError('UPX解压入口变化')
    symbols=import_symbols(original)
    if symbols!=import_symbols(unpacked):raise RuntimeError('UPX解压导入地址或DLL/函数变化')
    print(f'UPX解压非导入段字节/布局与{len(symbols)}项导入地址及函数语义一致。')
    return {'非导入运行段':'除已解析调试元数据外，字节及虚拟布局完全一致','导入项数':len(symbols),'导入表':'IAT地址、DLL及函数语义一致，允许UPX重排元数据'}

def test_upx_roundtrip_rejections(original,unpacked):
    """用真实解压PE损坏副本核对代码、只读业务数据和导入仍受严格保护。"""
    pe=PE(unpacked);sections=runtime_sections(unpacked);raw=Path(unpacked).read_bytes()
    def offset(rva):
        for start,size,at in pe.sections:
            if start<=rva<start+size:return at+rva-start
        raise RuntimeError('损坏回放地址不在文件内')
    descriptor=pe.read(pe.base+pe.directory(1)[0],20)
    points=[('代码字节',offset(sections['.text'][0])),
            ('只读业务数据',offset(sections['.rdata'][0]+sections['.rdata'][1]//2)),
            ('导入DLL',offset(struct.unpack_from('<I',descriptor,12)[0]))]
    temporary=Path(unpacked).with_name('EDSlash-invalid-check.asi')
    for label,at in points:
        changed=bytearray(raw);changed[at]^=1;temporary.write_bytes(changed);rejected=False
        try:verify_upx_roundtrip(original,temporary)
        except RuntimeError:rejected=True
        finally:temporary.unlink()
        if not rejected:raise RuntimeError('压缩验证没有拒绝'+label+'损坏')
    return '代码、只读业务数据和导入DLL变更均被拒绝；没有执行损坏副本'

def test_upx_debug_record_compatibility(original,unpacked):
    """真实PE回放UPX保留全零调试槽的情况，并拒绝非空的未知类型记录。"""
    source,pe=PE(original),PE(unpacked)
    rva,size=source.directory(6)
    if not rva or not size or size%28:raise RuntimeError('调试槽回归需要完整MSVC调试目录')
    for start,length,raw in pe.sections:
        if start<=rva and rva+size<=start+length:
            at=raw+rva-start;break
    else:raise RuntimeError('调试槽回归目录不在实际文件段内')
    data=bytearray(pe.data)
    # PE32可选头的数据目录从96字节开始，第6项为调试目录；恢复其位置与长度。
    struct.pack_into('<II',data,pe.opt+96+6*8,rva,size)
    data[at:at+size]=b'\0'*size
    temporary=Path(unpacked).with_name('EDSlash-debug-slot-check.asi')
    try:
        temporary.write_bytes(data)
        verify_upx_roundtrip(original,temporary)
        # Type仍为0但填入一个非零数据长度，必须拒绝，不能扩展忽略范围。
        struct.pack_into('<I',data,at+16,1)
        temporary.write_bytes(data)
        try:verify_upx_roundtrip(original,temporary)
        except RuntimeError:pass
        else:raise RuntimeError('未知非空调试记录没有被拒绝')
    finally:
        temporary.unlink()
    return '保留目录的全零槽位通过，未知非空记录被拒绝；未执行回放副本'

if __name__ == "__main__":
    verify(Path(sys.argv[1]), Path(sys.argv[2]))
