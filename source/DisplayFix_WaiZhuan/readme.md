# DisplayFix_WaiZhuan 源码说明

这是《刀剑封魔录外传：上古传说》DisplayFix 的独立源码工程。当前纯净工作基线为 `v0.1-clean1`。

运行文件仍统一叫：

```text
DisplayFix.asi
DisplayFix.ini
```

## clean1 版本定位

`v0.1-clean1` 的代码主体严格回滚到 `v0.1-test2`，只从 `v0.1-test4` 单独移植用户已经实机确认成功的 Steam `ResJM.Lib` 多语言最终兜底。

当前源码**不包含** test3~test9 的 DirectShow、ActiveMovie、MovieManager、teardown、worker/export 隔离等实验运行代码。OpenGL 崩溃和 Steam 开场动画拉伸仍是已知未解决问题；clean1 的目的就是得到一个没有这些历史实验干扰的纯净工作版本。

## 目录

```text
source\DisplayFix_WaiZhuan\
├─ src\DisplayFix.c
├─ template\DisplayFix.ini
├─ tools\verify_build.py
├─ tools\verify_compatibility.py
├─ tools\verify_compatibility.bat
├─ tools\工具详细说明.md
├─ build.bat
└─ readme.md
```

独立字体补丁器：

```text
source\FontFix_WaiZhuan\
```

项目研究/接档文档：

```text
docs\DisplayFix_WaiZhuan\
```

最终 `release\` 严格只有 `DisplayFix.asi` 和 `DisplayFix.ini`。

## 已通过基线

v0.1-test1 已由用户在非 Steam 实机确认：96 DPI、FRONTEND 4:3、Strategy 游戏内 fixed-Y 宽屏、HUD/GUI 居中、右侧 6 个菜单按钮、退出 Strategy 后恢复原版 4:3。

v0.1-test2 已修复 `Display.Enable=0` 时错误连带关闭 `Font.FixDPI=1` 的配置耦合。

v0.1-test4 的 Steam raw `ResJM.Lib` callsite shim 已实机确认消除裸 `ResJM.Lib` 缺失弹窗；clean1 只保留这一项。

## 构建

双击 `build.bat`。脚本使用 Win32/x86 clang + lld-link 从零生成 `release`。

BAT 硬规则：UTF-8 无 BOM、CRLF、首行 `chcp 65001 >nul`、不递归扫描 Visual Studio LLVM、避免误选 ARM64、正文 REM/echo 行末两个半角空格。

构建后运行 `tools\verify_build.py`；需要检查目标 EXE 结构时使用 `tools\verify_compatibility.py` 或拖拽到 `verify_compatibility.bat`。
