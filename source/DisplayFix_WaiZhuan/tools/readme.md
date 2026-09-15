# 工具详细说明

## 2026-09-15 stripe1 当前同步（本节优先级最高）

- **当前竖条实验版：本体 `v0.3.4-stripe1` / 外传 `v0.2.3-stripe1`。** 本体 GUI/输入基线 `v0.3.3-layer1d` 已由用户实机确认最后生命周期边界也解决；外传仍不能用本体结果代替独立实机。
- 本轮只处理从早期宽屏阶段就存在的**最右侧竖条纹**，不扩大 GUI/Input Hook。用户在 `BaseHeight=480` 下实测：`16:9 / 32:9 / 8:9 / 40:9` 有竖条，`24:9` 没有。
- 旧宽度算法会得到：`16:9 -> 854 (mod8=6)`、`32:9 -> 1708 (mod8=4)`、`8:9 -> 428 (mod8=4)`、`40:9 -> 2134 (mod8=6)`、`24:9 -> 1280 (mod8=0)`。目前所有有问题样本的实际 `TargetWidth` 都**不是 8 像素倍数**，唯一无问题样本 `1280` 恰好是 8 像素倍数。
- 因此 stripe1 的单一实验变量是：把最终 `TargetWidth` 从“按比例四舍五入后只保证偶数”改成“**选择距离理想比例最近的 8 像素倍数**”。对应上述样本，新宽度为 `856 / 1704 / 424 / 2136 / 1280`。
- 这仍是**待实机验证的根因假设**，目前不能把“8 像素块处理”写成已确认事实。若 stripe1 消除竖条，才可把根因升级为已实机闭合；若竖条仍在，则立即回退，不继续扩大改动。
- `layer1d` 已通过的辅助 GUI HUD 后绘制、顶层 picker 单次返回值覆盖、独立面板短生命周期跟踪、Strategy FRONTEND/GAMEPLAY、Steam delayed JMM、96-DPI、0x0B/0x0E 防穿透全部保持不变。外传 Steam `ResJM.Lib`、ComeOn.dll class-atom 与官方 EDIT WndProc 红线也不变。
- 两套 `verify_build.py` 已新增 stripe1 防回归：要求 `TargetWidth` 使用 8 像素边界算法、旧“只补偶数”运行代码为 0，并继续锁住 layer1d GUI/Input 红线。
- 当前构建 SHA-256：本体 `f565b4d683746fcf9d1fa30d9728df66c7e4e83676758b69a40693ccbebb6b8e`；外传 `deb40b447c94dedd2301a665c59e27dfa3fdb3e2ff44dccc55a46e813b33d4c3`。当前环境仍没有四套 ComeOn.exe 与外传 Steam ComeOn.dll，所以深度 `verify_compatibility.py` 仍标记为待真实样本。
- **本体首轮实机最重要：继续使用 `BaseHeight=480`，优先测 `16:9` 和 `24:9`。** 日志中 `16:9` 应变成 `TargetWidth=856`，`24:9` 应保持 `1280`，并且 `stripe1目标宽度8像素对齐余数=0`。随后再复测 `32:9 / 8:9 / 40:9`。

> 下文保存 layer1d、layer1c、layer1b、test1~test7 以及更早阶段的完整历史。凡历史段落仍使用“当前版本 / 当前方案 / 竖条另案处理”等措辞，与本节冲突时一律以本节为准。

## 2026-09-15 layer1d 阶段记录（历史，已由 stripe1 当前同步取代）

- **当前封版候选：本体 `v0.3.3-layer1d` / 外传 `v0.2.2-layer1d`。稳定回退基线仍为本体 `v0.3.2` / 外传 `v0.2.1`。**
- 本体 `layer1c` 已完成实机验证：此前 layer1a/layer1b 遗留的物品底部重叠区按钮、技能“连招编辑”关闭按钮、HUD 重叠区输入 root、左侧独立装备/辅助面板在物品界面打开时的层级问题均已解决。用户结论为“问题完全解决了”。
- `layer1c` 仅剩一个很小的生命周期边界：**当左侧装备独立面板仍保持打开时，如果先关闭物品主窗口，独立面板会重新落回主 HUD 下方。** 用户日志已经证明该面板此前被动态识别为独立对象（本体实机样本对象 `0x04255670`、vtable `0x0052A60C`、矩形 `0,217,325,442`；对象地址只属于该次运行，不得硬编码）。
- 根因已由源码复核闭合：layer1c 的独立面板候选入口要求 `0x0B/0x0D/0x0E` 至少一个主菜单仍 active；物品 root 关闭以后，这个硬门槛立即失效，即使独立装备面板自身仍 active，也不再参加 HUD 后延迟 Draw。
- `layer1d` **不改变 layer1c 已实机通过的核心方案**。新增的只有“独立辅助面板短生命周期跟踪”：对象必须先在合法主菜单上下文中被识别并成功拥有动态 Draw wrapper，之后才记录“对象指针 + vtable + 本帧验证 epoch”。主 root 关闭以后，只要该对象仍在真实 `manager+0x1C -> object+0x08` Draw 链的 HUD 之前、仍 active、vtable 未变、有效矩形仍与 HUD 相交，就继续在 HUD 后绘制，并继续参与原版 picker 的单次 input-root 覆盖。
- 跟踪不是永久缓存。对象 inactive、离开 HUD 前 Draw 链、vtable 变化、矩形不再与 HUD 相交、退出 GAMEPLAY、进入 Strategy 切换或 HUD 不可用时，记录会立即清除；验证旧记录时不会直接解引用“表里的旧指针”，只会对当前真实 Draw 链重新枚举到的对象做检查，因此不会为了保留层级而制造悬空指针访问。
- **没有主菜单 active 时绝不发现新的未知对象。** layer1d 只允许继续保留此前已经由合法菜单上下文确认过的独立面板，所以不会因为普通跑图时某个窗口恰好与 HUD 相交就把它错误提升。
- 输入方案仍完全沿用 layer1c：只 Hook 原版顶层 picker 的单次返回值；`manager+0x40`、焦点、`+0x20/+0x24/+0x30`、child hit-test、按下/释放、键盘快捷键和按钮业务仍全部由原版处理。
- test1~test7 的 X/Y 位移、GetCursorPos 反算、`self+0xA8` 修补、`+0x30` 接管、按钮硬编码继续禁止；layer1b 的顶层链写入继续禁止。最右侧历史竖条纹仍单独处理，不属于本轮封版条件。
- 两套 `verify_build.py` 已通过新边界检查。本体当前 SHA-256：`a146348809cbba6a16d3602f5fff87d04274bd9c298409b96c3db86a335b4c9d`；外传当前 SHA-256：`e33d9e1b6813be5878a4cf1fce266fe7b4ab29fa91a78fa62a67bea84441133c`。本体只剩这一项 layer1d 最终实机确认；外传仍不能用本体结果代替，后续正式封版前至少应完成一次外传主线实机回归。


本目录服务 `v0.2.2-layer1d`，稳定回退基线 `v0.2.1`。
外传工具还必须锁死 Steam `ResJM.Lib` 与 ComeOn.dll class-atom / 官方 EDIT WndProc 兼容边界。

## verify_build.py

只读检查当前 ASI、INI、源码和仓库规范：PE32/i386、DLL、非零入口、`InitializeASI`、Import Directory=0、UTF-8 无 BOM、BAT CRLF、中文日志、`Font.FixDPI` 与 `Display.Enable` 初始化独立。

layer1d 专项检查包括：
- 固定三主菜单 HUD 后延迟 Draw 与动态辅助顶层 Draw wrapper 存在；
- 独立面板短生命周期跟踪必须存在，并逐帧复核真实 Draw 链、active、vtable 与 HUD 相交；没有主菜单时不得发现新的未知对象；
- 顶层 picker 单次返回值 Hook 存在；
- 输入候选保留原版 JMM 属性 `0x0D==1` 与原版 hit-rect 路径；
- `manager+0x40` 仍由原版更新；
- HUD 特殊 pass 不 Hook、不重放；
- 禁止 test1~test7 位移/命中补偿符号；
- 禁止 layer1b 顶层链重排函数和四个链字段写入。

## verify_compatibility.py

对 ComeOn.exe 做只读机器码/结构验证，不以整文件 SHA-256 作为兼容白名单。继续核对稳定分辨率、Strategy/JMM/HUD、world press/global release、0x09~0x0E、三菜单类及 UI manager Draw。layer1d 继续验证 picker callsite/原版命中矩形/JMM 属性路径的验证内容应以真实 EXE 为准。

当前环境没有用户真实 ComeOn.exe；因此这个工具本轮没有重新跑最终样本，不能把历史输出写成 layer1d 新通过。

## verify_compatibility.bat

Windows 拖拽/多文件入口，只调用 Python 验证器，不写任何游戏二进制。

## 当前状态

最终封包前必须重新运行 `verify_build.py`；兼容验证在拿到真实 EXE/DLL 后补跑。