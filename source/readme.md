# 源码构建说明

当前统一目标为EDSlash.asi＋EDSlash.toml；用户自定义技能另存同目录EDSlash.SkillContols.toml，默认状态不创建该文件，静态链接仓库内完整SDL 3.4.16。旧独立Controller、两份INI与外置SDL3.dll构建路径已移除。MOD设置窗口已接通LT＋RT＋Back及物理反引号键，原系统菜单暂停，待实机验收。构建输入摘要标识当前产物；离线回归与非游戏加载单独记录，新产物实机待用户验证，历史反馈见docs/文档/版本与更新记录.md。

## 环境

Windows、Python 3.11及以上、CMake 3.21及以上、Ninja、Visual Studio或Build Tools的桌面C++、x86编译工具和Windows SDK及UPX。主插件和SDL统一构建为Win32/x86，CMake/Ninja本身可为64位；不会读取个人编译器地址记录、下载依赖或使用64位SDL库。

构建器复用x86开发者终端，或通过vswhere自动找到VS/Build Tools并调用vcvarsall x64_x86。可用EDSLASH_VS指定安装根；不会把本机绝对路径写进构建系统。必须安装完整Windows SDK/UCRT；SDK注册缺失时按实际头文件和x86库验证安装目录。CMake／Ninja在该开发环境中查找。

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

默认使用MSVC /O2、/MT、C17和/utf-8，一次优化链接同时生成ASI与EDSlash.pdb；发行副本默认执行upx --best --lzma，调试目录保留未压缩ASI及匹配PDB。先做配置同步、四样本字体安全输出及档案反例回归，再运行29组CTest、四官方EXE复核、两件PE／入口／重定位／依赖、TOML解析、运行段一致及实际加载检查，通过后同步配置并发布。checks-only不更新发布目录。UPX使用PATH或UPX_BIN中的本机工具，正式发行默认压缩，--upx仅保留为兼容参数。执行upx -t、非游戏加载／重定位及解压运行段一致性检查；未找到UPX时停止发布，不自动安装。CI环境也须显式安装UPX并加入PATH。

两件运行代码和优化级别相同；Debug不等于-O0，完整源码符号存放在debug/EDSlash.pdb。运行时配置／日志仍叫EDSlash.toml／EDSlash.log，不能同时安装两件。

中间目录固定.build/msvc；SDL在该目录的SDL子目录构建，上游源码保持原样。失败保留诊断。无需读取参考资料中的旧SDL ZIP；未携带完整四份游戏EXE时明确跳过样本复核，源码仍可构建，不能称本次四样本验证通过。

## 产物与配置

- release/EDSlash.asi：发行版，源码符号独立于ASI存放在PDB中，使用UPX --best --lzma压缩。
- release/debug/EDSlash_debug.asi：完整版，保留项目和SDL源码调试信息，不UPX；同目录有匹配EDSlash.pdb及独立配置／许可。
- release/debug/EDSlash.pdb：与调试ASI的GUID/age匹配，便于源码断点和崩溃定位。
- release/EDSlash.toml：缺失时复制默认值，已有配置只补模板新增段／键，保留原数值、绑定和注释。
- release/统一构建验证.json：实际散列、体积、导入、SDL选项及验收边界。
- release/第三方声明.txt与release/licenses：独立中文声明和原样许可证副本。
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

仓库的`.github/workflows/build.yml`在main推送或手动触发时运行。Windows runner安装Python 3.13、使用runner自带的MSVC/Windows SDK、原生CMake/Ninja及UPX 5.2.1，再调用同一个`source/tools/build.py`；SDL直接使用thirdparty完整源码，不额外下载SDL。只有main成功构建更新dev-auto标签与开发版，其它分支手动构建仅保存Actions附件。构建任务只读仓库，发布任务单独取得contents: write。

自动构建内部仍验证同次优化链接的正式/debug双产物，但不发布debug版。tools/package_release.py输出的EDSlash-dev-auto.zip包含正式ASI、默认TOML、LICENSE.txt、第三方声明.txt与licenses目录。各组件许可证直接复制仓库原件；SunPro单独原样提取源码注释声明。说明不插入许可证，原件编码、换行、空格均保留。

在项目根运行`python source/tools/package_release.py --output release/EDSlash-dev-auto.zip`可生成本地包。须先完成正式构建；ASI、默认配置、源码摘要和许可副本均须匹配。工具逐字节核对许可证，并回读ZIP检查清单、CRC和输入字节；不上传文件。test_package_release.py覆盖缺失许可、仅空白改动、错误产物、个人配置、旧源码、SunPro片段与旧汇总清理。

CI不携带原游戏EXE，真实四样本复核明确跳过，不能记录成通过。Actions成功仍不代表真实设备、Steam DLL或游戏实机验收。首次远程构建及发布状态以Actions运行记录为准。

自有构建／验证工具都在tools；详细使用、输入输出和限制见[工具说明](tools/工具说明.md)。旧run_runtime_checks.py和run_notice_checks.py已统一到CTest，不恢复独立构建别名。

配置标量核心改编自Castle Reforge Runtime TOML v1，已纳入本项目，无外部项目路径依赖。SDL完整固定源码及原始许可保留，版权和实际编译器声明见[第三方声明](../docs/第三方许可/第三方声明.txt)。


Guard设置应用测试使用真实TOML与双版本运动回放；test_menu链接生产Control/Menu/Game和真实虚表包装，检查标题无玩家、独立焦点、页面/来源中立门、业务ABI及逐槽失败回滚。游戏内MOD设置界面已接通原暂停、输入与绘制；真实高亮/动画/热应用和设备时序必须两作实机确认。

默认并行数6，可用`python tools/build.py --jobs 1`诊断环境问题；任何失败都停止交付，不自动重试或复用旧产物。

全项目只保留原生MSVC构建；GCC专用IPA/位置视图参数已经移除。使用.build/msvc隔离编译缓存；源码与SDL共用静态运行库/MT，发行/debug来自同一优化链接，PDB身份必须匹配。

正式构建成功后默认清理根.build；--checks-only或失败保留诊断，--keep-build保留增量缓存。随机核心195611项脚本回归和新名称窗口适配纳入构建，原始证据/正式产物不放缓存。

设置的第五页“关于”显示CMake项目版本、当前源码摘要和作者；版本由PROJECT_VERSION提供，作者在Runtime/RuntimeText.c维护，BuildInfo.h提供统一引用。关于文案在Runtime/RuntimeText.c，只读滚动在SettingsWindow.c，不改变TOML配置格式。

## 开发阶段文本维护

项目自有界面与日志文本编译进ASI，不读取外置语言文件。每个模块的文字集中在对应目录的RuntimeText.c、ControllerText.c、QOLText.c、DisplayFixText.c；配套Text.h只声明常量。开发时编辑.c中的字符串，保留变量名和格式占位符，随后运行build.bat重新编译。变量以模块、具体功能和文本用途命名，不使用流水编号。例如RuntimeText_Config_StickDeadzoneLabel是摇杆死区名称，RuntimeText_Config_StickDeadzoneDescription是其说明，ControllerText_Device_DisconnectedLog是手柄断开日志。新增文字必须用能说明用途的名字；仅修改措辞或翻译时保持变量名。相同用途、相同内容的文本共用定义。

随机姓名词库RandomNameData.h属于功能数据，保持原样；原游戏技能／物品文字仍从游戏读取。配置键、文件路径、导出符号、签名掩码等程序标识不作为可翻译文案。四样本名称在ControllerText.c定义，地址表生成器只引用对应常量。版本号和构建摘要仍由构建系统提供，作者及未指定时的显示文字在RuntimeText.c维护。修改翻译不代表原游戏字体具备对应字形，仍需实机检查显示。

## MSVC调用约定与调试

原游戏thiscall接口在C中通过fastcall空EDX参数桥接：self进入ECX、第二参数NULL进入EDX，其余参数保持原栈布局并由游戏清栈。三处裸Hook使用MSVC x86汇编。不要删去类型或调用中的空EDX参数。默认启用/W4和/WX；局部兼容诊断允许Win32地址/函数指针转换、C聚合初始化以及已有的显式类型收窄。

调试时使用release/debug/EDSlash_debug.asi与同目录EDSlash.pdb；只加载一份ASI。源码符号在PDB中，ASI本身仍有CodeView身份记录，文件名固定为EDSlash.pdb而非本机绝对路径。构建会核对GUID/age，错误或旧PDB停止交付。

手柄键位图为默认首页，用户提供并声明CC0授权的SVG副本在source/assets，已生成像素嵌入ASI。普通构建不需要Qt或Pillow；重新生成图时用generate_controller_art.py，需PySide6与Pillow。图示读取草稿，所以世界改键、菜单AB和连招切换可在保存前查看。

技能菜单选择模式位于按键设置页，对应controller.menu.skill_menu_navigation。left、right、independent分别为左摇杆导航全部、右摇杆导航全部、各自独立导航；默认independent。L3/R3展开侧别不随导航模式变化，手柄键位图即时反映草稿；保存后安全空闲时生效。

技能存储由Runtime/Config.c协调独立主文档和技能文档，Runtime初始化传入已识别GameProfile.game_id。技能文件仅含version和按创建角色selector分组的稀疏技能；缺省槽未设置。普通设置保存不会触碰技能文档。混合保存先完整验证两份候选，每个文件原子替换；主设置失败会回滚已写技能文件并拒绝覆盖外部新改动，不宣称操作系统提供跨文件事务。公开包固定清单排除个人技能文件和备份，构建只更新默认主模板。
