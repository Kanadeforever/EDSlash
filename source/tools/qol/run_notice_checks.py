"""验证QOL拾取提示的实际库存判定与原版文字调用；不启动或修改游戏。"""
from pathlib import Path
import importlib.util
import re
import subprocess
import sys


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    source=Path(__file__).resolve().parents[2]
    spec=importlib.util.spec_from_file_location('controller_build',source/'tools/controller/build_controller.py')
    builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
    compiler,environment=builder.compiler()
    work=source/'.build/QolChecks';work.mkdir(parents=True,exist_ok=True)
    test=work/'test_notice.exe'
    subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-finput-charset=UTF-8','-fexec-charset=UTF-8',str(Path(__file__).with_name('test_notice.c')),
                    '-o',str(test)],check=True,env=environment)
    subprocess.run([str(test)],check=True,env=environment)
    # 单独检查两作冻结的五个调用签名；宿主替身不能证明真实EXE地址正确。
    text=(source/'src/Modules/QOL/PickupNotice.c').read_text(encoding='utf-8')
    rows=re.findall(r'\{(0x[0-9A-F]+ul(?:,0x[0-9A-F]+ul){7}), \{([^\n]+)\}\}',text)
    baselines=source.parent/'参考资料/刀剑封魔录系列反编译资料库_v0.31/基线程序'
    for row,tag in zip(rows,['DaoJian','WaiZhuan'],strict=True):
        path=baselines/f'ComeOn-{tag}-NonSteam.exe'
        if not path.exists():
            print(f'{tag}：未携带基线，跳过样本签名复核');continue
        pe=builder.PE(path)
        addresses=[int(v[:-2],16) for v in row[0].split(',')]
        signatures=re.findall(r'\{([^{}]+)\}',row[1])
        assert len(signatures)==5
        for address,signature in zip(addresses[:5],signatures,strict=True):
            assert pe.read(address,12)==bytes(int(v,16) for v in signature.split(','))
        print(f'{tag}：拾取提示五个原版入口签名通过')


if __name__=='__main__':
    main()
