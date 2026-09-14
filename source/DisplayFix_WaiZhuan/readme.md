# DisplayFix_WaiZhuan 源码说明

## v0.2.1 日志中文化

`v0.2.1` 只把 `DisplayFix.log` 全面改为简体中文，不改变 `v0.2.0` 的任何已封版逻辑。Steam `ResJM.Lib` 多语言兜底、`ComeOn.dll` CreateWindowExA class-atom OpenGL 修复、非 Steam 宽屏/HUD/DPI/输入路径全部保持原样。日志等级统一为 `[成功]/[信息]/[警告]/[失败]/[运行]`；历史英文日志仅作为旧版实机证据保留。

最终 `DisplayFix.asi` SHA-256：`e242e70e3a39afd71f57f45405bdaf52acb9e64c0f268e10f61401c00679cef4`。


这是《刀剑封魔录外传：上古传说》DisplayFix 的独立源码工程。当前正式封版为 `v0.2.1`；`v0.1-clean1` 继续作为纯净历史回退基线。

运行文件仍统一叫：

```text
DisplayFix.asi
DisplayFix.ini
```

## v0.2-test1 版本定位（历史成功 A/B）

`v0.2-test1` 继续以 `v0.1-clean1` 为主体：test2 稳定代码 + test4 已实机通过的 Steam `ResJM.Lib` 多语言兜底。

用户已经确认“删除 `ComeOn.dll` 后 OpenGL 可以启动”，因此本轮只新增一个激进实验：隔离该 DLL 对 `USER32!CreateWindowExA` 的全局 inline Hook 以及其 EDIT WndProc 后处理。ComeOn.dll、SteamAPI、官方多语言 Hook 均继续保留。

源码**仍不包含** test3~test9 的 DirectShow、ActiveMovie、MovieManager、teardown、worker/export 隔离等失败实验运行代码。Steam 官方 launcher 控制的开场动画明确不属于 DisplayFix 主程序修复范围。

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


## v0.2-test1 OpenGL A/B（历史预期已被实机修正）

当时原计划要求出现全局 Hook bypass `[OK]` 才算完整 A/B；实际用户日志只出现“callback neutralized only”，说明全局 Hook 仍在，但 OpenGL 已经成功启动。

因此 test1 的有效结论恰好与最初判读规则不同：**全局 CreateWindowExA Hook 不是必要根因，post-create callback 的 EDIT 专用逻辑才是收敛点。** PlugK 已由用户实机确认不能解决 OpenGL，后续仅把其逆向文档当辅助资料。


## v0.2-test2 历史定位

用户实机确认 test1 可以启动 OpenGL，但日志证明全局 `CreateWindowExA` Hook 并没有被绕过，只有 post-create callback neutralize 真正生效。

因此 test2 删除 test1 的 VirtualQuery Hook 对象扫描、USER32 trampoline bypass、callback 整段提前 return与旧 WndProc 恢复，只留下一个最小改动：`ComeOn.dll+0x2889` 的 `JNE 0x16` 改为 `JMP 0x16`，无条件跳过 `SetWindowLongA(GWL_WNDPROC, ComeOn.dll+0x26A0)`。

运行日志成功标志：

```text
[OK] Steam ComeOn.dll EDIT WndProc subclass disabled; global CreateWindowExA hook remains intact
[INFO] v0.2-test2 narrows the OpenGL fix to one JNE->JMP byte at ComeOn.dll RVA 0x2889
```

SteamAPI、多语言、CreateFileA Hook、CreateWindowExA 全局 Hook、callback 其它逻辑全部保留。


## v0.2-test2 实机失败与 v0.2-test3 根因闭环历史

`v0.2-test2` 已由用户实机确认仍然崩溃。它只禁止了 EDIT 的 `SetWindowLongA(GWL_WNDPROC)`，所以这个结果证明 test1 的成功并不是单纯来自“禁用自定义 WndProc”。

重新检查 callback 后发现 `RVA 0x2841` 取出的 `lpClassName` 会在只做 NULL 检查后被直接当字符串解引用；但 Win32 允许这里传 `MAKEINTATOM`。test3 因此撤销 test2 的 WndProc 禁用，只在 `lpClassName < 0x10000` 时跳过 ComeOn.dll 的字符串比较，正常字符串与官方 EDIT 功能全部保留。随后用户实机确认 OpenGL 成功，并记录到 `count=3 / last_atom=0xC1F2`，根因由动态证据闭环。

正常安装日志：

```text
[OK] Steam ComeOn.dll CreateWindowExA class-atom compatibility fix installed; official EDIT handling remains intact
[INFO] Steam OpenGL compatibility fix only bypasses unsafe ComeOn.dll string parsing for NULL/MAKEINTATOM class names
```


## v0.2.0 运行逻辑封版基线（v0.2.1 继承）

`v0.2.0` 不再继续缩小已经闭环的 class-atom guard，也不引入新的 Steam 影片/launcher 代码。正式版保留以下已通过组合：

- test2 的显示/HUD/DPI/输入稳定主线；
- test4 已实机通过的 `ResJM.Lib.<language>` 最终兜底；
- test3 已实机闭环的 `CreateWindowExA(lpClassName=MAKEINTATOM(...))` 安全修复；
- SteamAPI、ComeOn.dll 全局 `CreateWindowExA` Hook、普通字符串类名处理、官方 EDIT WndProc 全部保留。

OpenGL 根因已经闭合：ComeOn.dll callback 只检查 `lpClassName != NULL` 就按 C 字符串解引用，而 cnc-ddraw OpenGL 启动路径实机至少触发 3 次合法 class atom。正式版仅让 NULL/atom 跳过这段字符串解析，普通字符串仍走官方路径。

Steam 官方 launcher 控制的开场动画不属于本项目主程序修复范围，`v0.2.0` 不处理动画比例或播放行为。
