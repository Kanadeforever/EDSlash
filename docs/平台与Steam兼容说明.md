# 平台与 Steam 兼容说明

> 当前接续（2026-10-02）：统一DisplayFix＋QOL＋Controller、TOML及静态SDL3.4.16；用户确认性能候选1掉帧缓解、功能正常、实机验收通过。build.bat现在一次输出发行EDSlash.asi与debug/EDSlash_debug.asi，优化相同、运行段一致，UPX仅处理发行副本。MOD设置界面后置，未来F12和L3＋R3。最新配置／构建／验收见《统一配置与基础迁移说明.md》《完整接档说明.md》；下文旧阶段保留历史，不扩为所有设备或新产物全部实机。

## 1. 本体

Steam EXE 主体与非 Steam 版本高度接近，Steam 版额外通过启动桩加载 `ComeOn.dll`。DisplayFix 的 Steam 专项主要是 delayed full JMM apply；非 Steam 路径不执行该补偿。

Steam 性能/帧速历史问题不是本次统一重构的新变量，后续单独调查。

## 2. 外传多语言

外传 Steam 目录依赖语言化资源：

```text
ResJM.Lib.chs
ResJM.Lib.cht
ResJM.Lib.eng
ResJM.Lib.jpn
```

已确认正式方案：只对 basename 精确为 `ResJM.Lib` 的裸请求追加当前语言后缀；不覆盖整个 `CreateFileA` IAT。

关键历史地址：

- `CreateFileA` IAT：`0x005511E4`
- 主 EXE 低层 CreateFileA CALL：`0x0052824E`

## 3. 外传 Steam OpenGL 根因

最终实机闭环证明：ComeOn.dll 的全局 `CreateWindowExA` post-create callback 对 `lpClassName` 只检查 NULL，却没有识别合法 `MAKEINTATOM(atom)`；之后把低地址 atom 当 C 字符串解引用，导致 cnc-ddraw OpenGL 路径崩溃。

正式修复只为：

- `lpClassName == NULL`
- `0 < lpClassName < 0x10000` 的合法 class atom

跳过字符串类名解析；普通字符串类名仍走官方逻辑。

必须继续保留：

- SteamAPI
- 多语言
- 全局 CreateWindowExA Hook
- 官方 EDIT HWND 记录
- 官方 `SetWindowLongA` 自定义 WndProc

## 4. 禁止恢复的 Steam 影片路线

DirectShow/ActiveMovie/MovieManager 的历史逆向地址仍是知识，但此前实验没有证明 ComeOn.exe 进程内 ComeOn.dll 就是 Steam launcher 开场动画实际实例。统一项目不继续开发 launcher 影片补丁。

## 5. 当前统一版实机状态与后续测试矩阵

本体、外传现在不再各自发行 DisplayFix ASI；用户已经用同一个 `BladeSwordQOL.asi` 完成本体与外传实机测试并确认两边均可工作。后续若扩大兼容声明，仍应按以下矩阵分别验证：

- 本体 Steam；
- 本体非 Steam（若有测试环境）；
- 外传 Steam；
- 外传非 Steam（若有测试环境）；
- 外传 Steam + cnc-ddraw OpenGL；
- 外传语言资源切换。


## 2026-10-01 第八版实现同步

最新Controller为v0.1-dev8：体力幅度改为最大体力百分比（两位小数），方向闪避可开关及调距离，快捷动作图标与LT四键必杀准备/确认已实现；QOL成功拾取左侧文字已实现。两作离线检查通过，新增功能仍待实机；LB仍仅暂假定通过，LT+右摇杆原生菜单仍后置。本文旧阶段的“待实现/尚未修改/固定体力点数/闪避保持dev6”属于历史快照，当前实现、测试边界和参数以《手柄第八版与拾取提示验收说明.md》为准。


## 2026-10-01 第九版同步

最新Controller为dev9：新增OmnidirectionalGuard，原最低5点体力门与特殊穿防保留；动画收尾前图标保持，QOL拾取文字下移128像素。用户确认dev8必杀/方向闪避/拾取显示，现有日志来自外传，新增dev9待实机。SDK迁移到忽略的参考资料根，source/vendor从当前树删除，历史提交仍有旧SDK副本。本文旧版本说明保留为历史，当前以《手柄第九版全方位格挡与显示修复说明.md》为准。


## 2026-10-01 合并前验收与调查停点（历史）

用户确认dev9全向格挡、图标时长和文字下移通过，补充本体759行日志已归档。源码与双基线确认闪避0x66普通受击免疫，代码安排前半段窗口。自有MOD配置界面需求已纠正，原版设置页调查只作参考；界面操控尚未实现。用户要求先记录、停下并评估迁入主ASI，本轮只改文档，未迁移/构建/改配置。当前见《闪避无敌窗口与界面调研记录.md》《手柄迁入主插件评估.md》。


## 2026-10-02 统一项目代码检查同步

正式基线98bb27a：EDSlash.asi＋EDSlash.toml＋EDSlash.log，32位MinGW/CMake、静态SDL3.4.16；Controller已入ModuleRegistry。今后优先手柄，旧独立/INI/零导入迁移提案仅历史。此次只读查代码、核现有发行/完整版散列，未新构建/实机；发现闪避途中保存设置可能遗漏Guard延后同步，具体路径与边界见《统一项目手柄代码检查记录.md》。完整菜单、LT右摇杆和自有MOD设置界面仍未实现。


## 2026-10-02 闪避设置修复同步

此前检查发现的闪避运动设置延后同步已按用户认可的三点修复：真实状态17安全点、Guard返回应用结果、确认后更新代数。新增真实TOML与双版本Guard集成1212项，当前11组CTest及双产物完整构建通过，尚待实机；详见《闪避设置安全应用修复说明.md》。新发行/完整版已更新，UPX本轮未生成。菜单操控和MOD设置界面仍后置。旧阶段测试数与缺陷描述保留历史归属。
