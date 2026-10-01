# 源码构建说明

当前统一目标为EDSlash.asi＋EDSlash.toml，静态链接仓库内完整SDL 3.4.16。旧独立Controller、两份INI与外置SDL3.dll构建路径已移除。MOD设置界面后置，未来入口为F12和L3＋R3。用户已确认性能候选1掉帧缓解、功能正常、实机验收通过；本轮重新生成的双产物单独经过结构、同代码与加载验证。

## 环境

Windows、Python 3.11及以上、CMake 3.21及以上、Ninja、完整32位MinGW GCC/G++。工具必须与SDL和主插件同为i686；不会读取个人编译器地址记录、下载依赖或使用64位SDL库。

构建器按EDSLASH_CC、PATH中的i686-w64-mingw32-gcc／gcc、MSYS2_ROOT下mingw32/bin/gcc.exe的顺序查找并验证-dumpmachine。未配置MSYS2_ROOT时探测系统盘常见msys64布局。EDSLASH_CC必须指向gcc，旁边需要g++.exe；CMake／Ninja在同一环境PATH中查找。

## 唯一构建入口

在source目录双击build.bat。命令行不暂停：

~~~bat
build.bat --no-pause
build.bat --no-pause --upx
~~~

在项目根执行：

~~~powershell
python source/tools/build.py
python source/tools/build.py --checks-only
python source/tools/build.py --upx
~~~

默认一次带-g的优化编译，保留完整链接件，副本strip --strip-unneeded后作为发行件。10组CTest、双基线复核、两件PE／入口／重定位／依赖、TOML解析、运行段一致及实际加载通过后才发布。checks-only不更新发布目录。upx使用PATH或UPX_BIN中的本机工具，只从剥离后的发行件生成压缩副本，执行upx -t及非游戏加载／重定位检查；不替换未压缩件，不自动安装UPX。

两件运行代码和优化级别相同；Debug不等于-O0，完整信息仅留在_debug。运行时配置／日志仍叫EDSlash.toml／EDSlash.log，不能同时安装两件。

中间目录固定source/.build/Unified；SDL在其SDL子目录构建，上游源码保持原样。失败保留诊断。无需读取参考资料中的旧SDL ZIP；未携带两份游戏EXE时明确跳过样本复核，源码仍可构建，不能称本次双样本验证通过。

## 产物与配置

- release/unified/EDSlash.asi：发行版，已剥离符号、未压缩。
- release/unified/debug/EDSlash_debug.asi：完整版，保留项目和SDL源码调试信息，不strip、不UPX；同目录有独立配置／许可。
- release/unified/EDSlash.toml：只有缺失时复制默认值，已有新TOML保留。
- release/unified/统一构建验证.json：实际散列、体积、导入、SDL选项及验收边界。
- release/unified/第三方许可.txt：随发布保留的声明。
- --upx时生成release/unified/upx/EDSlash.asi及配套配置／许可。

配置模板source/config/EDSlash.toml由同一生产描述表生成；构建不拿它覆盖玩家已有配置，不删除共享release内的历史产物。项目打包应带docs和thirdparty，不分发游戏EXE。

## 当前验证

10组：输入规则、连续方向、两作宿主战斗／调用约定、拾取提示、Runtime写入／回滚、公共输入阶段、真实后台日志、TOML与原子保存、生产Input＋静态SDL虚拟设备、未知宿主ASI加载。UPX另外验证压缩完整性和加载。

产物必须PE32/i386、DLL、非零入口、唯一InitializeASI导出、重定位可用，不依赖SDL3.dll或外置libgcc／libstdc++／libwinpthread；标准Windows系统DLL允许导入，主目标零导入已不再作为最终契约。

当前静态SDL保留Windows Video公共代码以满足上游DirectInput构建依赖；初始化仅SDL_INIT_GAMEPAD，不初始化VIDEO／AUDIO。音频、GPU／渲染、摄像头、对话／托盘及OpenGL系列关闭。完整选项见CMakeLists.txt与构建报告。

编译／宿主／虚拟设备不能替代真实设备、两作实机和多敌人性能测试。功能、配置、角色selector、时序和性能摘要解释见[统一配置与基础迁移说明](../docs/统一配置与基础迁移说明.md)，独立接档见[完整接档说明](../docs/完整接档说明.md)。

## 工具与许可

自有构建／验证工具都在tools；详细使用、输入输出和限制见[工具说明](../docs/工具说明.md)。旧run_runtime_checks.py和run_notice_checks.py已统一到CTest，不恢复独立构建别名。

配置标量核心改编自Castle Reforge Runtime TOML v1，已纳入本项目，无外部项目路径依赖。SDL完整固定源码及原始许可保留，版权和实际编译器声明见[配置与SDL第三方许可](../docs/配置与SDL第三方许可.txt)。
