# 源码构建说明

> 当前接续（2026-10-01）：生产代码仍为主 ASI（DisplayFix＋QOL）与独立 Controller dev9、两份 INI，迁移尚未实施。用户要求全新迁移为单 ASI＋统一 TOML，沿用 Castle Reforge TOML 实现，不做旧 INI／旧键／旧产物向下兼容；最终玩家功能、默认操作、手感、性能与设置体验保持或改善，不能劣化。推荐沿用主 Runtime 的有效架构，重新设计最终公共服务、配置和统一构建；详见《手柄迁入主插件评估.md》与《完整接档说明.md》最新章节。下文各版本记录及已被替代的方案属于历史。

当前主插件编译 DisplayFix＋QOL，独立 Controller 为 v0.1-dev9；配置仍为两份INI，合并／TOML尚未实施。下方各阶段构建记录保留历史。QOL 与源码审计见 ../docs/自动拾取与地面物品名称说明.md、../docs/源码与文档一致性审计.md。

项目沿用 `source/`、`docs/`、`release/` 三个目录。Controller 是 `src/Modules/Controller/` 子模块，目前独立构建为一份同时包含本体与外传适配的 ASI，尚未并入主插件。

## 主插件

运行本目录 `build.bat`，需要 Python 3 与可运行的 Clang/lld-link。脚本从 PATH、LLVM 常见安装位置和 Visual Studio 查询工具定位编译器。主插件输出 `../release/BladeSwordQOL.asi` 与 `BladeSwordQOL.ini`。

当前主构建保留 release 中其它目标的产物，不再删除整个 release。当前源码已新增 QoL，并扩展 Runtime；这些新增单元已加入主构建的编译/链接列表。

## 独立手柄插件

运行本目录 `build_controller.bat`。自动化调用可以传 `--no-pause`；正常双击在成功和失败时都会停留显示结果。

需要 Python 3 和 **32 位 MinGW GCC**。构建器优先读取 `CONTROLLER_CC`，然后检查 PATH 的 `i686-w64-mingw32-gcc`、`gcc`，最后探测 MSYS2 的 mingw32 默认位置。也可通过 `MSYS2_ROOT` 指定 MSYS2 安装根。必须由 `-dumpmachine` 确认目标为 i686/i386；不能使用 mingw64 GCC。

不读取个人的编译器地址记录，不下载依赖。SDL3 3.4.14 的官方 x86 包须由本地构建者自行放入项目根的 `参考资料/SDL3-3.4.14-win32-x86.zip`。依赖不随Git上传；缺包时构建器明确停止，先检查包的 SHA-256，再提取运行库。许可证见 `../docs/SDL第三方许可.txt`。

构建过程依次执行：

1. 检查双版本地址表与冻结元数据同步。
2. 若本地提供两份参考 EXE，则同时检查散列、签名、输入调用点、投影和地图格换算；缺少成对样本时明确报告跳过，不能称为双样本复核通过。
3. 编译并执行输入时间线、连续角度/力度/位置组合和两版游戏适配回放测试。
4. 编译 Win32/x86 ASI，检查 PE 类型、入口、导出与运行库依赖。
5. 向 `../release/` 更新 `EDSlashController.asi`、`SDL3.dll`，仅在配置不存在时复制 `EDSlashController.ini`，保留已有用户配置。
6. 写出 `../release/手柄构建验证.json`，分别记录静态检查和两作待实机验收状态。

命令行也可直接运行：

```bat
python tools\controller\build_controller.py
```

中间文件保留在 `.build/Controller/`，不会被复制进正式源码包。所有产物验证通过后才更新 release。手柄 ASI 使用系统 Win32 导入，不适用主插件的“Import Directory=0”限制；构建器保证不额外依赖 libgcc 或 libwinpthread DLL。

## 测试与接档

此处保留 dev5 战斗输入衔接阶段记录；当时源码为 `EDSlashController v0.1-dev7`，见下方 dev7 历史版本。dev3 移动保持用户暂定手感；dev4 已有本体方向改善但衔接失败的反馈，dev5 恢复旧历史处理和输入时序，仍待两作实机确认，完整手柄设计尚未全部实现。具体实现范围、缺项与两作测试步骤见 [手柄模块接档说明](../docs/手柄模块接档说明.md) 和 [手柄实机验证说明](../docs/手柄实机验证说明.md)。编译通过、模拟回放通过均不能代替实机验收。

## 当前构建检查记录（2026-10-01）

两份配置模板放在 config/，所有构建和校验入口已同步。主构建包含 DisplayFix、QoL 和 Runtime 桥接/跳转工具，使用 .build/Main；独立手柄使用 .build/Controller，两者保留共享 release 中其它目标的产物和已有玩家 INI。主 build.bat 也支持 --no-pause。

两目标完整编译、链接和发布检查通过，空手柄发布目录默认 INI 复制通过。主 ASI 保持零导入表，独立手柄保持 Win32 系统导入和动态 SDL。新增 Runtime 回滚测试可运行 `python tools/run_runtime_checks.py`，需与手柄相同的 32 位 MinGW GCC，当前 23 项通过。

本体 dev5 已有用户反馈正常；新增 QoL、两插件同场运行、外传及所有持续技能不能据离线通过写成全面实机验收。最新地址、踩坑、测试与下一步见 ../docs/完整接档说明.md 第十二节。

## dev6 新验证与配置

dev6 阶段独立手柄为 v0.1-dev6，新增物理鼠标/手柄交接、原生防御/方向闪避及 [Combat] ChargeGuardOnHit、FreeRunOutsideCombat 两个独立开关。构建自动补缺少的新键，保留已有值；修改后重启游戏。主插件新增 QoL 共用日志。新功能仍需两作实机，具体作用范围及测试见 ../docs/手柄防御闪避与热切换说明.md；LT 右摇杆原生左键菜单最后实施，未纳入当前产物。

## dev7 历史版本

dev7阶段Controller v0.1-dev7，RT技能/RB投掷直接请求、Y独立四套及LT十字切换。FreeRun是全程不扣跑步体力；GuardHitCost和AttackHitRecovery为-1默认、0无变化、正数体力点数，改后重启。升级保留已有配置值。实机待测；闪避保持dev6未调，已失败并进入调查，详见../docs/手柄快捷施放与体力规则说明.md。


dev8发布前源码补充（2026-10-01，历史）：GuardHitCost 与 AttackHitRecovery 的非负配置按角色最大体力百分比解析，范围 0 到 100.00，最多两位小数；12.50 表示 12.50%，最大体力 200 时对应 25 点。-1 保留原周期默认／恢复跟随扣费。每次事件通过角色虚表 +0x80 查询当前最大体力，不按当前剩余体力或原周期倍率计算。本文原 dev7 固定点数描述仅对应已发布历史基线。内部验证包已通过输入 88 项、连续方向 8640 组、双版本宿主 3480 项及 ASI/SDL 加载检查；不代表实机通过，release 仍为验收基线。


当前发布已更新Controller v0.1-dev8与QOL成功拾取提示。百分比为最大体力比例（100.00=全部最大值），新增DirectionalDodge/DodgeDistance，动作图标和LT四键必杀准备/确认已实现；旧dev7说明是历史快照。完整配置、两个ASI散列及实机清单见../docs/手柄第八版与拾取提示验收说明.md。QOL新增工具运行 python tools/qol/run_notice_checks.py，只生成自有测试宿主并只读核对两作签名；不向游戏进程安装Hook。两作新功能均待实机，LB暂假定通过，LT原生菜单后置。


当前发布Controller v0.1-dev9：新增OmnidirectionalGuard全方位格挡开关、动画收尾图标保持和防御诊断；QOL拾取文字下移128像素。新实现和本轮实机反馈见../docs/手柄第九版全方位格挡与显示修复说明.md。source/vendor已移除，SDK压缩包只在忽略的参考资料目录本地保留；旧Git历史含早期压缩包，不作未经授权的历史重写。
