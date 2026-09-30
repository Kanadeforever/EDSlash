"""只读导出两作原生动作拆分证据；不修改 EXE、不安装 Hook、不运行游戏。"""
from pathlib import Path
import argparse
import hashlib
import json
import sys


# 每段都从已核对的函数入口解码；不能从任意地址前退几字节猜指令边界。
# 最后两个可选值用于只保留函数内的一段，但解码依然从真正入口开始。
REGIONS = {
    '技能按住属性派生': [(0x4D6180, 0x63), (0x4EB5E0, 0x63)],
    '活动动作停止请求消费': [(0x4697F0, 0x140), (0x477450, 0x140)],
    '输入帧的原版分发顺序': [(0x405FB0, 0x261), (0x40CEC0, 0x261)],
    '每帧重试历史滚屏及防御': [(0x4748C0, 0x705), (0x483140, 0x705)],
    '技能输入总入口': [(0x4752C0, 0x32F), (0x483B40, 0x32F)],
    '序列前缀匹配': [(0x4750F0, 0x6A), (0x483970, 0x6A)],
    '序列成员解析': [(0x475280, 0x3B), (0x483B00, 0x3B)],
    '时间方向条件': [(0x475160, 0xF9), (0x4839E0, 0xF9)],
    '鼠标转向适配': [(0x474340, 0x84), (0x482BC0, 0x84)],
    '原生招式执行': [(0x420E90, 0x149), (0x429480, 0x149)],
    '点到朝向业务': [(0x429D60, 0x20), (0x432D60, 0x20)],
    '动作历史回写': [(0x475070, 0x7F), (0x4838F0, 0x7F)],
    '运行时回写调用边界': [(0x4693A0, 0x3F8, 0x469751), (0x477000, 0x3F8, 0x4773B1)],
}

# 下列不是函数入口，而是从实际控制流核对的完整基本块；每段必须完整解码。
BLOCKS = {
    '右键松开生成原始动作十五包': [(0x41E410, 0x53), (0x426A55, 0x53)],
    '动作十五设置停止标记': [(0x41C3AA, 0x31), (0x42492A, 0x2D)],
    '普通世界主按钮松开': [(0x4A1CA0, 3), (0x4BB8B0, 3)],
    '普通世界次按钮松开': [(0x4744A0, 0xD), (0x482D20, 0xD)],
}

# 这些是已定位到真实指令边界的全局门写入。自动续段期间的关闭不能被新历史层忽略。
HISTORY_GATE_WRITES = [
    (0x5457F0, [(0x41A2DC, 0), (0x41A31D, 1), (0x46B65D, 0),
                (0x46B784, 1), (0x46B7C6, 1), (0x4B41B6, 1)]),
    (0x574D10, [(0x4227EB, 0), (0x42282C, 1), (0x4794CC, 0),
                (0x479658, 1), (0x47969A, 1), (0x4C7586, 1)]),
]


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baselines', type=Path, required=True, help='两份非 Steam 基线 EXE 所在目录')
    parser.add_argument('--output', type=Path, required=True, help='证据文本的输出位置')
    args = parser.parse_args()
    # 参考资料目录是项目只读边界。即便误把它填成输出位置，也不能写入。
    output = args.output.resolve()
    reference = Path(__file__).resolve().parents[3] / '参考资料'
    if output.is_relative_to(reference.resolve()) or output.is_relative_to(args.baselines.resolve()):
        raise SystemExit('证据输出不能放入只读参考资料或基线程序目录。')
    try:
        import pefile
        import capstone
    except ImportError as exc:
        raise SystemExit('此只读研究工具需要 pefile 和 capstone；普通构建不依赖它们。') from exc
    metadata = json.loads(Path(__file__).with_name('profiles.json').read_text(encoding='utf-8'))
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    lines = ['# 手柄原生动作拆分原始证据', '',
             '由两份准确散列的非 Steam EXE 只读导出。指令证明局部控制流，不代替实机命中记录。', '']
    for index, profile in enumerate(metadata['profiles']):
        source = args.baselines / f'ComeOn-{profile["tag"]}-NonSteam.exe'
        data = source.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != profile['sha256']:
            raise SystemExit(f'拒绝处理散列不匹配的样本：{source.name}')
        binary = pefile.PE(data=data)
        base = binary.OPTIONAL_HEADER.ImageBase
        lines.extend([f'## {profile["name"]}', f'SHA-256：{digest}', ''])
        for label, pair in (REGIONS | BLOCKS).items():
            region = pair[index]
            start, size = region[:2]
            display_from = region[2] if len(region) > 2 else start
            decoded = list(engine.disasm(binary.get_data(start-base, size), start))
            if not decoded or decoded[-1].address + decoded[-1].size != start + size:
                raise SystemExit(f'解码未覆盖完整指令范围：{profile["tag"]} {label}')
            lines.extend([f'### {label}：0x{start:08X}', ''])
            for ins in decoded:
                if ins.address >= display_from:
                    lines.append(f'{ins.address:08X}  {bytes(ins.bytes).hex(" "):<30}  {ins.mnemonic} {ins.op_str}')
            lines.append('')
        gate, writes = HISTORY_GATE_WRITES[index]
        initial = int.from_bytes(binary.get_data(gate-base, 4), 'little')
        if initial != 1:
            raise SystemExit('动作历史门默认值与已核对基线不一致')
        lines.extend([f'### 动作历史回写门：0x{gate:08X}，初始值 1', ''])
        for address, value in writes:
            expected = b'\xC7\x05' + gate.to_bytes(4, 'little') + value.to_bytes(4, 'little')
            actual = binary.get_data(address-base, 10)
            if actual != expected:
                raise SystemExit(f'历史门写入指令不匹配：0x{address:08X}')
            ins = next(engine.disasm(actual, address))
            lines.append(f'{address:08X}  {actual.hex(" "):<30}  {ins.mnemonic} {ins.op_str}')
        lines.append('')
        print(f'{profile["name"]}：散列通过，已完整解码 {len(REGIONS)} 组函数范围与 {len(BLOCKS)} 组基本块。')
    # 全部读取和校验成功后才写结果，避免导出半份单版本证据。
    args.output.write_bytes(('\r\n'.join(lines).rstrip() + '\r\n').encode('utf-8'))
    print(f'已保存：{args.output}')


if __name__ == '__main__':
    main()
