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

独立字体补丁器：

```text
source\FontFix_WaiZhuan\
```

项目研究 / 接档文档：

```text
docs\DisplayFix_WaiZhuan\
```

最终 `release\` 严格只有 `DisplayFix.asi` 和 `DisplayFix.ini`。

## 构建

双击 `build.bat`。脚本使用 32 位 Windows 目标 clang + lld-link，从零生成 `release`。

BAT 硬规则：

- UTF-8 无 BOM；
- CRLF；
- 第一行 `chcp 65001 >nul`；
- 不递归扫描 Visual Studio LLVM，避免误选 ARM64 clang；
- release 只允许两个运行文件。

## v0.1-test1 已通过基线

用户已经在非 Steam 实机确认：

- 96 DPI 字体；
- FRONTEND 4:3 / GAMEPLAY fixed-Y 宽屏；
- Strategy enter force=1；
- Strategy exit 原版 mode 4；
- GUI/HUD 居中；
- 右侧 6 个菜单按钮；
- 返回主菜单恢复 4:3。

Steam 版也顺带确认大部分功能可用，但 OpenGL 后端动画后闪退仍留到后续专项。

## v0.1-test2 唯一运行逻辑改动

修复配置耦合：`[Display] Enable=0` 不再阻止 `[Font] FixDPI=1`。

初始化顺序现在是：

1. 解析 EXE 与配置；
2. 独立执行 Font.FixDPI；
3. 再根据 Display.Enable 决定是否继续安装宽屏 / HUD / 输入补丁。

因此可以只启用字体修复而完全保留原版 4:3。
