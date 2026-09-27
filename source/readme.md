# 源码构建说明

项目沿用 `source/`、`docs/`、`release/` 三个目录。Controller 是 `src/Modules/Controller/` 子模块，目前独立构建为一份同时包含本体与外传适配的 ASI，尚未并入主插件。

## 主插件

运行本目录 `build.bat`，需要 Python 3 与可运行的 Clang/lld-link。脚本从 PATH、LLVM 常见安装位置和 Visual Studio 查询工具定位编译器。主插件输出 `../release/BladeSwordQOL.asi` 与 `BladeSwordQOL.ini`。

当前主构建保留 release 中其它目标的产物，不再删除整个 release。主插件代码与既有显示修复行为本轮未变。

## 独立手柄插件

运行本目录 `build_controller.bat`。自动化调用可以传 `--no-pause`；正常双击在成功和失败时都会停留显示结果。

需要 Python 3 和 **32 位 MinGW GCC**。构建器优先读取 `CONTROLLER_CC`，然后检查 PATH 的 `i686-w64-mingw32-gcc`、`gcc`，最后探测 MSYS2 的 mingw32 默认位置。也可通过 `MSYS2_ROOT` 指定 MSYS2 安装根。必须由 `-dumpmachine` 确认目标为 i686/i386；不能使用 mingw64 GCC。

不读取个人的编译器地址记录，不下载依赖。SDL3 3.4.14 的官方 x86 包已随源码保存在 `vendor/SDL3/`，构建器先检查包的 SHA-256，再提取运行库。许可证见 `../docs/SDL第三方许可.txt`。

构建过程依次执行：

1. 检查双版本地址表与冻结元数据同步。
2. 若本地提供两份参考 EXE，则同时检查散列、签名、输入调用点、投影和地图格换算；缺少成对样本时明确报告跳过，不能称为双样本复核通过。
3. 编译并执行输入时间线、八方向和两版游戏适配回放测试。
4. 编译 Win32/x86 ASI，检查 PE 类型、入口、导出与运行库依赖。
5. 向 `../release/` 更新 `EDSlashController.asi`、`SDL3.dll`，仅在配置不存在时复制 `EDSlashController.ini`，保留已有用户配置。
6. 写出 `../release/手柄构建验证.json`，分别记录静态检查和两作待实机验收状态。

命令行也可直接运行：

```bat
python tools\controller\build_controller.py
```

中间文件保留在 `_build_controller/`，不会被复制进正式源码包。所有产物验证通过后才更新 release。手柄 ASI 使用系统 Win32 导入，不适用主插件的“Import Directory=0”限制；构建器保证不额外依赖 libgcc 或 libwinpthread DLL。

## 测试与接档

当前为 `EDSlashController v0.1-dev2` 调查与操作修正版。dev1 游戏内操作已收到失败反馈；dev2 的恢复效果仍待两作实机确认，完整手柄设计尚未全部实现。具体实现范围、缺项与两作测试步骤见 [手柄模块接档说明](../docs/手柄模块接档说明.md) 和 [手柄实机验证说明](../docs/手柄实机验证说明.md)。编译通过、模拟回放通过均不能代替实机验收。
