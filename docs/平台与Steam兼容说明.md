# 平台与 Steam 兼容说明

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
