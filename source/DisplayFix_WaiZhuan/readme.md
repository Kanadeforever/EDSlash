# DisplayFix_WaiZhuan 源码说明

这是《刀剑封魔录外传：上古传说》DisplayFix 的独立源码工程。运行文件仍统一叫：

```text
DisplayFix.asi
DisplayFix.ini
```

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

独立字体补丁器位于：

```text
source\FontFix_WaiZhuan\
```

项目研究/接档文档位于：

```text
docs\DisplayFix_WaiZhuan\
```

最终玩家目录 `release\` 严格只有 `DisplayFix.asi` 和 `DisplayFix.ini`。

## 构建

双击 `build.bat`。脚本使用 32 位 Windows 目标的 clang + lld-link，从零生成 `release`。

BAT 硬规则：

- UTF-8 无 BOM；
- CRLF；
- 第一行 `chcp 65001 >nul`；
- 不递归扫描 Visual Studio 的 LLVM，避免误选 ARM64 clang；
- release 只允许两个运行文件。

## v0.1-test1 范围

本轮以本传 v0.3 的实机成功架构为参考，但所有外传地址/签名均重新从外传 EXE 验证：

- 96 DPI 字体；
- FRONTEND 4:3 / GAMEPLAY fixed-Y 宽屏生命周期；
- Strategy enter force=1；
- Strategy exit 强制原版 mode 4；
- JMM 布局；
- 主 HUD / 0x0B / 0x0E / world press / global release 结构已静态闭合。

Steam 专项故意留待非 Steam 稳定后处理。
