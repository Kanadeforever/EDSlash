"""四样本只读核查：地址、原指令来源、结构偏移、清栈及跨模块游戏身份。

这是研究工具，不修改运行时代码、不构建/部署、不连接玩家进程。
报告中的片段引用只是原指令证据，不自动等同完整函数语义或实机验收。
"""
from pathlib import Path
import argparse
import contextlib
import hashlib
import io
import json
import re
import struct

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM
import pefile
from controller.verify_profiles import PE, verify_baselines
from compat.compat_daojian import verify_one as verify_daojian
from compat.compat_waizhuan import verify_one as verify_waizhuan

ROOT=Path(__file__).resolve().parents[2]
CONTROLLER=ROOT/'source/src/Modules/Controller'

# 以下片段从真实入口解码。非档案函数的地址分别取两作证据，不能按统一差值猜外传。
# 长度只限定核查范围，片段内出现引用不等于已证明所有分支可达。
EXTRA_REGIONS={
    '世界主操作':[(0x473F10,0x260),(0x482790,0x2A0)],
    '原待执行事件消费者':[(0x41CD40,0x130),(0x425330,0x130)],
    '原生命调整':[(0x424770,0x110),(0x42D3D0,0x110)],
    '原动态物件选择器':[(0x44EC40,0x300),(0x45A540,0x320)],
    '原完整输入帧':[(0x405FB0,0x261),(0x40CEC0,0x261)],
}
WINDOWS={'resolver':0x290,'skill_release':0x330,'history_record':0x80,
         'guard_check':0xA0,'dodge_start':0x110,'inventory_get':0x40,
         'inspect_static_picker':0x237,'menu_talk_tick':0x170,
         'menu_skill_primary':0x400,'player_class_probe':0x100,
         'menu_message_primary':0x70,'menu_load_primary':0x190,
         'menu_title_show':0x150,'menu_message_open':0x130,
         'grid_commit':0x150,'stamina_adjust':0x100,'dodge_motion':0x160,
         'facing_direction':0xB0,'throw_group':0x60,'quick_use':0x300,
         'menu_message_base_tick':0xE1}
OFFSET_SOURCES={
    'invalid_offset':('skill_release','ebx'),
    'interact_offset':('世界主操作','eax'),
    'active_offset':('guard_check','esi'),
    'pending_offset':('原待执行事件消费者','esi'),
    'health_offset':('原生命调整','esi'),
    'dodge_counter_offset':('dodge_motion','esi'),
    'inspect_ready_offset':('原待执行事件消费者','edi'),
    'stamina_offset':('stamina_adjust','esi'),
}


def text(path):
    return path.read_text(encoding='utf-8-sig')


def code_only(source):
    # 去掉注释，同时保留行数。历史地址写在注释里不能误报为运行代码串用。
    source=re.sub(r'/\*.*?\*/',lambda m:'\n'*m.group().count('\n'),source,flags=re.S)
    return re.sub(r'//[^\n]*','',source)


def initializer(source, marker):
    # C初始化器含多层花括号。配对读取整个范围，不能用首个右括号截断签名数组。
    start=source.index('{',source.index(marker));depth=0
    for end in range(start,len(source)):
        if source[end]=='{':depth+=1
        elif source[end]=='}':
            depth-=1
            if depth==0:return source[start:end+1]
    raise ValueError('初始化器没有闭合')


def rows(source):
    # 取初始化器第一层的每一行对象，嵌套的签名数组保留在本行里。
    depth=0;start=None;result=[]
    for pos,char in enumerate(source):
        if char=='{':
            depth+=1
            if depth==2:start=pos
        elif char=='}':
            if depth==2:result.append(source[start:pos+1])
            depth-=1
    return result


def integers(source):
    return [int(v,16) for v in re.findall(r'0x([0-9a-fA-F]+)',source)]


def audit(path, profile, decoder, source_uses, this_uses):
    pe=PE(path);image=pefile.PE(str(path))
    imports={i.address:i.name.decode() if i.name else str(i.ordinal)
             for entry in image.DIRECTORY_ENTRY_IMPORT for i in entry.imports}
    expect={'keyboard_iat':'GetKeyboardState','async_iat':'GetAsyncKeyState','cursor_position_iat':'GetCursorPos'}
    for field,name in expect.items():assert imports.get(profile['addresses'][field])==name,(field,name)
    assert image.OPTIONAL_HEADER.AddressOfEntryPoint==profile['entry']
    assert image.OPTIONAL_HEADER.SizeOfImage==profile['size']
    index=profile['game_id']-1;regions={}
    for field,size in WINDOWS.items():
        address=profile['addresses'][field]
        regions[field]=list(decoder.disasm(pe.read(address,size),address))
    for label,pair in EXTRA_REGIONS.items():
        address,size=pair[index];regions[label]=list(decoder.disasm(pe.read(address,size),address))
    offset_evidence={};records=[]
    for field,value in profile['addresses'].items():
        witnesses=[]
        for label,instructions in regions.items():
            for inst in instructions:
                absolute=any((op.type==X86_OP_MEM and not op.mem.base and not op.mem.index and op.mem.disp==value)
                             or (op.type==X86_OP_IMM and op.imm==value) for op in inst.operands)
                if absolute:witnesses.append(f'{label} {inst.address:08X} {inst.mnemonic} {inst.op_str}')
        # 每个档案字段都列入报告：不能只汇报容易通过的签名，遗漏未验证的全局。
        status='12字节签名及既有关系核对' if field in profile['signatures'] else '地址字段，见原引用/专项检查'
        if field.endswith('_vtable'):status='既有核查从原虚表解码焦点槽及对应业务槽'
        if field in expect:status='原PE导入表按API名称核对'
        if field in OFFSET_SOURCES:status='原函数的指定对象寄存器偏移核对'
        if field in ('resolver_call','history_call','retry_call','end_call'):status='既有核查解码rel32实际目标'
        if value==0:status='本作明确不使用的可选入口'
        records.append({'字段':field,'数值':f'{value:08X}','源码使用':source_uses.get(field,[]),
                        '已有签名':field in profile['signatures'],'原片段引用':witnesses[:8],'证据类型':status})
    for field,(region,base) in OFFSET_SOURCES.items():
        value=(profile['offsets']|profile['addresses'])[field];hits=[]
        for inst in regions[region]:
            if any(op.type==X86_OP_MEM and inst.reg_name(op.mem.base)==base and op.mem.disp==value
                   for op in inst.operands):hits.append(f'{inst.address:08X} {inst.mnemonic} {inst.op_str}')
        assert hits,(profile['name'],field,'没有找到指定角色基址的原偏移读取')
        offset_evidence[field]={'偏移':f'{value:X}','原入口':region,'对象寄存器':base,'指令':hits}
    abi=[]
    for field,cleanup in this_uses.items():
        if field not in profile['addresses']:continue
        address=profile['addresses'][field]
        ret=next((i for i in decoder.disasm(pe.read(address,0x1800),address) if i.mnemonic.startswith('ret')),None)
        assert ret is not None,(field,'没有定位清栈返回')
        actual=int(ret.op_str,0) if ret.op_str else 0
        assert actual in cleanup,(profile['name'],field,actual,sorted(cleanup))
        abi.append({'字段':field,'栈字节':actual,'原返回':f'{ret.address:08X} {ret.mnemonic} {ret.op_str}'})
    # 运行成熟的显示核查器；其保留研究结构和当前生产hook范围仍应分开解读。
    display=(verify_daojian if index==0 else verify_waizhuan)(path)
    # 独立核对非This0..4类型：绘制、文本、声音及通用确认使用更长参数列表或cdecl。
    special={'relation':0,'menu_sound':0,'icon_draw':36,'cursor_sprite_draw':24,'menu_message_open':24}
    special_abi=[]
    for field,size in special.items():
        address=profile['addresses'][field]
        ret=next((i for i in decoder.disasm(pe.read(address,0x1800),address) if i.mnemonic.startswith('ret')),None)
        actual=int(ret.op_str,0) if ret and ret.op_str else 0
        assert ret and actual==size,(profile['name'],field,'特殊调用清栈不符')
        special_abi.append({'字段':field,'原返回':f'{ret.address:08X} {ret.mnemonic} {ret.op_str}'})
    return {'游戏':profile['name'],'SHA256':hashlib.sha256(pe.data).hexdigest(),
            '档案字段':records,'独立偏移证据':offset_evidence,'直接This清栈':abi,
            '特殊调用清栈':special_abi,'导入核对':expect,'显示专项':display},pe,image,imports


def audit_runtime_qol(profile, pe, image, imports):
    source=code_only(text(ROOT/'source/src/Runtime/GameProfile.c'))
    marker='PROFILE_DAOJIAN' if profile['game_id']==1 else 'PROFILE_WAIZHUAN'
    values=integers(initializer(source,marker));assert len(values)==14
    assert values[0]==image.OPTIONAL_HEADER.SizeOfImage and values[1]==image.OPTIONAL_HEADER.AddressOfEntryPoint
    for rva,name in zip(values[2:4],('GetModuleHandleA','GetProcAddress')):
        assert imports.get(0x400000+rva)==name,(marker,name)
    # QOL字段顺序来自当前头文件，再对应当前C初始化器，避免另外手写一份同样可能错的表。
    header=initializer(text(ROOT/'source/src/Runtime/GameProfile.h'),'typedef struct QolGameProfile')
    fields=re.findall(r'unsigned long (\w+);',header);assert len(fields)==10
    qol=dict(zip(fields,[v+0x400000 for v in values[4:]]));a=profile['addresses']
    assert qol['ground_manager_global_rva']==a['entities_global']
    assert qol['action_slot_table_global_rva']==a['handles_global']
    assert pe.read(qol['ground_item_update_rva'],7)==bytes.fromhex('83 EC 08 8D 54 24 00')
    assert pe.read(qol['input_update_rva'],5)==bytes.fromhex('33 C0 8D 51 08')
    assert pe.read(qol['pickup_entry_rva'],5)==bytes.fromhex('56 8B 74 24 0C')
    # 输入帧把ECX设为键盘对象；刷新函数把ECX+8传给GetKeyboardState。
    # Controller只接管这个buffer，因此仅IAT正确还不够，buffer地址也必须从原调用链验证。
    frame=EXTRA_REGIONS['原完整输入帧'][profile['game_id']-1][0]
    head=pe.read(frame,16);assert head[3]==0xB9 and head[11]==0xE8
    keyboard_object=struct.unpack('<I',head[4:8])[0]
    assert keyboard_object+8==a['keyboard_buffer']
    assert frame+16+struct.unpack('<i',head[12:16])[0]==qol['input_update_rva']
    keyboard_call=pe.read(qol['input_update_rva']+0x26,6)
    assert keyboard_call[:2]==b'\xff\x15' and struct.unpack('<I',keyboard_call[2:])[0]==a['keyboard_iat']
    notice=rows(initializer(code_only(text(ROOT/'source/src/Modules/QOL/PickupNotice.c')),'static const NoticeProfile profiles'))
    numbers=integers(notice[profile['game_id']-1]);assert len(numbers)==68
    # 五函数的源码签名由对应EXE复核，三个全局还须与Controller独立档案一致。
    for i in range(5):assert pe.read(numbers[i],12)==bytes(numbers[8+i*12:20+i*12])
    assert numbers[2]==a['inventory_get'] and numbers[3]==a['item_at']
    assert numbers[5:8]==[a['inventory_root'],a['world_global'],a['skill_global']]
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    # QOL的动作入口有六个栈参数，拾取入口为cdecl，不能套用手柄管理器的四参数提交。
    for address,cleanup in [(qol['action_entry_rva'],24),(qol['pickup_entry_rva'],0),
                            (qol['show_item_name_rva'],4),(numbers[0],4),(numbers[1],20),(numbers[4],0)]:
        ret=next((i for i in decoder.disasm(pe.read(address,0x2000),address) if i.mnemonic.startswith('ret')),None)
        assert ret and (int(ret.op_str,0) if ret.op_str else 0)==cleanup,(hex(address),'QOL清栈错误')
    # 字段名带_rva，但上面调用PE.read时已加映像基址。报告必须同时标明两种数值，
    # 不能把VA写进RVA列，否则读报告的人再次加基址就会造成新的交叉错位。
    return {'Runtime和QOL字段':{k:{'源码RVA':f'{v-0x400000:08X}','核对VA':f'{v:08X}'} for k,v in qol.items()},
            '拾取提示五函数及三个全局':'通过'}


def audit_focus_source(profile,pe):
    """核对真正Runtime生成表，避免Controller迁出后共享服务悄悄串用地址。"""
    source=code_only(text(ROOT/'source/src/Runtime/FocusData.h'))
    entries=rows(initializer(source,'static const FocusBackend focus_profiles'))
    values=integers(entries[profile['game_id']-1]);assert len(values)==54
    keys=('skill_global','menu_hud_vtable','cursor_sprite_draw','focus_rect_draw','focus_frame_get','focus_image_get')
    assert values[:6]==[profile['addresses'][key] for key in keys]
    for i,address in enumerate(values[2:6]):assert pe.read(address,12)==bytes(values[6+i*12:18+i*12])
    renderer=code_only(text(ROOT/'source/src/Runtime/Focus.c'))
    assert not re.search(r'g_profile|g_input|g_intent|#include.*Controller',renderer)
    client=code_only(text(ROOT/'source/src/Modules/Controller/Cursor.c'))
    assert 'RuntimeFocus_Register' in client and 'g_profile->focus_rect_draw' not in client
    return {'共享生成表六字段':dict(zip(keys,[f'{v:08X}' for v in values[:6]])),
            '四原函数源码签名与Controller逆依赖检查':'通过'}


def audit_display_source(profile, pe, imports):
    source=code_only(text(ROOT/f'source/src/Modules/DisplayFix/Backend_{profile["tag"]}.c'))
    apis=[]
    for name,address in re.findall(r'#define GAME_(\w+)\s+\(\*\(\w+\*\)(0x[0-9A-Fa-f]+)u\)',source):
        actual=imports.get(int(address,16));assert actual==name,(profile['name'],name,actual)
        apis.append({'API':name,'IAT':address})
    patterns=[]
    # 从生产C数组本身取得字节，不只相信另一个研究工具内复制的签名。
    for name,body in re.findall(r'static const BYTE (\w+)\[\]\s*=\s*\{([^}]+)\}',source,re.S):
        if not name.endswith(('_PATTERN','_HEAD')):continue
        tokens=[v.strip() for v in body.split(',') if v.strip()]
        if any(not re.fullmatch(r'(?:0x[0-9A-Fa-f]+|\d+)[uUlL]*',v) for v in tokens):continue
        values=bytes(int(re.sub(r'[uUlL]+$','',v),0) for v in tokens)
        mask_name=name[:-8]+'_MASK' if name.endswith('_PATTERN') else ''
        mask_match=re.search(r'static const char '+re.escape(mask_name)+r'\[\]\s*=\s*([^;]+);',source) if mask_name else None
        mask=''.join(re.findall(r'"([x?]+)"',mask_match[1])) if mask_match else 'x'*len(values)
        assert len(mask)==len(values),(name,'字节/掩码长度不相符')
        # 固定字节先转义，问号位才允许任意单字节，不能让机器字节成为正则元字符。
        regex=b''.join(re.escape(bytes([v])) if m=='x' else b'.' for v,m in zip(values,mask))
        matches=list(re.finditer(b'(?=('+regex+b'))',pe.data,re.S))
        patched_state='PATCHED' in name
        # 已修复字体签名是幂等检测分支，原版EXE里应不存在，不能误报为原入口缺失。
        assert matches or patched_state,(profile['name'],name,'生产原始签名在样本中不存在')
        patterns.append({'数组':name,'长度':len(values),'文件匹配数':len(matches),
                         '源码引用次数':len(re.findall(r'\b'+name+r'\b',source)),
                         '修复后状态检测':patched_state})
    macro=re.search(r'#define GAME_DISPLAY_MANAGER.*?(0x[0-9A-Fa-f]+)',source)
    # 原输入帧的resolver进一步调用显示投影，本体/外传在resolver+60处装入显示对象。
    original=pe.read(profile['addresses']['resolver']+0x60,5)
    assert original[0]==0xB9
    display_object=struct.unpack('<I',original[1:])[0]
    declared=int(macro[1],16);used=len(re.findall(r'\bGAME_DISPLAY_MANAGER\b',source))>1
    # 未使用的旧声明进入问题清单；如果未来启用了错误声明，应立即让核查失败。
    assert declared==display_object,(profile['name'],'运行代码使用了另一作显示对象地址')
    return {'生产IAT':apis,'生产签名数组':patterns,
            '显示对象声明':{'声明':f'{declared:08X}','原指令':f'{display_object:08X}',
                            '当前有读取':used,'一致':declared==display_object}}


def audit_font_tools(profile, pe):
    path=ROOT/f'source/tools/font_fix/{profile["tag"]}/apply_dpi_font_fix.py'
    source=text(path)
    start=source.index('ORIGINAL_CONTEXT = bytes.fromhex(');end=source.index('\n)',start)
    body=source[start:end]
    fragments=re.findall(r'^\s*"([0-9A-Fa-f ]+)"',body,re.M)
    context=bytes.fromhex(''.join(fragments));hits=[];pos=0
    while True:
        found=pe.data.find(context,pos)
        if found<0:break
        hits.append(found);pos=found+1
    assert len(hits)==1,(profile['name'],'字体工具上下文不唯一')
    assert context[7:9]==b'\xff\x15'
    return {'字体上下文匹配':'唯一','文件偏移':f'{hits[0]:X}',
            '输出行为':'源码当前将派生EXE写到输入同目录；不能在只读参考资料目录运行写入操作'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    output=args.output.resolve()
    if output.is_relative_to((ROOT/'参考资料').resolve()):raise SystemExit('禁止写参考资料')
    metadata=json.loads(text(ROOT/'source/tools/controller/profiles.json'))
    # 正式核查必须有四份准确游戏文件，缺样本直接拒绝，不把跳过记作成功。
    with contextlib.redirect_stdout(io.StringIO()):assert verify_baselines(metadata)
    profiles=metadata['profiles'];uses={};this_uses={}
    assert [p['game_id'] for p in profiles]==[1,2,1,2],'四档案游戏身份次序错误'
    for path in CONTROLLER.glob('*.c'):
        source=code_only(text(path))
        for line,content in enumerate(source.splitlines(),1):
            for field in re.findall(r'g_profile->(\w+)',content):uses.setdefault(field,[]).append(f'{path.name}:{line}')
        for count,field in re.findall(r'\(\(This([0-4])\)g_profile->(\w+)\)',source):
            this_uses.setdefault(field,set()).add(int(count)*4)
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    report={'证据边界':'全部档案字段列入清单；引用片段/首个RET不等于所有路径完整语义验证，不等于实机验收。',
            '样本':[],'跨发行版':[],'后端串用检查':[]}
    for profile in profiles:
        path=ROOT/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f'ComeOn-{profile["tag"]}-{profile["edition"]}.exe'
        result,pe,image,imports=audit(path,profile,decoder,uses,this_uses)
        result['Runtime与QOL']=audit_runtime_qol(profile,pe,image,imports);result['Runtime焦点绘制']=audit_focus_source(profile,pe);report['样本'].append(result)
        result['显示生产源码']=audit_display_source(profile,pe,imports)
        result['字体工具只读上下文']=audit_font_tools(profile,pe)
        declared=result['显示生产源码']['显示对象声明']
        print(profile['name'],'原入口/偏移/ABI已核；显示对象声明一致=',declared['一致'],
              '当前有读取=',declared['当前有读取'])
    for i in range(2):
        before,after=profiles[i],profiles[i+2]
        for category in ('addresses','offsets','signatures'):
            assert before[category]==after[category],(before['tag'],category,'两发行版不能假定一致')
        # 扩大到每个签名地址后384字节的对应发行版对照，仍不声称覆盖完整所有函数。
        main_pe=PE(ROOT/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f'ComeOn-{before["tag"]}-NonSteam.exe')
        steam_pe=PE(ROOT/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'/f'ComeOn-{before["tag"]}-Steam.exe')
        for field in before['signatures']:
            address=before['addresses'][field]
            assert main_pe.read(address,384)==steam_pe.read(address,384),(before['tag'],field,'扩大窗口存在发行版差异')
        report['跨发行版'].append(before['tag']+'：对应Steam/非Steam档案地址、偏移及局部签名逐项相同，完整SHA分别匹配')
    for own,other in ((profiles[0],profiles[1]),(profiles[1],profiles[0])):
        path=ROOT/f'source/src/Modules/DisplayFix/Backend_{own["tag"]}.c';suspects=[]
        for line,content in enumerate(code_only(text(path)).splitlines(),1):
            for value in integers(content):
                foreign=[k for k,v in other['addresses'].items() if v==value and own['addresses'][k]!=v]
                if foreign:suspects.append({'行':line,'地址':f'{value:08X}','字段':foreign})
        report['后端串用检查'].append({'文件':path.name,'另一作档案地址候选':suspects})
    output.write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').replace('\n','\r\n').encode('utf-8'))
    print('报告已写入',output)


if __name__=='__main__':main()
