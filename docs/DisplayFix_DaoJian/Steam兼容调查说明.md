# Steam 兼容调查说明

## 已确认文件关系

Steam `ComeOnSteam.exe` 与非 Steam `ComeOn.exe` 大小相同，主体代码几乎一致，只加入一个非常小的启动桩。

Steam EXE 会在启动时：

```text
LoadLibraryA("ComeOn.dll")
-> 恢复现场
-> 继续原版启动函数
```

Steam EXE SHA-256：`0887ceae7589999a389ec271d690e1c55204d59575f8f2adb5ef46a1e605b3f5`

非 Steam SHA-256：`c8d1aa33272a2c28e94f0d18eda2d6a8fcb14796644d8f8bed467b744ddc94b5`

`ComeOn.dll` SHA-256：`9f8f71ad958c5c5d33e1b5aeb78603b970fd16d7e0b674aa7629d497ae96e2d0`

## 当前实机差异

用户观察到 Steam 启动环境可能出现：

- GUI 不正确；
- 游戏速度拖慢。

非 Steam 使用相同 DisplayFix 时 GUI 和速度正常。

因此现阶段不能把问题归因于 DisplayFix 的 fixed-Y / auto-X 算法本身。

## 已排除

- 不是 Steam EXE 里 DisplayFix 目标地址整体变化：当前兼容验证的字体、分辨率、JMM、HUD、输入 callsite 在 Steam EXE 中全部仍唯一匹配同一 VA。
- 非 Steam 不需要额外 startup JMM/UI 重播；早期版本没有这些实验也已经正常。

## 当前怀疑方向

优先研究 `ComeOn.dll`：

- Steam API 初始化
- 线程
- 时间函数
- 消息循环
- 窗口/DirectDraw 初始化
- 内存补丁

任何 Steam 修复必须条件化，不能改变非 Steam 稳定路径。

## v0.3-test8a 同步说明

- `v0.3-test8` 实机确认已经修复 test7 的普通地图左键 / Alt+F4 回归，并保留属性、道具两个顶部按钮响应；当前输入实现继续沿用 test8。
- test8 日志显示用户把 `BaseHeight` 改为 1080 后插件仍读取成 480。复核发行 INI 后确认 `[Display]` 原本正好位于 UTF-8 BOM 后的第一行；Win32 `GetPrivateProfile*A` 对 UTF-8 BOM 不可靠，第一节可能读不到，于是 `BaseHeight` 静默使用默认 480。test8a 将 INI 改为 UTF-8 无 BOM，并在 `[Display]` 前增加 ASCII 保护行，使以后编辑器即使重新加入 BOM，也不会再破坏第一节。
- test8a 日志新增 `ConfigPath=`，用于直接确认插件实际读取的是哪一份 `DisplayFix.ini`。
- `build.bat` 已按仓库位置重新整理：同时自动查找 PATH、`LLVM_HOME`、`LLVM_PATH`、`Program Files\LLVM` 和 Visual Studio 自带 LLVM，兼容本地与 Windows GitHub Actions；发行目录仍严格只有 ASI 与 INI。
- 主界面固定 640x480 的等比放大居中仍是后续任务，本轮没有处理。

