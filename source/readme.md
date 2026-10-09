# 源码构建说明

当前统一目标为EDSlash.asi＋EDSlash.toml，静态链接仓库内完整SDL 3.4.16。旧独立Controller、两份INI与外置SDL3.dll构建路径已移除。MOD设置窗口已接通LT＋RT＋Back及物理反引号键，原系统菜单暂停，待实机验收。构建输入摘要标识当前产物；离线回归与非游戏加载单独记录，新产物实机待用户验证，历史反馈见docs/文档/版本与更新记录.md。

## 环境

Windows、Python 3.11及以上、CMake 3.21及以上、Ninja、完整32位MinGW GCC/G++及UPX。工具必须与SDL和主插件同为i686；不会读取个人编译器地址记录、下载依赖或使用64位SDL库。

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

默认一次带-g的优化编译，保留完整链接件，副本strip --strip-unneeded后默认执行upx --best --lzma，作为正式发行件。先做配置同步、四样本字体安全输出及档案反例回归，再运行29组CTest、四官方EXE复核、两件PE／入口／重定位／依赖、TOML解析、运行段一致及实际加载检查，通过后同步配置并发布。checks-only不更新发布目录。UPX使用PATH或UPX_BIN中的本机工具，正式发行默认压缩，--upx仅保留为兼容参数。执行upx -t、非游戏加载／重定位及解压运行段一致性检查；未找到UPX时停止发布，不自动安装。CI环境也须显式安装UPX并加入PATH。

两件运行代码和优化级别相同；Debug不等于-O0，完整信息仅留在_debug。运行时配置／日志仍叫EDSlash.toml／EDSlash.log，不能同时安装两件。

中间目录固定.build；SDL在其SDL子目录构建，上游源码保持原样。失败保留诊断。无需读取参考资料中的旧SDL ZIP；未携带完整四份游戏EXE时明确跳过样本复核，源码仍可构建，不能称本次四样本验证通过。

## 产物与配置

- release/EDSlash.asi：发行版，已剥离符号，使用UPX --best --lzma压缩。
- release/debug/EDSlash_debug.asi：完整版，保留项目和SDL源码调试信息，不strip、不UPX；同目录有独立配置／许可。
- release/EDSlash.toml：缺失时复制默认值，已有配置只补模板新增段／键，保留原数值、绑定和注释。
- release/统一构建验证.json：实际散列、体积、导入、SDL选项及验收边界。
- release/第三方许可.txt：随发布保留的声明。
- release/upx/EDSlash.asi保留为旧取件路径的同一压缩副本，配套配置／许可同步。

配置模板source/config/EDSlash.toml由同一生产描述表生成。BAT和直接运行build.py都执行相同同步步骤：先验证发行／debug／upx各目录的配置，再只补缺失键；完整旧配置保持原字节，新增键写UTF-8无BOM＋CRLF。损坏、重复键、无法安全补入的表或构建期间外部修改会停止同步，不覆盖原值。构建报告记录补入键。此步骤只处理项目release目录，不部署到游戏目录，不删除历史产物；checks-only不写发布配置。项目打包应带docs和thirdparty，不分发游戏EXE。

## 当前验证

构建另外自动运行配置同步和Controller档案回归。后者只读四份本体/外传Steam/非Steam准确EXE，核对静态地图/句柄表/资格来源并确认故意写错地址会被拒绝；源码包没有完整游戏样本时明确跳过真实样本检查，不下载游戏。

29组（包含共享契约、延后初始化、焦点、跳跃和自有设置窗口回归）：输入规则、连续方向、两作宿主战斗／调用约定、原生菜单/删除确认/软件光标、调查扇区与静态原事件、拾取提示、Runtime写入／回滚、公共输入阶段、真实后台日志及1252／936／UTF-8代码页回放、TOML与原子保存、生产Input＋静态SDL虚拟设备、未知宿主ASI加载。UPX另外验证压缩完整性和加载。

产物必须PE32/i386、DLL、非零入口、唯一InitializeASI导出、重定位可用，不依赖SDL3.dll或外置libgcc／libstdc++／libwinpthread；标准Windows系统DLL允许导入，主目标零导入已不再作为最终契约。

当前静态SDL保留Windows Video公共代码以满足上游DirectInput构建依赖；初始化仅SDL_INIT_GAMEPAD，不初始化VIDEO／AUDIO。音频、GPU／渲染、摄像头、对话／托盘及OpenGL系列关闭。完整选项见CMakeLists.txt与构建报告。

编译／宿主／虚拟设备不能替代真实设备、两作实机和多敌人性能测试。功能、配置、角色selector、时序和性能摘要解释见[统一配置与模组设置说明](../docs/文档/统一配置与模组设置说明.md)，独立接档见[完整接档说明](../docs/文档/完整接档说明.md)。

## 工具与许可

### GitHub Actions自动构建

仓库的`.github/workflows/build.yml`在main推送或手动触发时运行。Windows runner安装Python 3.13、MSYS2的32位MinGW GCC/G++、原生CMake/Ninja及UPX 5.2.1，再调用同一个`source/tools/build.py`；SDL直接使用thirdparty完整源码，不额外下载SDL。只有main成功构建更新dev-auto标签与开发版，其它分支手动构建仅保存Actions附件。构建任务只读仓库，发布任务单独取得contents: write。

自动构建内部仍验证同次优化链接的正式/debug双产物，但**不发布debug版**。`tools/package_release.py`输出的`EDSlash-dev-auto.zip`根目录严格只有四个文件：EDSlash.asi、EDSlash.toml、LICENSE.txt、LICENSE-SDL.txt。项目许可来自根LICENSE；SDL许可文件使用现有第三方许可汇总，保留HIDAPI及静态运行库声明。源码、完整SDL、文档、报告、文件清单和额外校验附件均不进入ZIP或自动发布附件。

在项目根运行`python source/tools/package_release.py --output .workspace/release/EDSlash-dev-auto.zip`可生成同结构本地包。须先完成正式构建；正式件/默认配置/源码摘要与报告必须一致，个人配置不能打包。工具不修改输入或上传文件，回读ZIP检查根目录严格四件、CRC与输入字节；ZIP SHA-256只通过工作流内部输出传给发布任务，不生成额外文件。`python source/tools/test_package_release.py`验证四件清单、debug/源码/文档等排除，以及旧件/个人配置/源码变化/缺许可/输出位置错误拒绝。

CI不携带原游戏EXE，真实四样本复核明确跳过，不能记录成通过。Actions成功仍不代表真实设备、Steam DLL或游戏实机验收。首次远程构建及发布状态以Actions运行记录为准。

自有构建／验证工具都在tools；详细使用、输入输出和限制见[工具说明](tools/工具说明.md)。旧run_runtime_checks.py和run_notice_checks.py已统一到CTest，不恢复独立构建别名。

配置标量核心改编自Castle Reforge Runtime TOML v1，已纳入本项目，无外部项目路径依赖。SDL完整固定源码及原始许可保留，版权和实际编译器声明见[配置与SDL第三方许可](../docs/第三方许可/配置与SDL第三方许可.txt)。


Guard设置应用测试使用真实TOML与双版本运动回放；test_menu链接生产Control/Menu/Game和真实虚表包装，检查标题无玩家、独立焦点、页面/来源中立门、业务ABI及逐槽失败回滚。游戏内MOD设置界面已接通原暂停、输入与绘制；真实高亮/动画/热应用和设备时序必须两作实机确认。

编译器发生随机内部错误时，可用`python tools/build.py --jobs 1`串行复核；默认并行数6，不改变生产O2或发行/debug同一次链接的规则。

GCC16的SDL HID单元使用-fno-ipa-cp-clone，避免编译器丢失被函数表引用的静态包装函数；其余单元和完整上游源码／后端不改。构建成功后清理源码内旧.build及Python字节缓存，当前根.build保留增量产物／诊断。

正式构建成功后默认清理根.build；--checks-only或失败保留诊断，--keep-build保留增量缓存。随机核心195611项脚本回归和新名称窗口适配纳入构建，原始证据/正式产物不放缓存。

设置的第四页“关于”显示CMake项目版本、当前源码摘要和作者；版本由PROJECT_VERSION提供，作者在Runtime/BuildInfo.h维护。关于文案和只读滚动在SettingsWindow.c，不改变TOML配置格式。
