/*
 * DisplayFix.c
 *
 * 《刀剑封魔录外传：上古传说》ComeOn.exe 显示修复 ASI 插件。
 * 当前版本：v0.2.3-stripe1（从 v0.2.1 稳定基线重开：不移动辅助 GUI，不改顶层链；同步绘制层与单次输入 root 优先级）
 *
 * stripe1 是在 layer1d 已实机解决 GUI 绘制/输入层级问题之后，单独处理历史最右侧竖条纹的实验版。
 * 用户实测发现：BaseHeight=480 时，16:9、32:9、8:9、40:9 都会出现最右侧竖条，24:9 不会。
 * 把这些比例代入旧算法后，实际 TargetWidth 分别为 854、1708、428、2134、1280；前四个都不是
 * 8 像素整除，只有 1280 能被 8 整除。因此本实验只改变 TargetWidth 的最终横向对齐规则：
 * 从“先四舍五入、再向上补成偶数”改为“选择最接近理想比例的 8 像素倍数”。
 * 除 TargetWidth 计算外，layer1d 已实机通过的 GUI Draw、输入 root、Strategy、Steam JMM、字体与
 * 外传 Steam 专项兼容路径全部保持不变。这个版本的目的就是验证竖条是否来自游戏内部 8 像素块处理。
 *
 * layer1d 延续从稳定基线重新开出的独立路线。test1~test7 只继承逆向证据、失败经验和文档，
 * 不继承任何“移动物品/装备/技能窗口、补鼠标坐标、改 self+0xA8、手工移动乾坤袋”的运行时代码。
 * layer1a 已由实机证明“辅助菜单应该位于居中主 HUD 上方”的绘制方向正确，但只延迟 Draw 会让视觉层级和
 * 顶层输入 root 优先级不一致：重叠区域能看见按钮，却会先被 HUD 吃掉；独立左侧面板也可能仍画在 HUD 下。
 * layer1b 曾尝试重排 manager+0x1C/+0x18 双向顶层链，但实机日志明确显示其边界假设不成立，运行时直接退回
 * layer1a，因此 layer1d 完全撤销链表写入。现在绘制侧继续使用 HUD 后延迟 Draw，并动态覆盖独立辅助面板；
 * 输入侧只 Hook 原版顶层 picker 的单次返回值：仅当原版选中 HUD 且鼠标命中已延后绘制的菜单 root 时，
 * 才把这一次 root 改成菜单。对象坐标、child、active、业务事件和键盘快捷键仍全部保持原版。
 *
 * v0.2.1 不修改 v0.2.0 已经实机闭环的任何功能逻辑：
 *   - 非 Steam 显示/HUD/DPI/输入与 Strategy 生命周期保持不变；
 *   - Steam ResJM.Lib 多语言兜底保持不变；
 *   - Steam ComeOn.dll CreateWindowExA class-atom OpenGL 兼容修复保持不变；
 *   - SteamAPI、官方 EDIT 处理、全局 CreateWindowExA Hook 都保持不变。
 *
 * 本轮唯一运行时变化是日志文本：等级统一为 [成功]/[信息]/[警告]/[失败]/[运行]，
 * 初始化、分辨率、HUD、输入、Steam/JMM、ResJM、class-atom 诊断字段全部改为简体中文。
 * 关键技术名和配置键名继续保留英文，方便和逆向地址、源码变量及历史接档对应。
 * 日志文件仍然是 UTF-8 无 BOM；构建脚本显式固定 UTF-8 源码/执行字符集。
 *
 *
 * v0.2-test1 与 v0.2-test2 的组合实机结果把问题进一步缩小：
 *   - test1：全局 USER32!CreateWindowExA Hook 仍存在，但 callback 在调用原函数后立即返回，OpenGL 成功；
 *   - test2：只禁止 EDIT 的 SetWindowLongA/WndProc 子类化，其余 callback 逻辑恢复，OpenGL 再次崩溃。
 *
 * 因此“EDIT 自定义 WndProc”已经被排除为唯一根因；真正有问题的是 callback 在 SetWindowLongA 之前执行的部分。
 * 重新逐条反汇编后发现一个非常具体的 Win32 兼容性错误：callback 从 hook-context +0x2C 取出
 * CreateWindowExA 的 lpClassName，然后直接把它当作 char* 解引用并与字符串 "EDIT" 比较。
 *
 * 但 Win32 明确允许 lpClassName 不是字符串指针，而是 MAKEINTATOM(atom)：这时高 16 位为 0，低 16 位
 * 是窗口类 ATOM。原 ComeOn.dll callback 只检查 lpClassName != NULL，没有检查“是不是 ATOM”，因此遇到
 * 这类合法调用时会把例如 0x0000C0xx 当作内存地址读第一个字符，直接触发访问异常。
 * 用户实机日志已经确认该 guard 在 Steam + cnc-ddraw OpenGL 启动过程中命中 3 次，最后一次 atom 为 0xC1F2；
 * 因此这条非法解引用与 OpenGL 崩溃的因果链已经由 A/B + 动态命中闭合。
 *
 * v0.2.0 不禁 EDIT 子类化，也不 neutralize 整个 callback。它只在 callback 真正开始读取 class 名之前
 * 加一层合法 Win32 语义保护：
 *   - lpClassName == NULL：沿原来“无后处理”语义直接收尾；
 *   - 0 < lpClassName < 0x10000：判定为 MAKEINTATOM，直接跳过 ComeOn.dll 的字符串比较/EDIT 后处理；
 *   - lpClassName >= 0x10000：继续执行 ComeOn.dll 原始的 "EDIT" 比较、HWND 记录和 WndProc 子类化。
 *
 * 这样全局 CreateWindowExA Hook、普通字符串类名、SteamAPI、多语言和官方 EDIT 功能都尽量保持原样；
 * 只修 ComeOn.dll 对 CreateWindowExA 合法 ATOM 参数的不安全解引用。
 *
 * 外传 v0.1-test1 已由用户实机确认：非 Steam 版的标题 4:3、进入游戏宽屏、GUI/HUD 居中、
 * 右侧 6 个菜单按钮、高 DPI 字体与返回主菜单恢复 4:3 均正常；Steam 版顺带测试也确认绝大多数功能正常。
 * test2 不改这些已经通过的 Strategy/JMM/HUD/输入机器码路径，只修一个配置初始化顺序问题：
 * 旧版在 Display.Enable=0 时会过早 return，使本应独立的 Font.FixDPI=1 也失效；test2 改为先处理字体，
 * 再决定是否安装宽屏/HUD/输入补丁。
 *
 * ----------------------------------------------------------------------------------------------
 * v0.3 封版沿用 test15 已实机通过的最终方案：进入游戏切宽屏，退出 Strategy 后恢复原游戏真正的标题 640x480 生命周期。
 * ----------------------------------------------------------------------------------------------
 *
 * test14 的实机结果已经把问题进一步拆开：
 *   1. 进入 Strategy state=3 时，临时 force=1 后，live display 已经正确从 640x480 变成 854x480；
 *   2. 游戏内 HUD/JMM 也因此有了正确宽屏 surface，说明“进入游戏”方向已经闭合；
 *   3. 但是离开 Strategy state=3 时，test14 只把代码立即数恢复成 FRONTEND profile，没有真正把已经存在的
 *      854x480/其它宽屏 DirectDraw surface 重建回 640x480；
 *   4. 用户实机因此看到：第一次标题正常 4:3，进入游戏正常宽屏，退出游戏后标题却留在 16:9 surface 上，
 *      同时还残留小地图/技能 UI 等旧游戏内画面。
 *
 * 这说明“恢复前端代码 profile”和“恢复当前 live display”是两回事。test15 不再追求从头到尾统一一个宽屏
 * surface，而是完全顺着原游戏自己的设计：
 *   - 标题/启动动画/前端：真正的 mode 4，也就是原版 640x480；
 *   - Strategy/gameplay：才临时把当前模式映射到 TargetWidth x BaseHeight；
 *   - 离开 Strategy：先让原版 外传 Strategy-exit 做完游戏内清理，再恢复 FRONTEND 代码 profile，最后复用原版
 *     0x0040BCC0(self, mode=4, force=1) 强制重建一次真正的前端 mode 4 surface。
 *
 * 这里的 mode 4 不是新猜的魔法数字。进一步反汇编已经确认原游戏启动前端自己就在 0x0040C339~0x0040C345
 * 明确执行 `push 0 / push 4 / call 0x0040BCC0`；而 0x0040BCC0 的 mode 4 原始分支就是 640x480。
 * 因此 test15 做的是“返回前端时补回原游戏本来就使用的显示模式语义”，不是另造一套菜单缩放规则。
 * ----------------------------------------------------------------------------------------------
 *
 * test13 的实机日志新增了一条决定性证据：
 *   [运行] Strategy进入 状态=3 GAMEPLAY配置=就绪
 *   [运行] Strategy进入 原版应用完成 实际=640x480
 *
 * 这说明 test13 的 Strategy gate 本身已经命中正确时机，但 外传 Strategy-enter 调 0x0040BCC0 时传入 force=0。
 * 原版 0x0040BCC0 会先比较 self+0x04 的“当前 mode ID”和这次请求的 mode ID；二者相同且 force=0 时，
 * 会在真正写入 self+0x228/self+0x22C 新宽高之前直接返回。DisplayFix 虽然已经把 mode 4 对应的立即数改成
 * 854x480，但 mode ID 仍然是 4，所以实际 live display 继续保持 640x480。随后 Steam delayed JMM 却按 854x480
 * 重排 GUI，便出现用户截图里的严重错位；1080 时 center delta 更大，所以几乎整套 HUD 都被移出可见区域。
 *
 * test14 不再增加新的 gameplay 判据。它继续使用 test13 的 Strategy state=3 gate，只在调用原版 外传 Strategy-enter
 * 的极短时间内，把该函数开头的 `push 0` 临时改成 `push 1`，也就是把原版 0x0040BCC0 的 force 参数设为 1。
 * 原函数返回后立刻恢复 `push 0`。这样显示设备重建仍然完整走游戏自己的 外传 Strategy-enter -> 0x0040BCC0 路径，
 * 但不会再因为“mode ID 没变”而早退。
 *
 * 另外 test14 新增 live-size 安全核对：Strategy enter 返回后如果实际宽高仍不等于 TargetWidth/TargetHeight，
 * DisplayFix 会立即撤销 GAMEPLAY profile，禁止 HUD 居中和 Steam JMM 继续在错误 surface 上工作。失败时宁可保留
 * 原生 4:3，也不再产生 test13 那种整套 GUI 错位。
 *
 * test11 和 test12 都已经由用户实机证明失败，但它们留下了非常重要的排除证据：
 *   - test11 用“主 HUD 第一次 +0x58 自动布局”判断已经进入游戏；主菜单也会建立同类 HUD，所以过早切宽屏；
 *   - test12 改用“world 全局对象非空”判断；实机再次证明主菜单阶段 world 对象同样已经存在，因此还是过早切宽屏；
 *   - 两次失败都会在主菜单阶段把 live display 从 640x480 改成 TargetWidth x BaseHeight，随后出现
 *     左上角只有原生菜单、其余大面积黑区和旧 surface 黄色残影。
 *
 * test13 不再猜“某个对象出现了是不是代表 gameplay”，而是直接接到 ComeOn.exe 自己的高层状态机：
 *
 *   0x0040B9C0  状态切换函数
 *       self+0x0C = 当前高层状态
 *
 *   新状态 3：
 *       0x0040BA4B  打印 "BeforeStrategy() Begin"
 *       0x0040BA5F  call 外传 Strategy-enter
 *       0x0040BA64  打印 "BeforeStrategy() End"
 *
 *   离开旧状态 3：
 *       0x0040B9E1  call 0x0040F8B0
 *       0x0040B9E8  call 外传 Strategy-exit
 *       0x0040B9ED  打印 "AfterStrategy()  End"
 *
 * 外传 Strategy-enter 不是我们猜出来的“可能会切分辨率”的函数：它自己读取显示管理器 self+0x280 的游戏设置模式，
 * 然后直接调用原版 0x0040BCC0 重新应用显示模式。因此 test13 只做两件事：
 *   1. 在 0x0040BA5F 调原版 外传 Strategy-enter 之前，把代码立即数切成 GAMEPLAY profile；
 *   2. 在 0x0040B9E8 调原版 外传 Strategy-exit 之前，恢复 FRONTEND profile。
 *
 * 这样真正的 SetDisplayMode 仍然由游戏原来的 Strategy 进入流程自己执行，DisplayFix 不再额外插入一次
 * 0x0040BCC0 Reset，也不再依赖 HUD/world 对象生命周期。前端/主菜单完整保留原版 4:3 逻辑 surface。
 *
 * HUD 与 Steam GUI 路径也同步加一道防线：
 *   - FRONTEND profile 时，主 HUD +0x58 只执行原版布局，不做宽屏平移；
 *   - 只有 GAMEPLAY profile 已经由 Strategy 状态机正式启用后，才执行 v0.2-test1 已实机通过的 HUD 居中；
 *   - Steam/ComeOn.dll 的 test10 delayed full JMM apply 也只允许在 GAMEPLAY profile 中执行；
 *   - 每次重新进入 Strategy 状态 3，会重置 Steam one-shot 状态，保证“返回菜单再开新局”也能重新同步。
 *
 * Steam 特殊路径继续保留 v0.3-test10 已实机通过的修复：
 *   - Steam/ComeOn.dll 环境缺少非 Steam 自然发生的一次完整后续 JMM apply；
 *   - 等 HUD、顶层 UI、资源根都成熟后，one-shot 调用原版 0x004C6970(0,W,H)；
 *   - test10 实机已确认 GUI 最终可恢复正确位置；test13 不改变这个原版 JMM 调用本身，只修正它的触发阶段。
 *
 * 输入修复继续保留 test8 已实机验证的方案：
 *   - 顶部“属性/道具”真实 control ID 是 0x0B / 0x0E；
 *   - 释放阶段在 0x0040CF96 -> 0x004C7930 之后，仅在原版没有切换窗口时补一次原版式 toggle；
 *   - 按下阶段绝不包装 0x004C78C0（test7 已证明它依赖调用者隐藏寄存器状态）；
 *   - 只在真正世界输入 0x0040CFFB -> 0x00482790 的最后 callsite 上，命中 0x0B/0x0E 时跳过
 *     本次角色移动，其他地图点击完全走原版。
 *
 * 关于 BaseHeight 和性能必须特别说明：
 *   - BaseHeight 是“游戏内部逻辑高度 / 世界视野量级”，不是最终显示器输出清晰度；
 *   - BaseHeight=1080 + 16:9 会让老游戏真正运行 1920x1080 的内部世界，看到的地图范围显著增加，
 *     CPU/GPU/对象更新和 DirectDraw surface 成本都会明显上升，所以用户本轮实机看到严重掉帧是可解释的；
 *   - 原项目最初的 fixed-Y 目标仍推荐 BaseHeight=480 或 600，让 cnc-ddraw 负责最终放大到 4K；
 *   - 用户此前已经允许任意正整数 BaseHeight 作为高级 FOV/世界缩放实验，所以 test13 不重新封死上限，
 *     但 BaseHeight>600 会在日志和 INI 中明确标为高级高负载用法，而不是“4K 画质模式”。
 *
 * 其它已经实机确认并继续保留：
 *   - 字体创建路径固定 96 DPI，解决 Windows 125% 等缩放下字体裁切，同时不改变整个进程 DPI；
 *   - TargetWidth 按宽高比自动计算；
 *   - v0.2-test1 的底部主 HUD 根节点水平居中；边缘 UI（小地图/右侧按钮）继续贴边；
 *   - INI 第一行 ASCII 保护注释；正式源码/配置/脚本统一 UTF-8 无 BOM；
 *   - 机器码/上下文签名验证，不用整个 EXE SHA-256 锁死兼容版本。
 *
 * 重要测试状态：
 *   - test10：Steam delayed full JMM apply 稳定基线，已实机通过；
 *   - test11：HUD 存在误判 gameplay，实机失败；
 *   - test12：world 对象存在误判 gameplay，实机失败；
 *   - test13：Strategy gate 时机正确，但原版 Strategy-enter 以 force=0 重应用相同 mode ID，实际 live 仍停在 640x480，实机失败；
 *   - test14：Strategy enter force=1 实机成功，进入游戏 live 已正确变成目标宽高；但退出只恢复代码 profile，
 *             没有把 live surface 强制重建回 640x480，因此返回标题后仍停留宽屏，实机失败；
 *   - test15：保留 test14 的进入路径；离开 Strategy 后强制恢复原版 mode 4 前端 surface，BaseHeight=480 实机通过；
 *             进入日志 live=854x480 / expected=854x480 / force=1，退出日志 live=640x480 / expected=640x480。
 *   - v0.3：不再扩大运行时修改范围，直接以 test15 的实机通过代码封版。
 *   - v0.2.1：仅日志中文化；v0.2.0 的 Steam OpenGL class-atom 修复与全部稳定逻辑不变。
 *
 * 代码里的注释故意写得非常细，目标是让只学过一天编程的人也能顺着看懂每一步。
 */

#include "../../Runtime/Runtime.h"


/* ============================================================================================== */
/* 1. 最基础的 Win32 类型                                                                               */
/* ============================================================================================== */

/*
 * 因为这个文件要在没有 Windows SDK 头文件的情况下也能由 clang 交叉编译，
 * 所以这里只把本插件真正用到的 Win32 类型自己写出来。
 */
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef unsigned long       DWORD;
typedef long                LONG;
typedef int                 BOOL;
typedef unsigned int        UINT;
typedef void*               HANDLE;
typedef void*               HMODULE;
typedef void*               HINSTANCE;
typedef void*               FARPROC;
typedef const char*         LPCSTR;
typedef char*               LPSTR;
typedef void*               LPVOID;
typedef const void*         LPCVOID;
typedef unsigned long*      LPDWORD;
typedef void*               HWND;
typedef DWORD               SIZE_T;

/*
 * Win32 的 POINT 就是两个 32 位有符号整数：x 和 y。
 * GetCursorPos 会把当前鼠标位置写进这个结构。
 */
typedef struct POINT_TAG {
    LONG x;
    LONG y;
} POINT;

/* Win32 的 BOOL 真值通常就是 1。 */
#define TRUE  1
#define FALSE 0

/* DllMain 收到的 reason=1 表示 DLL 正在被加载到进程。 */
#define DLL_PROCESS_ATTACH 1

/* GetSystemMetrics(0) 取得屏幕宽度，GetSystemMetrics(1) 取得屏幕高度。 */
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1

/* VirtualProtect 所需的页面保护常量：允许代码页读、写、执行。 */
#define PAGE_EXECUTE_READWRITE 0x40u

/* CreateFileA 用到的几个常量。 */
#define GENERIC_WRITE         0x40000000u
#define FILE_APPEND_DATA      0x00000004u
#define FILE_SHARE_READ       0x00000001u
#define CREATE_ALWAYS         2u
#define OPEN_ALWAYS           4u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u

/* Win32 用 (HANDLE)-1 表示 CreateFile 失败。 */
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG)-1)

/* ============================================================================================== */
/* 2. ComeOn.exe 已确认的导入表地址                                                                    */
/* ============================================================================================== */

/*
 * ComeOn.exe 是老式 PE32 程序：ImageBase 固定为 0x00400000，并且没有 ASLR 重定位表。
 * 它自己的导入表已经包含下面这些 API。
 *
 * ASI 是在进程已经建立后由加载器 LoadLibrary 进来的，因此这些 IAT 槽位已经被 Windows 填成真正函数地址。
 * 这样我们就不需要给 DisplayFix.asi 自己增加 import table，也不需要额外链接 kernel32.lib/user32.lib。
 *
 * 注意：这里虽然用了固定 IAT 地址，但真正“要修改的游戏代码”仍然全部通过机器码签名搜索定位；
 * IAT 地址属于当前 ComeOn.exe 架构的一部分，现有原版和附件宽屏改版都一致。
 */

typedef HMODULE (__stdcall *FnGetModuleHandleA)(LPCSTR name);
typedef FARPROC (__stdcall *FnGetProcAddress)(HMODULE module, LPCSTR name);
typedef DWORD   (__stdcall *FnGetModuleFileNameA)(HMODULE module, LPSTR buffer, DWORD size);
typedef void    (__stdcall *FnOutputDebugStringA)(LPCSTR text);
typedef HANDLE  (__stdcall *FnCreateFileA)(LPCSTR name, DWORD access, DWORD share, LPVOID security,
                                            DWORD creation, DWORD flags, HANDLE template_file);
typedef BOOL    (__stdcall *FnWriteFile)(HANDLE file, LPCVOID buffer, DWORD bytes_to_write,
                                         LPDWORD bytes_written, LPVOID overlapped);
typedef BOOL    (__stdcall *FnCloseHandle)(HANDLE handle);
typedef HANDLE  (__stdcall *FnGetCurrentProcess)(void);
typedef int     (__stdcall *FnGetSystemMetrics)(int index);
typedef BOOL    (__stdcall *FnGetCursorPos)(POINT* point);

/*
 * “*(函数指针类型*)地址”可以理解成：
 *   1. 先把这个绝对地址当成“装着函数地址的小格子”；
 *   2. 从小格子里取出函数地址；
 *   3. 再按照正确的函数参数格式去调用它。
 */
#define GAME_GetModuleHandleA   (*(FnGetModuleHandleA*)0x0055119Cu)
#define GAME_GetProcAddress     (*(FnGetProcAddress*)0x00551254u)
#define GAME_GetModuleFileNameA (*(FnGetModuleFileNameA*)0x005511A0u)
#define GAME_OutputDebugStringA (*(FnOutputDebugStringA*)0x00551174u)
#define GAME_CreateFileA        (*(FnCreateFileA*)0x005511E4u)
#define GAME_WriteFile          (*(FnWriteFile*)0x005511E0u)
#define GAME_CloseHandle        (*(FnCloseHandle*)0x00551224u)
#define GAME_GetCurrentProcess  (*(FnGetCurrentProcess*)0x005511E8u)
#define GAME_GetSystemMetrics   (*(FnGetSystemMetrics*)0x005513FCu)

/*
 * 0x005283C8 是 ComeOn.exe 自己的 GetCursorPos IAT 槽。
 * 这里必须故意走“游戏自己的 IAT”，而不是直接从 USER32 重新取函数地址。
 * 原因是 cnc-ddraw 之类的兼容层可能会接管这个 IAT，把桌面坐标转换成游戏逻辑坐标。
 * 主 HUD 原版命中检测 0x4B3380 也是走这个槽，所以 DisplayFix 的补偿检测必须和它看到同一套坐标。
 */
#define GAME_GetCursorPos       (*(FnGetCursorPos*)0x005513E4u)

/*
 * VirtualProtect / FlushInstructionCache / GetPrivateProfileXXX 没有全部出现在 ComeOn.exe 的 IAT 中，
 * 但 ComeOn.exe 已经导入了 GetModuleHandleA 和 GetProcAddress。
 * 所以插件启动后可以安全地从已经加载的 kernel32.dll 中查询这些函数。
 */
typedef BOOL  (__stdcall *FnVirtualProtect)(LPVOID address, DWORD size, DWORD new_protect, LPDWORD old_protect);
typedef BOOL  (__stdcall *FnFlushInstructionCache)(HANDLE process, LPCVOID address, DWORD size);
typedef UINT  (__stdcall *FnGetPrivateProfileIntA)(LPCSTR section, LPCSTR key, int default_value, LPCSTR file_name);
typedef DWORD (__stdcall *FnGetPrivateProfileStringA)(LPCSTR section, LPCSTR key, LPCSTR default_value,
                                                       LPSTR output, DWORD output_size, LPCSTR file_name);

/* 这些函数指针在初始化时取得，之后所有补丁代码都通过它们调用系统 API。 */
static FnVirtualProtect            g_VirtualProtect = (FnVirtualProtect)0;
static FnFlushInstructionCache     g_FlushInstructionCache = (FnFlushInstructionCache)0;
static FnGetPrivateProfileIntA     g_GetPrivateProfileIntA = (FnGetPrivateProfileIntA)0;
static FnGetPrivateProfileStringA  g_GetPrivateProfileStringA = (FnGetPrivateProfileStringA)0;

/*
 * Auto 宽高比最好直接调用 USER32 真正导出的 GetSystemMetrics，而不是继续走游戏 IAT。
 * 原因是 cnc-ddraw 或其他兼容层有可能改写游戏自己的 IAT 槽位，让游戏看到“虚拟逻辑分辨率”。
 * 我们要计算的是桌面/输出比例，所以初始化时通过 GetProcAddress 单独保存真实 USER32 入口。
 * 如果查询失败，后面仍会回退到 ComeOn.exe 原有 IAT，不会因此让插件完全失效。
 */
static FnGetSystemMetrics          g_GetSystemMetrics = (FnGetSystemMetrics)0;

/* ============================================================================================== */
/* 3. 插件状态、路径和日志缓冲区                                                                       */
/* ============================================================================================== */

/* DllMain 和某些 ASI Loader 都可能尝试调用 InitializeASI；这个标志保证真正初始化只做一次。 */
static volatile LONG g_initialized = 0;

/* 保存当前 BladeSwordQOL.asi 自己的模块句柄，用来找到同目录的 BladeSwordQOL.ini。 */
static HINSTANCE g_self_module = (HINSTANCE)0;

/* 路径最大给 1024 字节，远大于这类老游戏常见安装路径。 */
static char g_ini_path[1024];
static char g_log_path[1024];

/* 日志使用一个静态小缓冲区，避免依赖 sprintf / C 运行库。 */
static char g_log_buffer[4096];

/* ============================================================================================== */
/* 4. 极简字符串工具                                                                                   */
/* ============================================================================================== */

/* 返回字符串长度；遇到结尾的 '\0' 就停止。 */
static DWORD str_len(const char* text)
{
    DWORD length = 0;

    if (!text) {
        return 0;
    }

    while (text[length] != '\0') {
        ++length;
    }

    return length;
}

/* 把 src 复制到 dst，并保证 dst 最后一定有 '\0'。 */
static void str_copy(char* dst, DWORD dst_size, const char* src)
{
    DWORD i = 0;

    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1 < dst_size) {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

/* 把 src 接到 dst 末尾，同样保证不会越过 dst_size。 */
static void str_append(char* dst, DWORD dst_size, const char* src)
{
    DWORD used;
    DWORD i = 0;

    if (!dst || dst_size == 0 || !src) {
        return;
    }

    used = str_len(dst);

    while (src[i] != '\0' && used + 1 < dst_size) {
        dst[used] = src[i];
        ++used;
        ++i;
    }

    dst[used] = '\0';
}

/* 只处理 ASCII 英文字母的小写转换；读取 INI 的 Auto/auto/AUTO 时足够用了。 */
static char ascii_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch + ('a' - 'A'));
    }

    return ch;
}

/* 不区分大小写比较两个 ASCII 字符串是否完全相同。 */
static BOOL str_equal_icase(const char* a, const char* b)
{
    DWORD i = 0;

    if (!a || !b) {
        return FALSE;
    }

    while (a[i] != '\0' && b[i] != '\0') {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) {
            return FALSE;
        }
        ++i;
    }

    return (a[i] == '\0' && b[i] == '\0') ? TRUE : FALSE;
}

/* 把一个无符号整数追加成十进制文本，例如 854 -> "854"。 */
static void append_uint(char* dst, DWORD dst_size, DWORD value)
{
    char temp[16];
    DWORD count = 0;

    /* 0 是特殊情况，否则下面 while 一次都不会执行。 */
    if (value == 0) {
        str_append(dst, dst_size, "0");
        return;
    }

    /* 先从个位开始倒着拆数字，所以临时数组里的顺序会是反的。 */
    while (value != 0 && count < (DWORD)sizeof(temp)) {
        temp[count] = (char)('0' + (value % 10u));
        value /= 10u;
        ++count;
    }

    /* 再倒着读 temp，就恢复成人类正常看到的数字顺序。 */
    while (count != 0) {
        char one[2];
        --count;
        one[0] = temp[count];
        one[1] = '\0';
        str_append(dst, dst_size, one);
    }
}

/*
 * 把有符号整数追加成十进制文本，例如：
 *   107  -> "107"
 *   -20  -> "-20"
 *
 * HUD 在 5:4 这种比 4:3 更窄的比例下会得到负偏移，所以日志不能只会打印无符号数。
 */
static void append_int(char* dst, DWORD dst_size, LONG value)
{
    DWORD magnitude;

    if (value < 0) {
        /* 先写负号，再把绝对值交给已经写好的无符号函数。 */
        str_append(dst, dst_size, "-");

        /*
         * 本项目的偏移量只有几百像素，不可能碰到 LONG_MIN 的极端溢出问题。
         * 因此这里直接取相反数，代码更容易给初学者理解。
         */
        magnitude = (DWORD)(-value);
    } else {
        magnitude = (DWORD)value;
    }

    append_uint(dst, dst_size, magnitude);
}


/*
 * 把 32 位数追加成固定 8 位十六进制，例如 0x004C3E40。
 * 运行时诊断里对象地址、函数地址用十六进制更容易和反汇编直接对应。
 */
static void append_hex32(char* dst, DWORD dst_size, DWORD value)
{
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    int shift;

    str_append(dst, dst_size, "0x");

    /* 一个 DWORD 有 8 个十六进制半字节，从最高位开始逐个写。 */
    for (shift = 28; shift >= 0; shift -= 4) {
        char one[2];
        one[0] = HEX_DIGITS[(value >> (DWORD)shift) & 0x0Fu];
        one[1] = '\0';
        str_append(dst, dst_size, one);
    }
}

/* ============================================================================================== */
/* 5. 路径和日志                                                                                       */
/* ============================================================================================== */

/*
 * 把完整 DLL 路径的最后一个文件名替换成新的文件名。
 * 例如：
 *   C:\Games\BladeAndSword\DisplayFix.asi
 * 变成：
 *   C:\Games\BladeAndSword\DisplayFix.ini
 */
static void make_sibling_path(const char* module_path, const char* new_name, char* output, DWORD output_size)
{
    DWORD length;
    DWORD cut;

    str_copy(output, output_size, module_path);
    length = str_len(output);
    cut = length;

    /* 从路径末尾向前找最后一个 '\\' 或 '/'。 */
    while (cut > 0) {
        char ch = output[cut - 1];
        if (ch == '\\' || ch == '/') {
            break;
        }
        --cut;
    }

    /* cut 现在位于文件名开头；在这里截断，然后拼上新名字。 */
    if (cut < output_size) {
        output[cut] = '\0';
    }

    str_append(output, output_size, new_name);
}

/* 写一行 UTF-8/ASCII 日志到 DisplayFix.log。这个函数每次都“追加”会比较麻烦，所以本测试版一次构建整份日志后统一写。 */
static void log_line(const char* text)
{
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), text);
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), "\r\n");
}

/* 追加“标签 + 数字”格式的日志。 */
static void log_uint(const char* label, DWORD value)
{
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), label);
    append_uint(g_log_buffer, (DWORD)sizeof(g_log_buffer), value);
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), "\r\n");
}

/* 和 log_uint 相同，只是允许打印负数，主要用于 HUD 水平偏移诊断。 */
static void log_int(const char* label, LONG value)
{
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), label);
    append_int(g_log_buffer, (DWORD)sizeof(g_log_buffer), value);
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), "\r\n");
}

/*
 * 把“标签 + 字符串内容”写进日志。
 * 这个小函数主要用来记录 DisplayFix.ini 的实际读取路径，避免用户改到了别处的同名 INI，
 * 但插件仍然安静地使用默认值，最后只能从 BaseHeight=480 反推配置没有读到。
 */
static void log_text(const char* label, const char* text)
{
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), label);
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), text ? text : "(null)");
    str_append(g_log_buffer, (DWORD)sizeof(g_log_buffer), "\r\n");
}

/* 把内存里的日志一次写到 DisplayFix.log。 */
static void flush_log_file(void)
{
    HANDLE file;
    DWORD written = 0;
    DWORD length = str_len(g_log_buffer);

    /* CREATE_ALWAYS 表示每次启动都重建日志，避免旧测试内容和新测试内容混在一起。 */
    file = GAME_CreateFileA(g_log_path,
                            GENERIC_WRITE,
                            FILE_SHARE_READ,
                            (LPVOID)0,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            (HANDLE)0);

    if (file != INVALID_HANDLE_VALUE) {
        if (length != 0) {
            GAME_WriteFile(file, g_log_buffer, length, &written, (LPVOID)0);
        }
        GAME_CloseHandle(file);
    }

    /* 同时发到调试输出；如果以后用 DebugView，可以直接看到。 */
    GAME_OutputDebugStringA(g_log_buffer);
}


/*
 * 运行时诊断不能再使用上面的“整份 CREATE_ALWAYS 日志”，因为 hook 真正被调用时初始化早已结束。
 * 这个函数每次只追加一行：
 *   - FILE_APPEND_DATA 让 Windows 永远把 WriteFile 放到文件末尾；
 *   - OPEN_ALWAYS 表示日志已经存在就打开，不存在就新建；
 *   - 每条运行时日志都严格限次数，绝不会每帧刷盘。
 *
 * 这次特意加入运行时日志，是因为 v0.3-test2 已经证明：
 * “机器码 hook 安装成功”不等于“游戏运行时真的经过了我们猜的那条路径”。
 */
static void append_runtime_line(const char* text)
{
    HANDLE file;
    DWORD written = 0;
    DWORD length;
    static const char CRLF[] = "\r\n";

    if (!text || text[0] == '\0') {
        return;
    }

    file = GAME_CreateFileA(g_log_path,
                            FILE_APPEND_DATA,
                            FILE_SHARE_READ,
                            (LPVOID)0,
                            OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            (HANDLE)0);

    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    length = str_len(text);
    GAME_WriteFile(file, text, length, &written, (LPVOID)0);
    GAME_WriteFile(file, CRLF, 2u, &written, (LPVOID)0);
    GAME_CloseHandle(file);

    GAME_OutputDebugStringA(text);
    GAME_OutputDebugStringA(CRLF);
}

/* ============================================================================================== */
/* 6. PE32 .text 扫描                                                                                  */
/* ============================================================================================== */

/* 读取小端 16 位整数。x86/Windows 文件本来就是小端，所以这样拼字节即可。 */
static WORD read_u16(const BYTE* p)
{
    return (WORD)((WORD)p[0] | ((WORD)p[1] << 8));
}

/* 读取小端 32 位整数。 */
static DWORD read_u32(const BYTE* p)
{
    return (DWORD)p[0]
         | ((DWORD)p[1] << 8)
         | ((DWORD)p[2] << 16)
         | ((DWORD)p[3] << 24);
}

/* 把 32 位整数按小端顺序写回内存。 */
static void write_u32_raw(BYTE* p, DWORD value)
{
    p[0] = (BYTE)(value & 0xFFu);
    p[1] = (BYTE)((value >> 8) & 0xFFu);
    p[2] = (BYTE)((value >> 16) & 0xFFu);
    p[3] = (BYTE)((value >> 24) & 0xFFu);
}

/* 描述主 EXE 的代码区。 */
typedef struct TextRegion {
    BYTE* start;
    DWORD size;
} TextRegion;

/*
 * 从内存里的 PE 头找到 .text 区域。
 * 这样扫描范围来自当前真正加载的 EXE，不把 0x401000/长度完全写死。
 */
static BOOL get_main_text_region(TextRegion* region)
{
    BYTE* image = (BYTE*)0x00400000u;
    DWORD pe_offset;
    BYTE* pe;
    WORD section_count;
    WORD optional_size;
    BYTE* section;
    WORD i;

    if (!region) {
        return FALSE;
    }

    /* 正常 PE 文件开头必须是字符 MZ，也就是十六进制 4D 5A。 */
    if (image[0] != 'M' || image[1] != 'Z') {
        return FALSE;
    }

    /* DOS 头 0x3C 保存 PE Header 相对文件/映像起点的偏移。 */
    pe_offset = read_u32(image + 0x3Cu);
    pe = image + pe_offset;

    /* PE Header 必须是 "PE\0\0"。 */
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0) {
        return FALSE;
    }

    /* COFF File Header 中 +2 是节数量，+16 是 Optional Header 大小。 */
    section_count = read_u16(pe + 6u);
    optional_size = read_u16(pe + 20u);

    /* Section Table 紧跟在 4 字节 PE Signature + 20 字节 COFF Header + Optional Header 后面。 */
    section = pe + 24u + optional_size;

    for (i = 0; i < section_count; ++i) {
        BYTE* one = section + (DWORD)i * 40u;

        /* 节名固定 8 字节；这里只需要识别“.text”。 */
        if (one[0] == '.' && one[1] == 't' && one[2] == 'e' && one[3] == 'x' && one[4] == 't') {
            DWORD virtual_size = read_u32(one + 8u);
            DWORD virtual_address = read_u32(one + 12u);

            region->start = image + virtual_address;
            region->size = virtual_size;
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * 带 ? 通配符的字节签名匹配：
 *   mask 字符 'x' 表示这一字节必须相同；
 *   mask 字符 '?' 表示这一字节可以是任何值。
 */
static BOOL match_pattern(const BYTE* data, const BYTE* pattern, const char* mask)
{
    DWORD i = 0;

    while (mask[i] != '\0') {
        if (mask[i] == 'x' && data[i] != pattern[i]) {
            return FALSE;
        }
        ++i;
    }

    return TRUE;
}

/*
 * 在 .text 中搜索一个模式，并要求它“恰好出现一次”。
 * 多于一次说明不确定该改哪一个；一次都没有说明版本结构不符合当前证据。
 */
static BYTE* find_unique_pattern(const TextRegion* region, const BYTE* pattern, const char* mask, DWORD pattern_size)
{
    DWORD offset;
    DWORD count = 0;
    BYTE* found = (BYTE*)0;

    if (!region || !region->start || region->size < pattern_size) {
        return (BYTE*)0;
    }

    for (offset = 0; offset + pattern_size <= region->size; ++offset) {
        BYTE* candidate = region->start + offset;

        if (match_pattern(candidate, pattern, mask)) {
            ++count;
            found = candidate;

            /* 一旦第二次命中，就已经不再是“唯一”，没必要继续浪费时间扫描。 */
            if (count > 1) {
                return (BYTE*)0;
            }
        }
    }

    return (count == 1) ? found : (BYTE*)0;
}

/* ============================================================================================== */
/* 7. 安全写代码页                                                                                     */
/* ============================================================================================== */

/*
 * 将一段机器码临时改成可写，写入新内容，再恢复原页面保护。
 * 这是比“直接向 .text 写字节”安全得多的标准做法。
 */
static BOOL patch_bytes(BYTE* address, const BYTE* replacement, DWORD size)
{
    DWORD old_protect = 0;
    DWORD ignored = 0;
    DWORD i;

    if (!address || !replacement || size == 0 || !g_VirtualProtect) {
        return FALSE;
    }

    if (!g_VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return FALSE;
    }

    for (i = 0; i < size; ++i) {
        address[i] = replacement[i];
    }

    /* 恢复原来保护属性；即使恢复失败，写入本身已经完成，但这里仍返回失败提醒用户。 */
    if (!g_VirtualProtect(address, size, old_protect, &ignored)) {
        return FALSE;
    }

    /* 修改机器码后刷新指令缓存，避免 CPU 极端情况下继续执行旧缓存。 */
    if (g_FlushInstructionCache) {
        g_FlushInstructionCache(GAME_GetCurrentProcess(), address, size);
    }

    return TRUE;
}

/* 方便写一个小端 DWORD：先在普通数组里准备 4 字节，再复用 patch_bytes。 */
static BOOL patch_u32(BYTE* address, DWORD value)
{
    BYTE bytes[4];

    write_u32_raw(bytes, value);
    return patch_bytes(address, bytes, 4u);
}

/* ============================================================================================== */
/* 7A. Steam ComeOn.dll CreateWindowExA class-atom 兼容修复（v0.2.0 正式封版）                              */
/* ============================================================================================== */

/*
 * 这是 v0.2.0 Steam/OpenGL 兼容修复最重要的两个回跳地址。
 *
 * ComeOn.dll 的 CreateWindowExA callback 在 RVA 0x2830 开始。调用完 Hook 引擎保存的原始
 * CreateWindowExA 后，RVA 0x2841 起会处理 lpClassName：
 *
 *   0x2841  mov ecx,[esi+2C]    ; ecx = lpClassName
 *   0x2844  add esp,8
 *   0x2847  mov [esi+20],eax    ; 保存刚创建出的 HWND
 *   0x284A  test ecx,ecx
 *   0x284C  je 0x28A1
 *   0x284E  mov edx,"EDIT"
 *   0x2854  mov bl,[ecx]        ; 这里直接解引用 lpClassName
 *
 * Win32 允许 lpClassName 是 MAKEINTATOM(atom)，此时数值位于 0x00000001~0x0000FFFF，
 * 根本不是可读字符串地址。Steam ComeOn.dll 只检查 NULL，却没有检查 class atom。
 *
 * 我们把 0x2841 的前 6 字节改成 JMP 到下面这个 ASI 内 trampoline；trampoline 会完整重放被覆盖的
 * `mov ecx,[esi+2C] / add esp,8 / mov [esi+20],eax`，然后只多做一个 `< 0x10000` 判断：
 *   - NULL 或 class atom：直接跳到 0x28A1，完全等价于“这个窗口不是 EDIT，不做 ComeOn 后处理”；
 *   - 普通字符串指针：跳回 0x284E，后面的官方 "EDIT" 比较和 SetWindowLongA 全部照旧。
 *
 * 这种做法比 test1 的“整个 callback 提前 return”窄得多，也比 test2 的“禁用 EDIT WndProc”更符合
 * CreateWindowExA API 本身的合法参数语义。
 */
static BYTE* g_steam_createwindow_string_continue = (BYTE*)0;
static BYTE* g_steam_createwindow_callback_end = (BYTE*)0;
static volatile DWORD g_steam_class_atom_bypass_count = 0u;
static volatile DWORD g_steam_last_class_atom = 0u;
static BOOL g_steam_class_atom_runtime_logged = FALSE;

/*
 * 这是一个纯 x86 小跳板，所以使用 __declspec(naked)：编译器不会自动生成 push ebp / mov ebp,esp 等
 * 函数序言，也不会改变 ComeOn.dll callback 原来的栈布局。
 *
 * 对初学者来说可以把它理解成“我们临时把游戏代码拐进来检查一下 class 参数，然后再送回原来的路”。
 * 这里绝不能调用普通 C 函数、写日志或分配内存，因为 CreateWindowExA 本身可能在系统/渲染器初始化的
 * 很早阶段调用；额外 Win32 调用可能递归创建窗口，反而制造新的时序问题。
 */
__declspec(naked) static void steam_createwindow_class_atom_guard(void)
{
    __asm {
        /* 重放被我们 6 字节 JMP 覆盖的第一条原指令：从 Hook context 取 lpClassName。 */
        mov ecx, dword ptr [esi+2Ch]

        /* 重放原 callback 对 original-forward helper 两个参数的栈清理。 */
        add esp, 8

        /* 原版随后会把真正 CreateWindowExA 的返回值 HWND 保存到 context+0x20；这里不能漏掉。 */
        mov dword ptr [esi+20h], eax

        /* NULL 原本就会直接跳到 callback 收尾；保持完全相同的行为。 */
        test ecx, ecx
        jz class_is_not_string

        /*
         * MAKEINTATOM 的定义就是“高 16 位为 0，低 16 位保存 atom”。
         * 在 32 位进程里等价于无符号值 < 0x10000。
         */
        cmp ecx, 10000h
        jae class_is_string

        /*
         * 命中合法 class atom。只记录两个普通 DWORD，不做任何 Win32/API 调用。
         * 等游戏真正进入 Strategy 后，普通 C 代码再把计数写进日志，避免这里递归。
         */
        inc dword ptr [g_steam_class_atom_bypass_count]
        mov dword ptr [g_steam_last_class_atom], ecx

class_is_not_string:
        /* 跳到 ComeOn.dll callback 自己的 pop esi / pop ebp / ret 8 收尾。 */
        jmp dword ptr [g_steam_createwindow_callback_end]

class_is_string:
        /* 普通字符串类名完全回到官方 0x284E，从 "EDIT" 比较开始继续。 */
        jmp dword ptr [g_steam_createwindow_string_continue]
    }
}

/*
 * 安装 class-atom guard。
 * 返回 TRUE 表示机器码结构与 Steam 2.01 样本完全匹配并且 JMP 已成功写入。
 * 该修复已经由用户实机完成闭环：test2 只禁 EDIT WndProc 仍崩，test3 只增加 atom guard 后成功；
 * 进入游戏后的日志又实际记录到 3 次 class atom 命中，因此 v0.2.0 将其作为正式兼容修复保留。
 */
static BOOL install_steam_createwindow_class_atom_guard(HMODULE steam_module)
{
    BYTE* steam_base;
    BYTE* callback_head;
    BYTE* patch_site;
    BYTE replacement[6];
    DWORD displacement;
    DWORD i;
    static const BYTE expected_callback_head[17] = {
        0x55,0x8B,0xEC,0x8B,0x45,0x08,0x56,0x8B,0x75,0x0C,0x56,0x50,0xE8,0xFF,0xEC,0xFF,0xFF
    };
    static const BYTE expected_patch_site[6] = {
        0x8B,0x4E,0x2C,             /* mov ecx,[esi+2C] */
        0x83,0xC4,0x08              /* add esp,8        */
    };

    if (!steam_module) {
        return FALSE;
    }

    steam_base = (BYTE*)steam_module;
    callback_head = steam_base + 0x2830u;
    patch_site = steam_base + 0x2841u;

    /* 先确认整个 callback 函数头仍然是我们已经逆向闭合的 Steam 2.01 版本。 */
    for (i = 0u; i < (DWORD)sizeof(expected_callback_head); ++i) {
        if (callback_head[i] != expected_callback_head[i]) {
            return FALSE;
        }
    }

    /*
     * 如果这里已经是跳向我们的 guard，说明 InitializeASI/DllMain 重复进入；直接视为成功。
     * 不能再次根据已经改过的机器码计算第二层 trampoline。
     */
    if (patch_site[0] == 0xE9u) {
        LONG existing_displacement = (LONG)read_u32(patch_site + 1u);
        BYTE* existing_target = patch_site + 5 + existing_displacement;

        if (existing_target == (BYTE*)&steam_createwindow_class_atom_guard) {
            return TRUE;
        }
        return FALSE;
    }

    /* 机器码不完全匹配就不写，避免误伤其它 ComeOn.dll 版本。 */
    for (i = 0u; i < (DWORD)sizeof(expected_patch_site); ++i) {
        if (patch_site[i] != expected_patch_site[i]) {
            return FALSE;
        }
    }

    /*
     * 继续地址和收尾地址都是 ComeOn.dll 内 RVA，因此用当前实际模块基址计算，天然兼容 ASLR。
     * 0x284E = 官方开始比较 "EDIT" 的位置；0x28A1 = 官方 callback 统一收尾。
     */
    g_steam_createwindow_string_continue = steam_base + 0x284Eu;
    g_steam_createwindow_callback_end = steam_base + 0x28A1u;

    /*
     * 构造标准 x86 `E9 rel32`，第 6 字节用 NOP 填掉，因为原来两条指令一共正好 6 字节。
     * 32 位进程里 rel32 可以覆盖整个 4GB 虚拟地址空间的模块间跳转。
     */
    replacement[0] = 0xE9u;
    displacement = (DWORD)((BYTE*)&steam_createwindow_class_atom_guard - (patch_site + 5u));
    write_u32_raw(replacement + 1u, displacement);
    replacement[5] = 0x90u;

    return patch_bytes(patch_site, replacement, (DWORD)sizeof(replacement));
}

/*
 * 读取 E8 rel32 / E9 rel32 这类“相对地址指令”的真正目标地址。
 * x86 的 rel32 保存的是“目标 - 下一条指令地址”，所以要把 4 字节有符号位移加回 call+5。
 */
static BYTE* decode_rel32_target(BYTE* instruction)
{
    LONG displacement;

    if (!instruction) {
        return (BYTE*)0;
    }

    displacement = (LONG)read_u32(instruction + 1u);
    return instruction + 5 + displacement;
}

/*
 * 把一条已经确认是 E8 CALL 的目标改成新的函数。
 * 这里只改后面的 4 字节 rel32，不碰 opcode 本身；写入仍经过 VirtualProtect + FlushInstructionCache。
 */
static BOOL patch_rel32_call(BYTE* call_instruction, LPVOID new_target)
{
    DWORD displacement;

    if (!call_instruction || !new_target || call_instruction[0] != 0xE8) {
        return FALSE;
    }

    displacement = (DWORD)((BYTE*)new_target - (call_instruction + 5));
    return patch_u32(call_instruction + 1u, displacement);
}

/* ============================================================================================== */
/* 8. 高 DPI 字体修复                                                                                   */
/* ============================================================================================== */

/*
 * 原游戏字体路径中的完整稳定上下文。
 * 真正要替换的是中间这 6 字节：FF 15 5C 80 52 00，也就是 call [GetDeviceCaps]。
 */
static const BYTE FONT_ORIGINAL_PATTERN[] = {
    0x6A,0x48, 0x6A,0x5A, 0x57, 0x8B,0xF0,
    0xFF,0x15,0x54,0x10,0x55,0x00,
    0x50, 0x6A,0x01, 0x8B,0xCE
};

/* 修复后上下文，用来识别“这个 EXE 本来就已经修过”。 */
static const BYTE FONT_PATCHED_PATTERN[] = {
    0x6A,0x48, 0x6A,0x5A, 0x57, 0x8B,0xF0,
    0x83,0xC4,0x08,0x6A,0x60,0x58,
    0x50, 0x6A,0x01, 0x8B,0xCE
};

/* 原 CALL 的等长 6 字节替换：add esp,8 / push 96 / pop eax。 */
static const BYTE FONT_REPLACEMENT[6] = { 0x83,0xC4,0x08,0x6A,0x60,0x58 };

/* 返回：1=本次成功修复，2=原来已经修复，0=没有安全定位。 */
static int apply_font_dpi_fix(const TextRegion* region)
{
    const char* exact_mask = "xxxxxxxxxxxxxxxxxx";
    BYTE* original;
    BYTE* patched;

    original = find_unique_pattern(region,
                                   FONT_ORIGINAL_PATTERN,
                                   exact_mask,
                                   (DWORD)sizeof(FONT_ORIGINAL_PATTERN));

    patched = find_unique_pattern(region,
                                  FONT_PATCHED_PATTERN,
                                  exact_mask,
                                  (DWORD)sizeof(FONT_PATCHED_PATTERN));

    /* 原始版和修复版不应该同时各出现一次；出现这种模糊状态就拒绝。 */
    if (original && patched) {
        return 0;
    }

    if (patched) {
        return 2;
    }

    if (!original) {
        return 0;
    }

    /* 上下文开头 +7 正好落在原 6 字节 CALL 上。 */
    if (!patch_bytes(original + 7u, FONT_REPLACEMENT, 6u)) {
        return 0;
    }

    return 1;
}

/* ============================================================================================== */
/* 9. 固定 Y / 自动 X 的逻辑分辨率 + 底部主 HUD 居中                                                */
/* ============================================================================================== */

/*
 * 先说明本版相对 v0.1-test1 最大的架构修正。
 *
 * 原游戏设置菜单里，玩家正常能选到的只有：
 *   - 模式 4 -> 640x480
 *   - 模式 5 -> 800x600
 *
 * 反汇编里还存在：
 *   - 模式 6 -> 1024x768
 *
 * 但这个 1024x768 是内部/隐藏分支，不等于“菜单里真的有一个 1024x768 选项”。
 * 外部宽屏 EXE 正好利用过这个分支，所以它对逆向很有价值；但插件不能要求用户去选一个不存在的菜单项。
 *
 * 因此 v0.2-test1 的做法是：
 *   1. 先算出唯一的目标逻辑分辨率，例如 854x480；
 *   2. 把 640、800、隐藏 1024 三个分支都改成同一个 854x480；
 *   3. 再同步两处分辨率映射表中的“扩展分支”。
 *
 * 这样无论游戏当前保存的是 640 模式、800 模式，还是某个旧宽屏补丁留下的内部模式，
 * 最终都会落到同一个 DisplayFix 目标，不再需要用户配合选择某个特定分辨率槽位。
 */

/*
 * 0x404D7A 一带的完整分辨率派发。
 *
 * 固定部分包含 800x600 和 640x480，因此能确认我们命中的确是 ComeOn.exe 的显示模式派发；
 * 中间内部/隐藏分支的宽高允许是任意值，所以原版 1024x768、已有 854x480、1920x1080 改版都能匹配。
 *
 * 下面几个立即数相对签名开头的偏移是：
 *   +17 = 内部/隐藏分支 Width
 *   +27 = 内部/隐藏分支 Height
 *   +39 = 原 800 分支 Width
 *   +49 = 原 600 分支 Height
 *   +61 = 原 640 分支 Width
 *   +71 = 原 480 分支 Height
 */
static const BYTE RES_MODE_ALL_PATTERN[] = {
    0x83,0xE8,0x04, 0x74,0x32, 0x48, 0x74,0x19, 0x48, 0x75,0x2C,

    /* 内部/隐藏分支：宽高立即数是通配。 */
    0xC7,0x86,0x28,0x02,0x00,0x00, 0,0,0,0,
    0xC7,0x86,0x2C,0x02,0x00,0x00, 0,0,0,0,
    0xEB,0x2A,

    /* 原生 800x600 分支：作为稳定锚点。 */
    0xC7,0x86,0x28,0x02,0x00,0x00, 0x20,0x03,0x00,0x00,
    0xC7,0x86,0x2C,0x02,0x00,0x00, 0x58,0x02,0x00,0x00,
    0xEB,0x14,

    /* 原生 640x480 分支：同样作为稳定锚点。 */
    0xC7,0x86,0x28,0x02,0x00,0x00, 0x80,0x02,0x00,0x00,
    0xC7,0x86,0x2C,0x02,0x00,0x00, 0xE0,0x01,0x00,0x00,

    /* 再带上后续两个 push 0，减少误匹配概率。 */
    0x6A,0x00, 0x6A,0x00
};
static const char RES_MODE_ALL_MASK[] =
    "xxxxxxxxxxxxxxxxx????xxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

/*
 * 0x470C3E 一带：GetSystemMetrics 后把当前逻辑宽度映射成内部 Width/Height。
 *
 * 这里故意从“cmp eax,640”开始，而不是只从后面的 800/隐藏分支开始。
 * 原因是 DisplayFix 支持任意比例，某些比例计算出来的目标宽度可能刚好等于 640 或 800：
 *
 *   BaseHeight=480 + 5:3 -> 800x480
 *   BaseHeight=600 + 极窄比例 -> 可能得到 640x600
 *
 * 如果只把原来的 1024 比较改成目标宽度，那么 target_width 恰好等于 800 时，
 * CPU 会先命中游戏原生的 800x600 分支，结果 Y 又被偷偷改回 600，违背“固定 Y”的设计。
 *
 * 所以本版把 640、800、隐藏三条比较都纳入同一个签名：
 *   - 普通目标（例如 854）仍然只走隐藏/扩展分支；
 *   - 如果目标宽度和 640/800 撞值，再把冲突的原生比较临时改成不可能的哨兵值，
 *     强制它继续走隐藏/扩展分支；
 *   - 只有真正的原生 640x480、800x600 组合保留原分支，其中 640 模式的 431 特殊高度也不乱动。
 */
static const BYTE RES_MAP1_PATTERN[] = {
    /* cmp eax,640 / je 原 640 分支 */
    0x3D,0x80,0x02,0x00,0x00, 0x74,0x23,

    /* cmp eax,800 / je 原 800 分支 */
    0x3D,0x20,0x03,0x00,0x00, 0x74,0x10,

    /* cmp eax,<内部/扩展宽度>；宽度立即数通配 */
    0x3D,0,0,0,0, 0x75,0x1F,

    /* 隐藏/扩展分支：esi=eax，也就是 Width 直接取目标宽度。 */
    0x8B,0xF0,

    /* mov edi,<内部/扩展高度>；高度立即数通配 */
    0xBF,0,0,0,0, 0xEB,0x16,

    /* 原 800x600 分支，作为稳定上下文锚点。 */
    0xBE,0x20,0x03,0x00,0x00,
    0xBF,0x58,0x02,0x00,0x00,
    0xEB,0x0A,

    /* 原 640x431 分支，431 是已确认存在但语义尚未完全闭合的特殊路径。 */
    0xBE,0x80,0x02,0x00,0x00,
    0xBF,0xAF,0x01,0x00,0x00
};
static const char RES_MAP1_MASK[] =
    "xxxxxxxxxxxxxxx????xxxxx????xxxxxxxxxxxxxxxxxxxxxxxx";

/*
 * 0x47B0AB 一带：另一处分辨率派发，最后把 Width/Height 压栈交给 0x478D50。
 *
 * 和上面的 RES_MAP1 一样，这里也把 640/800/隐藏三条比较全部纳入签名，
 * 这样才能在目标宽度恰好等于 640 或 800 时安全避开错误的原生分支。
 */
static const BYTE RES_MAP2_PATTERN[] = {
    /* cmp eax,640 */
    0x3D,0x80,0x02,0x00,0x00, 0x74,0x26,

    /* cmp eax,800 */
    0x3D,0x20,0x03,0x00,0x00, 0x74,0x07,

    /* cmp eax,<内部/扩展宽度> */
    0x3D,0,0,0,0, 0x74,0x0C,

    /* 原 800x600 分支。 */
    0x68,0x58,0x02,0x00,0x00,
    0x68,0x20,0x03,0x00,0x00,
    0xEB,0x16,

    /* 隐藏/扩展分支：Height / Width 都是通配，运行时改成 DisplayFix 目标。 */
    0x68,0,0,0,0,
    0x68,0,0,0,0,
    0xEB,0x0A,

    /* 原 640x431 分支。 */
    0x68,0xAF,0x01,0x00,0x00,
    0x68,0x80,0x02,0x00,0x00
};
static const char RES_MAP2_MASK[] =
    "xxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx????x????xxxxxxxxxxxx";

/*
 * 0x4B363B 一带是 JMM 界面布局文件选择器。
 *
 * 原游戏会按“当前逻辑宽度”选择两套布局数据：
 *   640  -> JMMDL.txt
 *   800/1024 -> JMMDL800.txt
 *
 * 非原生宽度（例如 854）原本三个比较都不命中，所以它不会在这里重新选择布局文件。
 * 外部宽屏 EXE 之所以还能显示，是因为此前已经有一套布局留在管理器里；
 * 但 ASI 如果从游戏启动最早阶段就把 640/800 分支都改成 854，就不能依赖这种“碰巧先加载过”的状态。
 *
 * 本版因此把目标宽度显式接入正确的原生布局：
 *   BaseHeight < 600 -> 目标宽度按 640 路径加载 JMMDL.txt；
 *   BaseHeight >= 600 -> 目标宽度按 800 路径加载 JMMDL800.txt。
 *
 * 这样固定 Y 的含义不仅体现在世界分辨率，还保持对应原生高度的 GUI 数据基线。
 */
static const BYTE JMM_LAYOUT_SELECT_PATTERN[] = {
    /* cmp ebp,640 */
    0x81,0xFD,0x80,0x02,0x00,0x00,
    0xF3,0xA4,
    0x75,0x0B,
    0x8D,0x54,0x24,0x10,

    /* mov edi, JMMDL.txt 字符串地址；绝对地址设为通配。 */
    0xBF,0,0,0,0,
    0xEB,0x19,

    /* cmp ebp,800 */
    0x81,0xFD,0x20,0x03,0x00,0x00,
    0x74,0x08,

    /* cmp ebp,1024 */
    0x81,0xFD,0x00,0x04,0x00,0x00,
    0x75,0x44
};
static const char JMM_LAYOUT_SELECT_MASK[] = "xxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx";

/*
 * test13：0x0040B9C0 是游戏自己的高层状态切换函数。
 *
 * 这个长签名从函数头一路覆盖到“离开旧状态 3”的两次清理 call：
 *   0x0040B9E1 -> 0x0040F8B0
 *   0x0040B9E8 -> 外传 Strategy-exit
 * 后面的两个字符串地址分别落在 AfterStrategy 日志附近，是很强的语义锚点。
 *
 * E8 的 rel32 和 jump-table 绝对地址不作为固定版本地址使用：安装时会重新解码目标函数，
 * 并逐字验证 外传 Strategy-enter / 外传 Strategy-exit 的函数头结构。这样历史宽屏 EXE 只要状态机结构未变就能兼容。
 */
static const BYTE STRATEGY_EXIT_CALLSITE_PATTERN[] = {
    /* 旧 state=3 的 AfterStrategy 分支：先做一段 Strategy 清理，再调用真正的离开包装函数。 */
    0x8B,0xCE,
    0xE8,0,0,0,0,
    0x8B,0xCE,
    0xE8,0,0,0,0,
    0x68,0,0,0,0,
    0x68,0,0,0,0,
    0xE8,0,0,0,0,
    0x83,0xC4,0x08,
    0xEB,0x19
};
static const char STRATEGY_EXIT_CALLSITE_MASK[] =
    "xxx????xxx????x????x????x????xxxxx";

/*
 * 外传进入 state=3 的 BeforeStrategy 分支和本传相比多了两次初始化调用，但核心时序相同：
 *   先做准备 -> 打印 BeforeStrategy() Begin -> 调用真正的 Strategy-enter -> 打印 End。
 * 我们只替换“Begin 之后”的那一条 E8 CALL，绝不改状态机 jump table。
 */
static const BYTE STRATEGY_ENTER_CALLSITE_PATTERN[] = {
    0x8B,0xCE,
    0xE8,0,0,0,0,
    0x68,0,0,0,0,
    0x68,0,0,0,0,
    0xE8,0,0,0,0,
    0x83,0xC4,0x08,
    0x8B,0xCE,
    0xE8,0,0,0,0,
    0x68,0,0,0,0,
    0x68,0,0,0,0,
    0xE8,0,0,0,0,
    0x83,0xC4,0x08
};
static const char STRATEGY_ENTER_CALLSITE_MASK[] =
    "xxx????x????x????x????xxxxxx????x????x????x????xxx";

/*
 * 0x004060D6 一带是“UI 按下分派已经明确返回 0，接下来准备把同一次按下交给游戏世界”的路径。
 *
 * 原版机器码顺序已经闭合：
 *   0x0040CFDD  CALL 0x004C78C0    ; UI manager 按下分派
 *   0x004060D2  test eax,eax
 *   0x004060D4  jne  0x004060F0    ; UI 已处理时直接跳过世界输入
 *   0x004060D6  mov  ecx,[world]    ; 世界输入对象
 *   ...
 *   0x0040CFFB  CALL 0x00482790    ; 真正的世界鼠标按下/角色移动路径
 *
 * v0.3-test7 曾在更早的 0x0040CFDD 包装 0x004C78C0。实机证明这会让普通地图左键和 Alt+F4 都失效。
 * 进一步反汇编发现 0x004C78C0 会把调用者保留的 ESI 直接压给下游 vtable+0x20，说明它存在
 * 非标准隐藏寄存器输入；用普通 C wrapper 再调用原函数会破坏这个上下文。
 *
 * test8 不再碰 0x004C78C0，而只改 0x0040CFFB 这一条世界调用：
 *   - 当前鼠标在主 HUD ID 0x0B / 0x0E 实时矩形内 -> 只跳过本次世界输入；
 *   - 其他任何位置 -> 原样调用 0x00482790；
 *   - 属性/道具窗口仍由 release fallback 开关，因此这里绝不主动开窗。
 *
 * 签名中的世界对象全局地址、CALL rel32、后续全局写地址都设为通配，只锁定稳定指令结构。
 */
static const BYTE WORLD_MOUSE_PRESS_CALLSITE_PATTERN[] = {
    0x8B,0x0D,0,0,0,0,
    0x3B,0xCF,
    0x74,0x10,
    0x8B,0x54,0x24,0x10,
    0x8B,0x44,0x24,0x0C,
    0x52,
    0x50,
    0x53,
    0xE8,0,0,0,0,
    0x89,0x1D,0,0,0,0
};
static const char WORLD_MOUSE_PRESS_CALLSITE_MASK[] = "xx????xxxxxxxxxxxxxxxx????xx????";

/*
 * 0x00406070 一带是游戏主循环里的“鼠标左键释放”分派点。
 *
 * 原版流程已经逐条反汇编确认：
 *   1. 外传主输入循环先通过 ComeOn.exe 自己的 GetCursorPos IAT 取得游戏逻辑鼠标坐标；
 *   2. 检测到鼠标从按下变成抬起时，把 event_type=1、mouse_x、mouse_y 压栈；
 *   3. ECX 放 UI manager（原版是 0x0055AF98）；
 *   4. 0x0040CF96 CALL 0x004C7930，把释放事件交给当前顶层 UI。
 *
 * v0.3-test4 的实机 A/B 日志已经确认，属性/道具顶部两个按钮真正对应直属 child ID 0x0B / 0x0E。
 * v0.3-test5 又证明：当 HUD 居中以后，鼠标释放有时根本到不了主 HUD 的 +0x24 事件函数，
 * 所以任何放在 +0x24 里面的兜底都“太晚了”。
 *
 * v0.3-test6 起把窗口 fallback 提升到这个全局释放分派点，test8 继续保留这条已实机成功路径：
 *   - 先根据主 HUD 当前真实 child 矩形判断鼠标是否落在 0x0B / 0x0E；
 *   - 永远先完整调用原版 0x004C7930；
 *   - 只有目标窗口 active 前后没有变化时，才复用原版同一个 vtable+0x1C 开关；
 *   - 兜底真正触发时返回 handled=1，保持原版“UI 已处理释放”的语义；按下阶段的世界穿透由独立 0x473F10 callsite guard 处理。
 *
 * 下面的签名把两个绝对全局地址和 E8 rel32 都设为通配，只锁定稳定指令结构。
 */
static const BYTE GLOBAL_MOUSE_RELEASE_CALLSITE_PATTERN[] = {
    0x8B,0x54,0x24,0x10,
    0x8B,0x44,0x24,0x0C,
    0x52,
    0x50,
    0x53,
    0xB9,0,0,0,0,
    0x89,0x3D,0,0,0,0,
    0xE8,0,0,0,0,
    0x85,0xC0,
    0x75,0x1A
};
static const char GLOBAL_MOUSE_RELEASE_CALLSITE_MASK[] = "xxxxxxxxxxxx????xx????x????xxxx";

/*
 * 0x0040F9C0 是“把当前分辨率重新应用给 GUI/JMM 管理器”的小包装函数。
 * 它从分辨率对象 +0x228/+0x22C 读取 Width/Height，然后在 0x0040F9D5 调 0x004C6970。
 * 用户高 BaseHeight 实机出现“启动不对、手工切一次分辨率就恢复”，正好指向这条调用时序。
 *
 * 这里把 ECX 中的 GUI 管理器绝对地址和 CALL rel32 都设为通配，只锁周围稳定结构。
 */
static const BYTE JMM_APPLY_CALLSITE_PATTERN[] = {
    0x8B,0x81,0x2C,0x02,0x00,0x00,
    0x8B,0x89,0x28,0x02,0x00,0x00,
    0x50,
    0x51,
    0x6A,0x00,
    0xB9,0,0,0,0,
    0xE8,0,0,0,0,
    0xC3
};
static const char JMM_APPLY_CALLSITE_MASK[] = "xxxxxxxxxxxxxxxxx????x????x";

/* 0x004C6970 已确认的函数头；用来验证 0x4087B5 真正 call 到我们理解的 JMM/UI 广播函数。 */
static const BYTE JMM_LOAD_FUNCTION_HEAD[] = {
    0x81,0xEC,0x00,0x01,0x00,0x00,
    0x53,0x55,0x56,0x57,
    0x68
};


/*
 * 底部主 HUD 的根对象构造函数签名，位于 0x4C2AE3 一带。
 *
 * 这条签名最重要的不是绝对地址，而是：
 *   C7 06 <vtable>        -> 把该对象的虚函数表写入对象开头；
 *   83 C8 FF              -> eax 置 -1；
 *   89 35 <global>        -> 把这个对象保存到全局主界面指针；
 *   后面连续初始化多个该类独有字段。
 *
 * vtable 和全局地址都设为通配，因此定位依靠“代码结构”，而不是整个 EXE 的 SHA-256。
 */
static const BYTE MAIN_HUD_ROOT_PATTERN[] = {
    0xC7,0x06, 0,0,0,0,
    0x83,0xC8,0xFF,
    0x89,0x35, 0,0,0,0,
    0x89,0x86,0xC0,0x00,0x00,0x00,
    0x89,0xBE,0x24,0x01,0x00,0x00,
    0x89,0x86,0x28,0x01,0x00,0x00,
    0x89,0x86,0x2C,0x01,0x00,0x00
};
static const char MAIN_HUD_ROOT_MASK[] = "xx????xxxxx????xxxxxxxxxxxxxxxxxxxxxxxx";

/*
 * 通用 JMM UI 布局函数 0x4B2B90 的函数头签名。
 * 主 HUD vtable 的 +0x58 槽原本就应该指向这个函数。
 *
 * 我们同时搜索“构造函数”和“通用布局函数”，再验证 vtable 槽确实等于这个唯一函数地址。
 * 三层检查都通过才安装 HUD hook，可以避免把错误 vtable 当成主 HUD。
 */
static const BYTE UI_LAYOUT_PATTERN[] = {
    0x8B,0x44,0x24,0x04,
    0x56,
    0x83,0xF8,0xFF,
    0x57,
    0x8B,0xF1,
    0x74,0x0C,
    0x89,0x46,0x14,
    0x8B,0x44,0x24,0x10,
    0xE9,0,0,0,0
};
static const char UI_LAYOUT_MASK[] = "xxxxxxxxxxxxxxxxxxxxx????";

/*
 * JMM 基础对象中已经由反汇编确认的字段偏移。
 *
 * +0x14 = 当前对象最终 X 坐标；
 * +0x18 = 当前对象最终 Y 坐标；
 * +0xA4 = 父对象指针。
 *
 * 子控件绘制/布局计算时会把父对象的 +0x14/+0x18 加到自己的局部坐标上。
 * 这就是只改“主 HUD 根节点”便能整体移动底栏的依据。
 *
 * 这里必须特别记录 v0.2-test1 的实机结论：视觉位置确实会整体移动，但并不是所有旧式控件的
 * 鼠标命中矩形都会自动使用同一套最终坐标。属性/道具按钮就出现了“画面已经移动、点击没有跟上”。
 * 因此 v0.2-test2 把视觉布局和输入 hit-test 分开处理，不能再把两者视为天然同步。
 */
#define UI_OBJECT_X_OFFSET       0x14u
#define UI_OBJECT_Y_OFFSET       0x18u
#define UI_OBJECT_WIDTH_OFFSET   0x1Cu
#define UI_OBJECT_HEIGHT_OFFSET  0x20u
#define UI_OBJECT_NEXT_OFFSET    0x08u
#define UI_OBJECT_PREV_OFFSET    0x0Cu
#define UI_OBJECT_CHILD_HEAD     0x9Cu
#define UI_OBJECT_CHILD_ITER     0xA0u
#define UI_OBJECT_HIT_CHILD      0xA8u
#define UI_OBJECT_CONTROL_ID     0x28u
#define UI_OBJECT_ACTIVE_A       0x64u
#define UI_OBJECT_ACTIVE_B       0x68u
#define UI_OBJECT_ACTIVE_LATCH   0xB8u

/*
 * 主 HUD vtable 中已经闭合的关键槽（这里写外传自己的地址，不能照抄本体）：
 *   +0x24 = 鼠标释放/点击事件分派，当前已验证样本为 0x004D8400；
 *   +0x30 = 原版子控件命中更新槽。兼容验证器会动态确认它仍调用通用 child hit-test
 *           0x004C6700，并把返回 child 写入 self+0xA8；layer1a 运行时绝不 Hook 这个槽；
 *   +0x58 = 通用布局 0x004C5F00；
 *   +0x08 = 普通 Draw，layer1a 只在安装图层功能时才会临时替换这一槽。
 *
 * 稳定基线的 HUD 居中只需要 +0x58；+0x24/+0x30 继续作为原版输入证据和目标对象解析依据。
 * test1~test7 已经证明：如果为了修视觉问题去改 +0x30/self+0xA8，会产生新的按钮误命中。
 * 因此 layer1a 的原则是“验证输入结构，但不接管输入结构”。
 */
#define MAIN_HUD_DESTRUCTOR_SLOT 0x00u
#define MAIN_HUD_DRAW_SLOT       0x08u
#define MAIN_HUD_EVENT_SLOT      0x24u
#define MAIN_HUD_INPUT_SLOT      0x30u
#define MAIN_HUD_LAYOUT_SLOT     0x58u

/*
 * 这是通用布局函数的真实调用约定。
 * __thiscall 的 this 放在 ECX，两个整数参数放在栈上，并由被调用函数 ret 8 清理。
 */
typedef int (__thiscall *FnUILayout)(LPVOID self, LONG x, LONG y);

/*
 * 主 HUD 真正的鼠标释放事件是 vtable +0x24。
 * 外传主输入循环读取光标，0x0040CF96 在鼠标由按下变成抬起时经 0x004C7930 调这个虚函数；
 * 此时三个参数已经闭合为：event_type=1、mouse_x、mouse_y。
 *
 * v0.3-test8 不再实际 hook 这个 +0x24 函数。下面保留的桥接实现只作为历史诊断代码和
 * 静态研究依据，不会写进 vtable；真正的新兜底已经提升到 0x0040CF96 -> 0x004C7930 的全局释放层。
 */
typedef void (__thiscall *FnMainHudEvent)(LPVOID self, LONG event_type, LONG mouse_x, LONG mouse_y);

/*
 * v0.3-test4 不再直接调用 0x004CAF10，也不再把 ID 0x0F / 0x10 当成“已经确认的两个视觉按钮”。
 *
 * v0.3-test3 的实机日志给了一个关键反证：
 *   - +0x24 事件参数记录到的 x/y 和 child 命中矩形明显不在同一套坐标系；
 *   - 但真正的原版 hit-test 0x004B3380 会自己调用 ComeOn.exe IAT 里的 GetCursorPos。
 *
 * 所以本版的输入 hook 只做“观察”，绝不改变游戏输入结果：
 *   1. 先用同一个 GAME_GetCursorPos 读取原版真正看到的鼠标位置；
 *   2. 完整调用原版 +0x24；
 *   3. 再读取 self+0xA8，记录原版最后到底命中了哪个 child；
 *   4. 同时列出鼠标当前落入的 0x09~0x10 候选 child，供下一版精确修复。
 */
static FnMainHudEvent g_original_main_hud_event = (FnMainHudEvent)0;
static FnUILayout g_original_main_hud_layout = (FnUILayout)0;

/*
 * 主 HUD vtable +0x00 是 scalar deleting destructor。
 * 它的参数只有一个删除标志 DWORD，返回 self。这个函数本身会先正常析构 HUD，
 * 再根据 flags&1 决定是否释放对象内存。
 *
 * test13 继续保留这个 destructor hook，但它只负责清掉 g_main_hud_instance 缓存。
 * 分辨率 profile 生命周期已经完全交给 0x0040B9C0 的 Strategy 状态 enter/exit callsite，
 * 所以 HUD 析构、地图临时重建和设备 Reset 都不会再改变 FRONTEND/GAMEPLAY 状态。
 */
typedef LPVOID (__thiscall *FnMainHudDestructor)(LPVOID self, DWORD flags);
static FnMainHudDestructor g_original_main_hud_destructor = (FnMainHudDestructor)0;

/*
 * 保存“最近一次真正参与布局的主 HUD 实例”。
 * 主 HUD 在切地图/读档时可能被销毁后重建，所以不能只在安装 hook 时猜一个固定对象地址；
 * 每次 +0x58 布局 hook 被游戏调用时都重新写这个指针，global mouse-release hook 始终使用最新实例。
 */
static LPVOID g_main_hud_instance = (LPVOID)0;

/*
 * UI manager 的全局鼠标释放函数 0x004C7930：
 *   ECX=this(UI manager)，栈参数依次是 event_type、mouse_x、mouse_y，返回非 0 表示事件已处理。
 * callsite 改到 fastcall 桥接后，unused_edx 只占住 EDX；三个真实参数的栈位置保持不变。
 */
typedef int (__thiscall *FnUiManagerMouseRelease)(LPVOID self, LONG event_type, LONG mouse_x, LONG mouse_y);
static FnUiManagerMouseRelease g_original_ui_manager_mouse_release = (FnUiManagerMouseRelease)0;

/*
 * 0x00482790 是 UI manager 已经明确“没有处理本次按下”以后才会进入的世界输入函数。
 * 调用点使用 ECX=世界对象，栈上仍是 event_type / mouse_x / mouse_y，并由原函数 ret 0x0C 清栈。
 *
 * 与 0x004C78C0 不同，0x00482790 的函数头会先保存 EBX/EBP/ESI/EDI，然后立即 mov esi,ecx；
 * 没有观察到依赖调用者原始 ESI/EDI/EBX 的隐藏输入，因此可以用同形状桥接安全包裹。
 */
typedef void (__thiscall *FnWorldMousePress)(LPVOID self, LONG event_type, LONG mouse_x, LONG mouse_y);
static FnWorldMousePress g_original_world_mouse_press = (FnWorldMousePress)0;

/*
 * test13 不再把 world 对象是否存在当成 gameplay 判据。
 * 用户已经用 test12 实机证明：主菜单阶段 world 全局对象同样会存在，所以“对象非空”并不等于进入地图。
 * 世界输入 hook 仍保留，只负责 0x0B/0x0E 点击防穿透；它和分辨率生命周期彻底解耦。
 */

/*
 * 外传 Strategy-enter / 外传 Strategy-exit 是 Strategy 状态进入/离开时由 0x0040B9C0 状态机直接调用的原版函数。
 * 二者都使用 thiscall：ECX 是同一个显示/高层状态管理对象，栈上没有参数。
 */
typedef void (__thiscall *FnStrategyStateStep)(LPVOID self);
static FnStrategyStateStep g_original_strategy_enter = (FnStrategyStateStep)0;
static FnStrategyStateStep g_original_strategy_exit = (FnStrategyStateStep)0;

/*
 * test14 起保存 外传 Strategy-enter 开头 `push 0` 的“立即数字节”地址。
 * test15 继续原样使用这条进入游戏 force 补丁；本轮新增逻辑只发生在 Strategy exit 之后。
 * 原机器码是：
 *     56          push esi
 *     8B F1       mov  esi,ecx
 *     6A 00       push 0        ; 传给 0x0040BCC0 的 force 参数
 *
 * 进入 Strategy state=3 时，我们只把最后这个 00 临时改成 01。
 * 这样原版 外传 Strategy-enter 自己仍然负责读取 mode、写 self+0x08、调用 0x0040BCC0 和后续 Strategy 初始化；
 * DisplayFix 不复制这些逻辑，也不额外调用第二次 SetDisplayMode。原函数返回后马上恢复 00。
 */
static BYTE* g_strategy_enter_force_immediate = (BYTE*)0;
static BYTE g_strategy_enter_force_original = 0u;

static BOOL g_strategy_state_hooks_installed = FALSE;
static DWORD g_frontend_hud_skip_log_count = 0u;

/*
 * v0.3-test4 的实机日志已经把主 HUD 顶部两个圆形按钮真正闭合为：
 *   ID 0x0B -> 事件函数 0x4C4000 分支，控制一个独立顶层窗口；
 *   ID 0x0E -> 事件函数 0x4C3F53 分支，控制另一个独立顶层窗口。
 *
 * 这里不再写死 0x559274 / 0x55BDC8。安装 hook 时会从已经验证的原版事件函数机器码
 * “mov ecx,[absolute]”指令里解析出这两个全局槽地址。这样附件里的其他兼容 EXE 只要代码结构一致，
 * 就不需要为每个 SHA-256 单独维护地址表。
 */
static LPVOID* g_top_button_0b_target_slot = (LPVOID*)0;
static LPVOID* g_top_button_0d_target_slot = (LPVOID*)0;
static LPVOID* g_top_button_0e_target_slot = (LPVOID*)0;

/* 当前目标逻辑宽高，例如 854x480、1920x1080。 */
static DWORD g_target_width = 0u;
static DWORD g_target_height = 0u;

/*
 * HUD 只存在两套已经确认的原生布局模板：
 *   BaseHeight < 600  -> 640 参考宽；
 *   BaseHeight >=600  -> 800 参考宽。
 * 世界分辨率本身仍然允许任意 BaseHeight。
 */
static DWORD g_native_base_width = 640u;

/*
 * test11 把“前端/动画”和“真正游戏内”重新拆成原游戏本来的两套分辨率生命周期。
 *
 * 以前 test10 在 ASI 初始化时就把 640/800/内部三个分支全部永久改成 TargetWidth x TargetHeight，
 * 这会让原本固定 640x480 的主菜单和开场/过场动画也得到一个超大的主表面。菜单本身仍只画原生
 * 4:3 区域，于是画面停在左上角，未覆盖区域还可能残留动画/3D surface 的旧内容，出现用户截图中
 * “黑底 + 黄色轮廓残影”一类现象。
 *
 * test11 改成两套“代码配置 profile”：
 *   FRONTEND：完全恢复 EXE 原始的 640/800/内部分辨率与 JMM 比较；主菜单/动画继续用原版 640x480，
 *             最终放大、保持 4:3、居中交给 cnc-ddraw 这一层完成；
 *   GAMEPLAY：只有游戏状态机真正进入 Strategy 状态 3 前才把对应立即数改成 DisplayFix 目标分辨率。
 *
 * 这些指针全部来自唯一内容签名，不按 SHA-256 绑版本；原始立即数也在安装时从当前 EXE 现场保存，
 * 所以 Steam EXE、原版 EXE 和已经修改过隐藏分支的宽屏 EXE 都能各自恢复“自己原来的前端代码”。
 */
typedef int (__thiscall *FnDisplayModeApply)(LPVOID self, LONG mode, LONG force);
static FnDisplayModeApply g_display_mode_apply = (FnDisplayModeApply)0;

static BYTE* g_res_mode_patch = (BYTE*)0;
static BYTE* g_res_map1_patch = (BYTE*)0;
static BYTE* g_res_map2_patch = (BYTE*)0;
static BYTE* g_jmm_selector_patch = (BYTE*)0;

static DWORD g_res_mode_original[6];
static DWORD g_res_map1_original[5];
static DWORD g_res_map2_original[6];
static DWORD g_jmm_original_cmp640 = 0u;
static DWORD g_jmm_original_cmp800 = 0u;
static BOOL g_resolution_profile_ready = FALSE;
static BOOL g_gameplay_profile_active = FALSE;

/*
 * Strategy enter/exit 原函数内部可能触发 SetDisplayMode、设备重建和 HUD 临时布局。
 * 这段调用栈里绝不能再递归做 HUD 居中或 Steam full JMM；等原版 Strategy 步骤返回后，
 * 新 HUD 后续的正常 +0x58 自动布局再执行游戏内修复。
 */
static BOOL g_strategy_transition_in_progress = FALSE;

/* ComeOn.exe 的显示模式管理对象；当前兼容样本中架构固定，真实修改位置仍由内容签名验证。 */
#define GAME_DISPLAY_MANAGER ((LPVOID)0x00548398u)
#define DISPLAY_CURRENT_MODE_OFFSET   0x04u
#define DISPLAY_CURRENT_WIDTH_OFFSET  0x228u
#define DISPLAY_CURRENT_HEIGHT_OFFSET 0x22Cu

/* 是否启用主 HUD 居中；顶部按钮兜底在更上层的全局鼠标释放 hook 中，与此开关彼此独立。 */
static BOOL g_center_main_hud = TRUE;

/*
 * layer1a 独立图层开关。
 * TRUE 只表示“允许辅助 GUI 在需要时调整 Draw 先后”，并不保证每帧都会调整：
 * 真正执行还必须同时满足 GAMEPLAY、CenterMainHUD=1、Strategy 不在切换、HUD 与目标都在同一顶层绘制链。
 * FALSE 时不会安装 layer1a 绘制 Hook，运行行为直接退回 v0.3.2 / v0.2.1 稳定基线。
 */
static BOOL g_auxiliary_ui_above_hud = TRUE;

/*
 * Steam 版的 ComeOnSteam.exe 会额外 LoadLibraryA("ComeOn.dll")。
 * 非 Steam 版没有这个模块，因此它可以作为“是否启用 Steam 专用兼容路径”的直接运行时条件。
 *
 * 这里故意不根据 EXE SHA-256 判断 Steam：
 *   - 项目总原则是按真实结构/内容签名兼容不同 EXE；
 *   - ComeOn.dll 是否真的已经装进当前进程，比“文件来自哪个发行渠道”更准确。
 */
static BOOL g_steam_environment = FALSE;


/* ============================================================================================== */
/* Steam 多语言 ResJM.Lib 最终兜底（从 v0.1-test4 单独移植；不包含任何影片/OpenGL 实验代码）              */
/* ============================================================================================== */

/*
 * 这一小段是 clean1 唯一从 test2 之后版本带回来的运行时代码。
 * 用户已经在 Steam 版实机确认：目录里没有裸 `ResJM.Lib`，而是四个语言文件：
 *   ResJM.Lib.chs  简体中文
 *   ResJM.Lib.cht  繁体中文
 *   ResJM.Lib.eng  英文
 *   ResJM.Lib.jpn  日文
 *
 * test4 的窄 CreateFileA 调用点 shim 成功解决了“找不到 ResJM.Lib”的弹窗，因此 clean1 只保留这一项。
 * test3~test9 的 delayed-JMM 改写、DirectShow、ActiveMovie、MovieManager、teardown、worker/export 等
 * 所有实验运行代码一律没有移植回来，避免继续污染当前纯净基线。
 */
static BYTE* g_steam_create_file_callsite = (BYTE*)0;
static BOOL g_steam_resjm_shim_installed = FALSE;
static DWORD g_steam_resjm_runtime_log_count = 0u;

/*
 * 返回完整路径中“最后一个文件名”的起点。
 * 例如：
 *   F:\\Game\\ResJM.Lib
 * 会返回指向 `ResJM.Lib` 的位置。
 *
 * 这里只在原字符串中移动指针，不申请新内存，也不会修改原路径。
 */
static const char* path_basename(const char* path)
{
    const char* name = path;
    DWORD i = 0u;

    if (!path) {
        return (const char*)0;
    }

    while (path[i] != '\0') {
        if (path[i] == '\\' || path[i] == '/') {
            name = path + i + 1u;
        }
        ++i;
    }

    return name;
}

/*
 * Steam/启动器写入 ComeOn.ini 的语言值可能是短码，也可能是较长的人类可读名称。
 * 游戏目录真正使用的文件后缀只有 chs / cht / eng / jpn 四种，所以这里把常见别名统一到这四个值。
 *
 * 返回 TRUE 代表已经成功识别并写入 output；
 * 返回 FALSE 代表无法识别，调用者会使用 ComeOn.dll 原本的默认值 chs。
 */
static BOOL normalize_steam_language(const char* input, char* output, DWORD output_size)
{
    const char* normalized = (const char*)0;

    if (!output || output_size < 4u) {
        return FALSE;
    }
    output[0] = '\0';

    if (!input || input[0] == '\0') {
        return FALSE;
    }

    if (str_equal_icase(input, "chs") || str_equal_icase(input, "schinese") ||
        str_equal_icase(input, "zh-cn") || str_equal_icase(input, "zh_cn") ||
        str_equal_icase(input, "simplified_chinese")) {
        normalized = "chs";
    } else if (str_equal_icase(input, "cht") || str_equal_icase(input, "tchinese") ||
               str_equal_icase(input, "zh-tw") || str_equal_icase(input, "zh_tw") ||
               str_equal_icase(input, "traditional_chinese")) {
        normalized = "cht";
    } else if (str_equal_icase(input, "eng") || str_equal_icase(input, "english") ||
               str_equal_icase(input, "en")) {
        normalized = "eng";
    } else if (str_equal_icase(input, "jpn") || str_equal_icase(input, "japanese") ||
               str_equal_icase(input, "ja")) {
        normalized = "jpn";
    }

    if (!normalized) {
        return FALSE;
    }

    str_copy(output, output_size, normalized);
    return TRUE;
}

/*
 * 读取 Steam 版 ComeOn.dll 同目录下的 ComeOn.ini。
 * 已有逆向证据表明 ComeOn.dll 自己读取：
 *
 *   [local_config]
 *   current_language=chs
 *
 * clean1 不创建第二套语言配置，而是直接复用同一个文件和同一个默认值。
 */
static void read_steam_current_language(HMODULE steam_module, char* output, DWORD output_size)
{
    char dll_path[1024];
    char ini_path[1024];
    char raw_language[64];

    if (!output || output_size == 0u) {
        return;
    }

    /* ComeOn.dll 自己的默认语言就是简体中文 chs。 */
    str_copy(output, output_size, "chs");

    if (!steam_module || !g_GetPrivateProfileStringA) {
        return;
    }

    dll_path[0] = '\0';
    ini_path[0] = '\0';
    raw_language[0] = '\0';

    if (GAME_GetModuleFileNameA(steam_module, dll_path, (DWORD)sizeof(dll_path)) == 0u) {
        return;
    }

    make_sibling_path(dll_path, "ComeOn.ini", ini_path, (DWORD)sizeof(ini_path));
    g_GetPrivateProfileStringA("local_config",
                               "current_language",
                               "chs",
                               raw_language,
                               (DWORD)sizeof(raw_language),
                               ini_path);

    if (!normalize_steam_language(raw_language, output, output_size)) {
        str_copy(output, output_size, "chs");
    }
}

/*
 * Steam-only CreateFileA 最终兜底。
 *
 * 安全边界非常窄：只有路径最后的文件名“恰好”等于 `ResJM.Lib` 时才进行语言后缀尝试。
 * 因此：
 *   - `ResJM.Lib.chs` 等已经带语言后缀的请求不会再次被追加；
 *   - 其它 .lib、贴图、音频、存档、配置都完全不受影响；
 *   - 非 Steam 环境根本不会安装这个 shim。
 *
 * 这里也不覆盖游戏的 CreateFileA IAT。ComeOn.dll 仍可按自己的方式处理 CreateFileA；
 * DisplayFix 只改游戏低层文件包装器中的一条 CALL，避免两个 Hook 互相踩踏。
 */
static HANDLE __stdcall steam_create_file_a_shim(LPCSTR name,
                                                   DWORD access,
                                                   DWORD share,
                                                   LPVOID security,
                                                   DWORD creation,
                                                   DWORD flags,
                                                   HANDLE template_file)
{
    const char* base_name;
    FnCreateFileA real_create_file;

    /*
     * 每次都从游戏原始 IAT 读取当前 CreateFileA 地址。
     * 我们从不把 0x005511E4 改成自己的函数，所以这里不会递归调用自己。
     */
    real_create_file = GAME_CreateFileA;
    if (!real_create_file) {
        return INVALID_HANDLE_VALUE;
    }

    base_name = path_basename(name);

    if (base_name && str_equal_icase(base_name, "ResJM.Lib")) {
        HMODULE steam_module = GAME_GetModuleHandleA ? GAME_GetModuleHandleA("ComeOn.dll") : (HMODULE)0;
        char language[16];
        char localized_path[1200];
        HANDLE localized_file;

        language[0] = '\0';
        localized_path[0] = '\0';

        /*
         * 把裸文件名变成当前语言对应的真实磁盘文件名，例如：
         *   ResJM.Lib  ->  ResJM.Lib.chs
         */
        read_steam_current_language(steam_module, language, (DWORD)sizeof(language));
        str_copy(localized_path, (DWORD)sizeof(localized_path), name);
        str_append(localized_path, (DWORD)sizeof(localized_path), ".");
        str_append(localized_path, (DWORD)sizeof(localized_path), language);

        localized_file = real_create_file(localized_path,
                                          access,
                                          share,
                                          security,
                                          creation,
                                          flags,
                                          template_file);

        /* 日志最多写四次，足够诊断，同时避免文件系统调用频繁刷日志。 */
        if (g_steam_resjm_runtime_log_count < 4u) {
            char line[1400];
            line[0] = '\0';
            str_copy(line, (DWORD)sizeof(line), "[运行] Steam原始ResJM.Lib重定向 -> ");
            str_append(line, (DWORD)sizeof(line), localized_path);
            str_append(line,
                       (DWORD)sizeof(line),
                       (localized_file != INVALID_HANDLE_VALUE)
                           ? " 结果=成功"
                           : " 结果=失败；继续尝试原始文件");
            append_runtime_line(line);
            ++g_steam_resjm_runtime_log_count;
        }

        if (localized_file != INVALID_HANDLE_VALUE) {
            return localized_file;
        }
    }

    /*
     * 如果未来官方目录重新出现裸 ResJM.Lib，或者语言后缀文件缺失，就保留原始请求作为最终回退。
     */
    return real_create_file(name, access, share, security, creation, flags, template_file);
}

/*
 * 安装 ResJM 兜底时只改一条经过内容签名确认的低层 CreateFileA 调用。
 * 原指令是 6 字节：
 *   FF 15 E4 11 55 00       call dword ptr [0x005511E4]
 *
 * clean1 改成：
 *   E8 xx xx xx xx          call steam_create_file_a_shim
 *   90                      nop
 *
 * 指令长度保持 6 字节，不移动后面的任何游戏代码。
 */
static BOOL install_steam_resjm_language_shim(const TextRegion* exe_text)
{
    static const BYTE CREATE_FILE_CALL_PATTERN[] = {
        0xFF,0x75,0xF0,
        0xFF,0x75,0xF4,
        0xFF,0x75,0x08,
        0xFF,0x15,0xE4,0x11,0x55,0x00,
        0x8B,0xF0,
        0x3B,0xF7,
        0x75,0x14,
        0xFF,0x15,0xF0,0x11,0x55,0x00
    };
    static const char CREATE_FILE_CALL_MASK[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxx";
    BYTE* match;
    BYTE* callsite;
    BYTE replacement[6];
    DWORD displacement;

    if (g_steam_resjm_shim_installed) {
        return TRUE;
    }
    if (!exe_text) {
        return FALSE;
    }

    match = find_unique_pattern(exe_text,
                                CREATE_FILE_CALL_PATTERN,
                                CREATE_FILE_CALL_MASK,
                                (DWORD)sizeof(CREATE_FILE_CALL_PATTERN));
    if (!match) {
        return FALSE;
    }

    /* Pattern 第 9 字节正好是那条 6 字节 CreateFileA 间接 CALL。 */
    callsite = match + 9u;

    replacement[0] = 0xE8;
    displacement = (DWORD)((BYTE*)&steam_create_file_a_shim - (callsite + 5u));
    write_u32_raw(replacement + 1u, displacement);
    replacement[5] = 0x90;

    if (!patch_bytes(callsite, replacement, (DWORD)sizeof(replacement))) {
        return FALSE;
    }

    g_steam_create_file_callsite = callsite;
    g_steam_resjm_shim_installed = TRUE;
    return TRUE;
}

/*
 * Steam GUI 修复只允许成功执行一次。
 * in_progress 用来防止我们广播顶层 UI 尺寸时，某个对象又反过来触发主 HUD +0x58，造成递归广播；
 * done 则表示本次进程已经把缺失的第二阶段布局补完，不需要以后每次 HUD layout 都重复做。
 */
static BOOL g_steam_ui_sync_in_progress = FALSE;
static BOOL g_steam_ui_sync_done = FALSE;
static BOOL g_steam_ui_sync_wait_root_logged = FALSE;
static DWORD g_steam_ui_sync_attempts = 0u;
static DWORD g_steam_ui_sync_applied = 0u;

/* 运行时日志严格限次数，避免老游戏频繁写磁盘。 */
static DWORD g_hud_layout_log_count = 0u;
static DWORD g_hud_event_log_count = 0u;
static DWORD g_global_release_log_count = 0u;
static DWORD g_world_press_log_count = 0u;
static DWORD g_hud_candidate_log_count = 0u;
/* test13 只记录少量 Strategy 进入/离开与前端 HUD 跳过信息，避免运行时刷盘。 */
static DWORD g_strategy_enter_log_count = 0u;
static DWORD g_strategy_exit_log_count = 0u;

/*
 * 从一个 child 读取矩形。
 * 这里仍沿用原版 0x4B3380 的定义：
 *   left = child+0x14
 *   top = child+0x18
 *   right = left + width - 1
 *   bottom = top + height - 1
 */
typedef struct HudRect {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
    BOOL valid;
} HudRect;

static BOOL read_child_rect(LPVOID child, HudRect* rect)
{
    LONG width;
    LONG height;

    if (!child || !rect) {
        return FALSE;
    }

    width = *(LONG*)((BYTE*)child + UI_OBJECT_WIDTH_OFFSET);
    height = *(LONG*)((BYTE*)child + UI_OBJECT_HEIGHT_OFFSET);

    if (width <= 0 || height <= 0) {
        rect->valid = FALSE;
        return FALSE;
    }

    rect->left = *(LONG*)((BYTE*)child + UI_OBJECT_X_OFFSET);
    rect->top = *(LONG*)((BYTE*)child + UI_OBJECT_Y_OFFSET);
    rect->right = rect->left + width - 1;
    rect->bottom = rect->top + height - 1;
    rect->valid = TRUE;
    return TRUE;
}

static BOOL point_in_hud_rect(LONG x, LONG y, const HudRect* rect)
{
    if (!rect || !rect->valid) {
        return FALSE;
    }

    return (x >= rect->left && x <= rect->right &&
            y >= rect->top && y <= rect->bottom) ? TRUE : FALSE;
}

/*
 * 只读地判断 child 当前是否“理论上可命中”。
 * 原版 0x4B1DB0 的核心语义就是：
 *   child+0x68 非零 -> 可用；
 *   否则 child+0x64 非零 -> 可用，同时可能更新 +0xB8 latch。
 *
 * 诊断代码不能去调用 0x4B1DB0，因为那样会改变 +0xB8。
 * 所以这里只读取 +0x64/+0x68，不产生任何副作用。
 */
static BOOL child_active_for_diagnostic(LPVOID child)
{
    LONG active_a;
    LONG active_b;

    if (!child) {
        return FALSE;
    }

    active_a = *(LONG*)((BYTE*)child + UI_OBJECT_ACTIVE_A);
    active_b = *(LONG*)((BYTE*)child + UI_OBJECT_ACTIVE_B);

    return (active_a != 0 || active_b != 0) ? TRUE : FALSE;
}

/* 把一个 child 的 ID、指针、矩形和 active 状态追加到一行日志。 */
static void append_child_brief(char* line, DWORD line_size, LPVOID child)
{
    HudRect rect;
    DWORD control_id;

    if (!child) {
        str_append(line, line_size, "无");
        return;
    }

    control_id = *(DWORD*)((BYTE*)child + UI_OBJECT_CONTROL_ID);

    str_append(line, line_size, "指针=");
    append_hex32(line, line_size, (DWORD)child);
    str_append(line, line_size, " ID=");
    append_hex32(line, line_size, control_id);
    str_append(line, line_size, " 激活=");
    append_int(line, line_size, child_active_for_diagnostic(child) ? 1 : 0);

    if (read_child_rect(child, &rect)) {
        str_append(line, line_size, " 矩形=");
        append_int(line, line_size, rect.left);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.top);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.right);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.bottom);
    } else {
        str_append(line, line_size, " 矩形=无效");
    }
}

/*
 * 第一次/前几次 HUD 自动布局完成后，把 control ID 0x09~0x10 的直属 child 全部列出来。
 */
static void log_hud_candidate_children(LPVOID self)
{
    LPVOID child;

    if (!self || g_hud_candidate_log_count >= 1u) {
        return;
    }
    ++g_hud_candidate_log_count;

    child = *(LPVOID*)((BYTE*)self + UI_OBJECT_CHILD_HEAD);

    while (child) {
        DWORD control_id = *(DWORD*)((BYTE*)child + UI_OBJECT_CONTROL_ID);

        if (control_id >= 0x09u && control_id <= 0x10u) {
            char line[384];
            line[0] = '\0';
            str_append(line, (DWORD)sizeof(line), "[运行] HUD候选 ");
            append_child_brief(line, (DWORD)sizeof(line), child);
            append_runtime_line(line);
        }

        child = *(LPVOID*)((BYTE*)child + UI_OBJECT_NEXT_OFFSET);
    }
}

/*
 * 下面两个函数的实现位于本文件更后面，但主 HUD layout hook 需要先调用它们，
 * 所以先写“函数声明”。函数声明可以理解成先告诉编译器：后面会有这样一个函数，参数和返回值如下。
 */
static LPVOID find_main_hud_child_by_id(LPVOID self, DWORD wanted_id);
static void try_steam_delayed_ui_sync(LPVOID hud);
static BOOL set_frontend_resolution_profile(void);
static BOOL set_gameplay_resolution_profile(void);

/*
 * 主 HUD 析构 hook。
 *
 * 只要不是我们自己正在做“前端 -> 游戏内”的显示模式重建，也不是 Steam delayed JMM 正在重载布局，
 * 就把下一阶段重新视为“前端 profile”。这里不主动 SetDisplayMode(640x480)：真正返回主菜单时，
 * 原游戏本来就会自己请求固定 640x480。我们只负责保证那次原版请求不再被 TargetWidth/Height 截走。
 *
 * 这样做比在析构函数里强行 Reset 设备安全：地图切换/读档如果临时重建 HUD，也不会被 DisplayFix
 * 粗暴插入一次额外 640x480 Reset；最坏只是在下一次 HUD 出现时重新确认/激活 GAMEPLAY profile。
 */
static LPVOID __fastcall main_hud_destructor_hook(LPVOID self, LPVOID unused_edx, DWORD flags)
{
    LPVOID result;

    (void)unused_edx;

    if (!g_original_main_hud_destructor) {
        return self;
    }

    result = g_original_main_hud_destructor(self, flags);

    /*
     * test11 已被实机证明不能把“HUD 析构”当成返回前端；test12 的 world 对象生命周期同样不够精确。
     * test13 因此这里只清掉缓存实例，FRONTEND/GAMEPLAY 由 Strategy 状态机唯一负责。
     */
    if (g_main_hud_instance == self) {
        g_main_hud_instance = (LPVOID)0;
    }

    return result;
}

/*
 * test13：Strategy 状态 3 的进入桥接。
 *
 * 这个 hook 是从 0x0040BA5F 的原版 callsite 进入的。到这里时，状态机已经先执行：
 *     self+0x0C = 3
 * 所以不需要靠 HUD、world、鼠标或资源对象去“猜”是不是 gameplay。
 *
 * 顺序非常重要：
 *   1. 先把 ComeOn.exe 的几处分辨率/JMM 立即数切成 GAMEPLAY profile；
 *   2. 再调用原版 外传 Strategy-enter；
 *   3. 原版 外传 Strategy-enter 自己会读取 self+0x280 的显示模式并调用 0x0040BCC0。
 *
 * 因此 DisplayFix 不再额外调用 SetDisplayMode，也就不会像 test11/test12 一样在一个“疑似 gameplay”对象
 * 出现时突然 Reset 设备。真正的分辨率切换时刻完全跟随游戏原本的 BeforeStrategy 流程。
 */
static void __fastcall strategy_enter_hook(LPVOID self, LPVOID unused_edx)
{
    BOOL profile_ok;
    BOOL force_patch_ok = FALSE;
    BOOL force_restore_ok = TRUE;
    BOOL live_matches_target;
    BYTE forced_value = 1u;
    char line[384];

    (void)unused_edx;

    if (!g_original_strategy_enter) {
        return;
    }

    /*
     * 新一轮 Strategy 会重新创建/重排 HUD，因此 Steam test10 的 one-shot 也必须按“每次进入游戏”重置。
     * 这里只重置 DisplayFix 自己的标志，不主动加载 JMM；真正 delayed apply 仍要等新 HUD/资源根成熟。
     */
    g_steam_ui_sync_done = FALSE;
    g_steam_ui_sync_wait_root_logged = FALSE;
    g_steam_ui_sync_attempts = 0u;
    g_steam_ui_sync_applied = 0u;
    g_main_hud_instance = (LPVOID)0;
    g_hud_candidate_log_count = 0u;
    g_hud_layout_log_count = 0u;

    /*
     * 第一步仍然是 test13 已证明时机正确的做法：把 mode 4/5/6 对应的宽高和 JMM 选择器切到 GAMEPLAY。
     * 注意，这一步只改“同一个 mode ID 对应什么宽高”，不会改变 self+0x280 里的 mode ID 本身。
     */
    profile_ok = set_gameplay_resolution_profile();

    /*
     * class-atom guard 自身不能在 CreateWindowExA 回调里直接写文件日志，否则可能递归进入窗口/系统初始化。
     * 到 Strategy 入口时系统已经稳定，所以在这里把累计命中数一次性写出来。
     */
    if (!g_steam_class_atom_runtime_logged && g_steam_class_atom_bypass_count > 0u) {
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] Steam CreateWindowExA 类Atom保护命中次数=");
        append_int(line, (DWORD)sizeof(line), (LONG)g_steam_class_atom_bypass_count);
        str_append(line, (DWORD)sizeof(line), " 最后Atom=");
        append_hex32(line, (DWORD)sizeof(line), (DWORD)g_steam_last_class_atom);
        append_runtime_line(line);
        g_steam_class_atom_runtime_logged = TRUE;
    }

    if (g_strategy_enter_log_count < 4u) {
        ++g_strategy_enter_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] Strategy进入 状态=");
        if (self) {
            append_int(line, (DWORD)sizeof(line), *(LONG*)((BYTE*)self + 0x0Cu));
        } else {
            append_int(line, (DWORD)sizeof(line), -1);
        }
        str_append(line, (DWORD)sizeof(line), profile_ok ? " GAMEPLAY配置=就绪" : " GAMEPLAY配置=失败");
        append_runtime_line(line);
    }

    /*
     * test13 的关键失败点就在这里。
     *
     * 原版 外传 Strategy-enter 会执行：
     *     push 0               ; force = 0
     *     mov eax,[self+0x280] ; 当前显示模式 ID，例如 4
     *     push eax
     *     mov [self+0x08],eax
     *     call 0x0040BCC0
     *
     * 0x0040BCC0 一开始会比较 self+0x04（当前 mode ID）和传入 mode ID。
     * 如果二者相同，并且第二个参数 force 也是 0，它就直接返回，不会走到我们已经改成 854x480 / 1068x600
     * 的宽高立即数。因此 test13 日志才会出现“GAMEPLAY profile=ready，但 live=640x480”。
     *
     * test14 不额外调用一次 0x0040BCC0，而是临时把原函数自己的 `push 0` 改成 `push 1`。
     * 这样原版调用链只执行一次，但会真正重建同一个 mode ID 对应的新宽高。
     */
    if (profile_ok && g_strategy_enter_force_immediate && g_strategy_enter_force_original == 0u) {
        force_patch_ok = patch_bytes(g_strategy_enter_force_immediate, &forced_value, 1u);
    }

    /*
     * 如果连这个 1 字节都无法安全写入，就不要在错误的 640x480 surface 上继续启用宽屏 HUD/JMM。
     * 这里立刻回退 FRONTEND profile，让游戏至少保持原生 4:3 可玩，而不是产生 test13 那种 GUI 大错位。
     */
    if (profile_ok && !force_patch_ok) {
        append_runtime_line("[运行] Strategy进入：强制重应用补丁失败；回退到FRONTEND配置");
        set_frontend_resolution_profile();
        profile_ok = FALSE;
    }

    /*
     * 原版在这里执行真正的显示模式应用和 Strategy 初始化。
     * 期间设备可能销毁/重建 HUD，所以继续保留 transition guard：这段调用栈内只允许原版布局。
     */
    g_strategy_transition_in_progress = TRUE;
    g_original_strategy_enter(self);
    g_strategy_transition_in_progress = FALSE;

    /*
     * 原函数已经返回，立即把 `push 1` 恢复成原版 `push 0`。
     * 这样其他任何潜在的 外传 Strategy-enter 调用都继续保持游戏原始语义；强制重应用只发生在我们的 Strategy hook 这一次。
     */
    if (force_patch_ok) {
        force_restore_ok = patch_bytes(g_strategy_enter_force_immediate, &g_strategy_enter_force_original, 1u);
        if (!force_restore_ok) {
            append_runtime_line("[运行] Strategy进入：强制重应用恢复失败；Strategy进入流程仍保持强制=1");
        }
    }

    /*
     * test14 的第二道保险：不要只相信“补丁写成功”，还要看游戏显示管理器最终记录的 live 宽高。
     * 只有 live 真正等于 TargetWidth x TargetHeight，HUD 居中和 Steam delayed JMM 才有资格继续执行。
     */
    live_matches_target =
        self &&
        (*(LONG*)((BYTE*)self + DISPLAY_CURRENT_WIDTH_OFFSET) == (LONG)g_target_width) &&
        (*(LONG*)((BYTE*)self + DISPLAY_CURRENT_HEIGHT_OFFSET) == (LONG)g_target_height);

    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[运行] Strategy进入 原版应用完成 实际=");
    append_int(line, (DWORD)sizeof(line), self ? *(LONG*)((BYTE*)self + DISPLAY_CURRENT_WIDTH_OFFSET) : 0);
    str_append(line, (DWORD)sizeof(line), "x");
    append_int(line, (DWORD)sizeof(line), self ? *(LONG*)((BYTE*)self + DISPLAY_CURRENT_HEIGHT_OFFSET) : 0);
    str_append(line, (DWORD)sizeof(line), " 目标=");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_width);
    str_append(line, (DWORD)sizeof(line), "x");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_height);
    str_append(line, (DWORD)sizeof(line), force_patch_ok ? " 强制=1" : " 强制=0");
    append_runtime_line(line);

    if (profile_ok && !live_matches_target) {
        append_runtime_line("[运行] Strategy进入：实际分辨率与目标不符；禁用GAMEPLAY HUD/JMM并恢复FRONTEND代码配置");
        set_frontend_resolution_profile();
    }

    /*
     * 统一 Runtime 事件：只有显示模式真正进入目标 GAMEPLAY Surface 后才广播。
     * DisplayFix 仍然负责这个已经实机验证过的物理 Hook；以后手柄/QoL 模块只订阅这个事件，
     * 不再各自去改同一个 Strategy callsite。这样可以从 v0.1-dev1 开始逐步建立“一个 Hook，多模块共享”。
     */
    if (profile_ok && live_matches_target) {
        Runtime_EmitEvent(RUNTIME_EVENT_GAMEPLAY_ENTER, self, g_target_width, g_target_height);
    }
}

/*
 * test15：离开旧 Strategy 状态 3 的前端恢复桥接。
 *
 * test14 在这里暴露了一个非常重要的区别：
 *   - set_frontend_resolution_profile() 只把 ComeOn.exe 里的“以后再选择 mode 时应该得到什么宽高”恢复成原值；
 *   - 它不会自动改变当前已经建立好的 DirectDraw 主 surface。
 *
 * 所以 test14 虽然打印了：
 *     FRONTEND profile=restored
 * 但当前 live display 仍然是刚才游戏内的 854x480 / 1068x600 / 其它目标宽高。
 * 返回标题以后，原生 640x480 菜单素材便被画进这个仍然宽屏的 surface，最终出现用户截图中的宽屏标题错位、
 * 小地图/技能 UI 残留等现象。
 *
 * test15 的顺序严格按“先清游戏内，再恢复前端显示”执行：
 *
 *   第 1 步：记住旧状态，并清掉 DisplayFix 缓存的 HUD 指针。
 *   第 2 步：设置 transition guard，然后完整调用原版 外传 Strategy-exit。
 *           这一步先让游戏自己结束 Strategy 资源；我们绝不在旧 HUD/旧 world 仍处于清理中时重建显示设备。
 *   第 3 步：原版清理返回后，恢复 FRONTEND 分辨率/JMM 立即数。
 *   第 4 步：复用已经由内容签名解析并验证过的原版 0x0040BCC0，强制请求 mode 4。
 *           force=1 很重要：当前 self+0x04 很可能仍然也是 mode 4；如果 force=0，0x0040BCC0 会和 test13
 *           一样因为“mode ID 没变”直接早退，live surface 就还是宽屏。
 *   第 5 步：读取 self+0x228/self+0x22C，确认真的回到了当前 EXE 自己保存下来的 mode 4 原始宽高。
 *
 * 为什么使用 mode 4：
 *   - 原游戏启动路径 0x0040C339~0x0040C345 本身就明确 `push 0; push 4; call 0x0040BCC0`；
 *   - 标准 ComeOn.exe 中 mode 4 的原始宽高正是 640x480；
 *   - 历史“只改游戏内宽屏”的 EXE 也保留了这条前端 640x480 语义。
 *
 * 这里没有把 640/480 写死成判断常量。expected_width / expected_height 直接读取安装时保存的
 * g_res_mode_original[4]/[5]，这样只要兼容 EXE 仍保持同一代码结构，日志和安全核对就尊重它自己的原始 mode 4。
 */
static void __fastcall strategy_exit_hook(LPVOID self, LPVOID unused_edx)
{
    LONG old_state = -1;
    LONG expected_width;
    LONG expected_height;
    LONG live_width;
    LONG live_height;
    int reset_result = 0;
    BOOL profile_ok;
    BOOL live_matches_frontend;
    char line[448];

    (void)unused_edx;

    if (!g_original_strategy_exit) {
        return;
    }

    /*
     * callsite 0x0040B9E8 只有“旧状态是 Strategy/state 3”时才会执行。
     * 此时 self+0x0C 还没有被 0x00404A50 后面的代码改成新状态，所以可以先把旧值记下来供日志核对。
     */
    if (self) {
        old_state = *(LONG*)((BYTE*)self + 0x0Cu);
    }

    /*
     * 旧 HUD 很快会随着 Strategy 清理失效。先把插件缓存清掉，避免后面的鼠标/JMM 辅助逻辑再引用它。
     */
    g_main_hud_instance = (LPVOID)0;

    /*
     * 原版 Strategy 清理期间可能释放 UI、world、DirectDraw 相关对象。
     * transition guard 会让主 HUD 居中和 Steam delayed JMM 暂停，避免在半析构状态下碰这些对象。
     */
    g_strategy_transition_in_progress = TRUE;
    g_original_strategy_exit(self);

    /*
     * 原版游戏内资源已经清理完，现在才把几处分辨率/JMM 机器码恢复为 FRONTEND 原值。
     * 这一步决定“接下来 mode 4 应该重新得到原生前端宽高”。
     */
    profile_ok = set_frontend_resolution_profile();

    /*
     * RES_MODE_ALL_PATTERN 保存顺序已经由 0x00404D7A 分支闭合：
     *   [0],[1] = mode 6 宽高；
     *   [2],[3] = mode 5 宽高；
     *   [4],[5] = mode 4 / 默认分支宽高。
     * 标准原版这里就是 640x480。
     */
    expected_width = (LONG)g_res_mode_original[4];
    expected_height = (LONG)g_res_mode_original[5];

    /*
     * 只有三项都成立才真正强制回前端：
     *   1. FRONTEND profile 已成功恢复；
     *   2. self 非空；
     *   3. 0x0040BCC0 已经在初始化时通过函数头签名解析成功。
     *
     * 第二个参数固定 mode=4；第三个参数 force=1，专门绕开“当前 mode ID 同样是 4”的原版早退。
     */
    if (profile_ok && self && g_display_mode_apply) {
        /*
         * 原版启动前端在 0x0040C33E 还会先写 self+0x08 = 4，然后才调用 0x0040BCC0。
         * 这个字段不是 live 宽高，而是对象内部保存的“本轮请求/准备使用的模式”。
         * test15 也照着原版顺序写回 4，避免 BaseHeight=600 等情况下刚离开的 gameplay 曾使用 mode 5，
         * 结果虽然 surface 已回 640x480，但对象内部的请求模式仍残留 5，影响后续前端状态或下一轮切换。
         */
        *(LONG*)((BYTE*)self + 0x08u) = 4;
        reset_result = g_display_mode_apply(self, 4, 1);
    }

    /*
     * 现在再读取真实 live 宽高。self 就是 外传 Strategy-enter 进入路径使用的同一个显示/高层对象，
     * 其 +0x228/+0x22C 也是 0x0040BCC0 写入并在前几版日志中已经验证过的当前宽高字段。
     */
    live_width = self ? *(LONG*)((BYTE*)self + DISPLAY_CURRENT_WIDTH_OFFSET) : 0;
    live_height = self ? *(LONG*)((BYTE*)self + DISPLAY_CURRENT_HEIGHT_OFFSET) : 0;

    live_matches_frontend =
        (profile_ok && reset_result != 0 &&
         live_width == expected_width && live_height == expected_height) ? TRUE : FALSE;

    /*
     * 显示重建和尺寸核对都结束后，才解除 transition guard。
     * 后续真正新建的前端 HUD 会继续走 FRONTEND 分支，不会再被 DisplayFix 水平平移或做 Steam gameplay JMM。
     */
    g_strategy_transition_in_progress = FALSE;

    /*
     * 原版 Strategy 清理已经完成，因此无论前端 Surface 重置最终是否成功，
     * 对其它模块来说“GAMEPLAY 已离开”都是真实事实。这里广播统一退出事件，
     * 让未来的手柄、QoL、额外 HUD 模块可以统一释放自己的游戏内状态。
     */
    Runtime_EmitEvent(RUNTIME_EVENT_GAMEPLAY_EXIT, self, (unsigned long)live_width, (unsigned long)live_height);

    if (g_strategy_exit_log_count < 4u) {
        ++g_strategy_exit_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] Strategy退出 原状态=");
        append_int(line, (DWORD)sizeof(line), old_state);
        str_append(line, (DWORD)sizeof(line), profile_ok ? " FRONTEND配置=已恢复" : " FRONTEND配置=失败");
        str_append(line, (DWORD)sizeof(line), " 重置模式=4 结果=");
        append_int(line, (DWORD)sizeof(line), (LONG)reset_result);
        str_append(line, (DWORD)sizeof(line), " 实际=");
        append_int(line, (DWORD)sizeof(line), live_width);
        str_append(line, (DWORD)sizeof(line), "x");
        append_int(line, (DWORD)sizeof(line), live_height);
        str_append(line, (DWORD)sizeof(line), " 目标=");
        append_int(line, (DWORD)sizeof(line), expected_width);
        str_append(line, (DWORD)sizeof(line), "x");
        append_int(line, (DWORD)sizeof(line), expected_height);
        append_runtime_line(line);
    }

    if (!live_matches_frontend) {
        append_runtime_line("[运行] Strategy退出：FRONTEND强制重置失败；标题界面可能仍停留在游戏内Surface");
    }
}

/*
 * 主 HUD vtable +0x58 的视觉居中 hook。
 * 继续保留 v0.2-test1 已经实机确认“视觉居中非常完美”的两次原版布局方案。
 */
static int __fastcall main_hud_layout_hook(LPVOID self, LPVOID unused_edx, LONG x, LONG y)
{
    int result;
    LONG original_x;
    LONG original_y;
    LONG center_delta;
    LONG centered_x;

    (void)unused_edx;

    if (!g_original_main_hud_layout) {
        return 0;
    }

    /*
     * 只要这次调用带着对象，就缓存“最近一次 HUD 实例”。Strategy enter 时会主动把旧缓存清零，
     * 所以 Steam delayed apply 不会把主菜单阶段的旧 HUD 当成新地图 HUD 使用。
     */
    if (self) {
        g_main_hud_instance = self;
    }

    if (!self || x != -1 || y != -1) {
        return g_original_main_hud_layout(self, x, y);
    }

    /* 第一次先让原版按自己的规则完成布局。 */
    result = g_original_main_hud_layout(self, x, y);

    /*
     * Strategy enter/exit 的原版函数内部可能因为 SetDisplayMode/设备重建临时触发这一布局。
     * test12 的手工 Reset 路径已经证明“分辨率切换调用栈里继续访问/重排旧 HUD”风险很高，
     * 所以这段过渡期间只允许原版布局，绝不做 DisplayFix 的平移/JMM。
     */
    if (g_strategy_transition_in_progress) {
        return result;
    }

    /*
     * 这是 test13 对 test11/test12 失败的关键修正：
     * FRONTEND 阶段即使出现相同 HUD 类，也绝对不做宽屏 X 偏移，更绝对不触发 Steam full JMM apply。
     * 只有 0x404A97 的 Strategy 状态机 hook 已经明确切到 GAMEPLAY profile 后，才允许下面的游戏内修复。
     */
    if (!g_gameplay_profile_active) {
        if (g_frontend_hud_skip_log_count < 2u) {
            ++g_frontend_hud_skip_log_count;
            append_runtime_line("[运行] FRONTEND HUD自动布局：保持原版4:3布局；不执行居中/JMM同步");
        }
        return result;
    }

    if (!g_center_main_hud) {
        /* 用户关闭视觉居中时，Steam GUI 同步仍然要在真正 gameplay 中独立工作。 */
        try_steam_delayed_ui_sync(self);
        log_hud_candidate_children(self);
        return result;
    }

    original_x = *(LONG*)((BYTE*)self + UI_OBJECT_X_OFFSET);
    original_y = *(LONG*)((BYTE*)self + UI_OBJECT_Y_OFFSET);
    center_delta = ((LONG)g_target_width - (LONG)g_native_base_width) / 2;

    if (center_delta != 0) {
        centered_x = original_x + center_delta;
        result = g_original_main_hud_layout(self, centered_x, original_y);
    }

    /*
     * Steam/ComeOn.dll 环境缺少非 Steam 自然发生的“第二阶段 UI/JMM 应用”。
     * test10 已经证明，在 HUD/顶层链/资源根成熟后 one-shot 调游戏原版 0x4B35F0 可以修复。
     * test13 只增加一个先决条件：必须已经由 Strategy 状态机正式进入 GAMEPLAY。
     */
    try_steam_delayed_ui_sync(self);
    log_hud_candidate_children(self);

    if (g_hud_layout_log_count < 4u) {
        char line[320];
        ++g_hud_layout_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] HUD布局 对象=");
        append_hex32(line, (DWORD)sizeof(line), (DWORD)self);
        str_append(line, (DWORD)sizeof(line), " 根坐标=");
        append_int(line, (DWORD)sizeof(line), *(LONG*)((BYTE*)self + UI_OBJECT_X_OFFSET));
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), *(LONG*)((BYTE*)self + UI_OBJECT_Y_OFFSET));
        str_append(line, (DWORD)sizeof(line), " 偏移=");
        append_int(line, (DWORD)sizeof(line), center_delta);
        append_runtime_line(line);
    }

    return result;
}

/*
 * 在一个鼠标点下，把 0x09~0x10 里所有“矩形包含该点”的 child ID 追加到日志。
 */
static void append_candidates_at_point(char* line, DWORD line_size, LPVOID self, LONG x, LONG y)
{
    LPVOID child;
    BOOL found = FALSE;

    str_append(line, line_size, " 包含=");

    if (!self) {
        str_append(line, line_size, "无");
        return;
    }

    child = *(LPVOID*)((BYTE*)self + UI_OBJECT_CHILD_HEAD);

    while (child) {
        DWORD control_id = *(DWORD*)((BYTE*)child + UI_OBJECT_CONTROL_ID);
        HudRect rect;

        if (control_id >= 0x09u && control_id <= 0x10u &&
            read_child_rect(child, &rect) &&
            point_in_hud_rect(x, y, &rect)) {
            found = TRUE;
            str_append(line, line_size, "[");
            append_hex32(line, line_size, control_id);
            str_append(line, line_size, child_active_for_diagnostic(child) ? " 激活]" : " 未激活]");
        }

        child = *(LPVOID*)((BYTE*)child + UI_OBJECT_NEXT_OFFSET);
    }

    if (!found) {
        str_append(line, line_size, "无");
    }
}

/*
 * 顶层属性/道具窗口的显示开关虚函数。
 * 原版 0x4C3F53 / 0x4C4000 两条分支都会：
 *   1. 先查询目标窗口当前 active；
 *   2. 把 active 取反；
 *   3. 用目标窗口 vtable+0x1C(new_active, 0) 应用。
 *
 * 所以这里的兜底并不是“自己发明打开窗口的方法”，而是逐字复用原版已经确认的同一个虚函数接口。
 */
typedef void (__thiscall *FnUISetActive)(LPVOID self, LONG enabled, LONG reserved_zero);

/*
 * 安全读取一个顶层 UI 对象当前是否 active。
 * 这些目标窗口都继承自和 HUD child 相同的基础 UI 类，因此 +0x64/+0x68 的语义一致。
 * 这里只读内存，不调用 0x4B1DB0，不会改变 +0xB8 latch。
 */
static BOOL ui_object_active_for_diagnostic(LPVOID object)
{
    if (!object) {
        return FALSE;
    }

    return child_active_for_diagnostic(object);
}

/*
 * 把顶层目标窗口的 active + 矩形追加到日志。
 * 如果对象当前不存在，就明确写 none，避免把“没创建”误判成“创建了但没显示”。
 */
static void append_ui_target_brief(char* line, DWORD line_size, LPVOID object)
{
    HudRect rect;

    if (!object) {
        str_append(line, line_size, "无");
        return;
    }

    str_append(line, line_size, "指针=");
    append_hex32(line, line_size, (DWORD)object);
    str_append(line, line_size, " 激活=");
    append_int(line, line_size, ui_object_active_for_diagnostic(object) ? 1 : 0);

    if (read_child_rect(object, &rect)) {
        str_append(line, line_size, " 矩形=");
        append_int(line, line_size, rect.left);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.top);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.right);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.bottom);
    } else {
        str_append(line, line_size, " 矩形=无效");
    }
}

/*
 * 当原版已经明确命中了 0x0B / 0x0E，但对应目标窗口 active 完全没有变化时，
 * 才调用一次“原版同一个 vtable+0x1C”做兜底。
 *
 * 返回 TRUE 表示确实调用了兜底；FALSE 表示没有动任何游戏状态。
 * 这三个条件缺一不可：
 *   - hit ID 必须就是实机闭合的 0x0B 或 0x0E；
 *   - 对应目标对象必须真实存在；
 *   - 原版事件前后 active 必须完全没变化。
 *
 * 因此如果原版自己已经正确打开/关闭窗口，本函数绝不会再切一次，不会造成“双重反转”。
 */
static BOOL fallback_toggle_top_button_target(DWORD control_id,
                                              LPVOID target,
                                              BOOL before_active,
                                              BOOL after_active)
{
    LPVOID vtable;
    DWORD function_address;
    FnUISetActive set_active;
    LONG desired_active;

    if ((control_id != 0x0Bu && control_id != 0x0Eu) ||
        !target || before_active != after_active) {
        return FALSE;
    }

    vtable = *(LPVOID*)target;
    if (!vtable) {
        return FALSE;
    }

    function_address = *(DWORD*)((BYTE*)vtable + 0x1Cu);
    if (function_address < 0x00400000u || function_address >= 0x00600000u) {
        return FALSE;
    }

    set_active = (FnUISetActive)function_address;
    desired_active = before_active ? 0 : 1;
    set_active(target, desired_active, 0);
    return TRUE;
}

/*
 * 在当前主 HUD 的直属 child 链中按 control ID 找控件。
 *
 * 这里只读 child 链，不修改 self+0xA0 迭代器，也不调用会写 latch 的 0x4B1DB0，
 * 因此可以安全地在“全局鼠标释放”最外层做预判，不会提前改变游戏自己的命中状态。
 */
static LPVOID find_main_hud_child_by_id(LPVOID self, DWORD wanted_id)
{
    LPVOID child;

    if (!self) {
        return (LPVOID)0;
    }

    child = *(LPVOID*)((BYTE*)self + UI_OBJECT_CHILD_HEAD);
    while (child) {
        if (*(DWORD*)((BYTE*)child + UI_OBJECT_CONTROL_ID) == wanted_id) {
            return child;
        }
        child = *(LPVOID*)((BYTE*)child + UI_OBJECT_NEXT_OFFSET);
    }

    return (LPVOID)0;
}

/*
 * 判断当前鼠标点是否正落在 0x0B / 0x0E 两个已实机确认的顶部按钮上。
 *
 * 重要：这里读的是“布局完成后的 child+0x14/+0x18/+0x1C/+0x20”，
 * 也就是和画面最终位置同一套实时矩形；不再使用旧 4:3 坐标，也不自己计算 +delta。
 * 这样 BaseHeight=480、1080 或任意宽高比都走同一套逻辑。
 */
static DWORD identify_top_button_at_point(LPVOID hud, LONG mouse_x, LONG mouse_y, LPVOID* out_child)
{
    static const DWORD IDS[2] = { 0x0Bu, 0x0Eu };
    DWORD i;

    if (out_child) {
        *out_child = (LPVOID)0;
    }

    if (!hud) {
        return 0u;
    }

    for (i = 0u; i < 2u; ++i) {
        LPVOID child = find_main_hud_child_by_id(hud, IDS[i]);
        HudRect rect;

        if (!child || !child_active_for_diagnostic(child)) {
            continue;
        }

        if (read_child_rect(child, &rect) && point_in_hud_rect(mouse_x, mouse_y, &rect)) {
            if (out_child) {
                *out_child = child;
            }
            return IDS[i];
        }
    }

    return 0u;
}

/*
 * v0.3-test8：只在真正进入“世界鼠标按下”函数之前阻止属性/道具按钮的点击穿透。
 *
 * 这和 test7 最大的区别是：这里已经处在 0x004C78C0 返回 0 之后。也就是说游戏自己的 UI 按下分派
 * 已经完整执行过，DisplayFix 不需要、也绝不能再包装那条存在隐藏 ESI 语义的路径。
 *
 * 此桥接只做三件事：
 *   1. 用 callsite 原本传给世界函数的 mouse_x / mouse_y 检查当前主 HUD 的 0x0B / 0x0E 实时矩形；
 *   2. 命中两个特殊按钮时直接返回，相当于“只跳过这一次 0x00482790”；
 *   3. 其他位置完整调用原版 0x00482790。
 *
 * 为什么这里不再额外 GetCursorPos：test7 实机日志已经证明 callsite 的 arg 坐标和游戏 IAT GetCursorPos
 * 在这些点击上完全一致。直接使用原参数更窄、更少副作用，也不会在世界输入入口额外调用 USER32。
 */
static void __fastcall world_mouse_press_hook(LPVOID self, LPVOID unused_edx,
                                               LONG event_type, LONG mouse_x, LONG mouse_y)
{
    LPVOID hit_child = (LPVOID)0;
    DWORD hit_id = 0u;
    BOOL block_world = FALSE;

    (void)unused_edx;

    if (g_main_hud_instance) {
        hit_id = identify_top_button_at_point(g_main_hud_instance, mouse_x, mouse_y, &hit_child);
    }

    if (hit_id == 0x0Bu || hit_id == 0x0Eu) {
        block_world = TRUE;
    }

    /*
     * test8b 为了调查曾记录大量普通地图 WORLD press；Steam 性能测试阶段这些磁盘 I/O 本身会成为干扰变量。
     * test9 只在真正命中 0x0B/0x0E、也就是我们确实阻止了一次点击穿透时记录。
     */
    if (block_world && g_world_press_log_count < 8u) {
        char line[640];
        ++g_world_press_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] 世界鼠标按下 参数坐标=");
        append_int(line, (DWORD)sizeof(line), mouse_x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), mouse_y);
        str_append(line, (DWORD)sizeof(line), " 顶层ID=");
        append_hex32(line, (DWORD)sizeof(line), hit_id);
        str_append(line, (DWORD)sizeof(line), " 子控件=");
        append_child_brief(line, (DWORD)sizeof(line), hit_child);
        str_append(line, (DWORD)sizeof(line), " 阻止世界输入=");
        append_int(line, (DWORD)sizeof(line), block_world ? 1 : 0);
        append_runtime_line(line);
    }

    if (block_world) {
        return;
    }

    if (g_original_world_mouse_press) {
        g_original_world_mouse_press(self, event_type, mouse_x, mouse_y);
    }
}

/*
 * v0.3-test6 已实机成功、test8 继续保留：在 UI manager 全局鼠标释放分派层做“原版优先、失败才兜底”。
 *
 * 为什么这一层比之前的 +0x24 hook 更可靠：
 *   - 0x0040CF96 的 call 一定发生在鼠标释放检测成立之后；
 *   - 它还没决定这次释放最终能不能进入主 HUD，所以不会遇到“事件根本没送到 HUD，兜底代码也永远不执行”；
 *   - mouse_x/mouse_y 就来自同一帧的 GetCursorPos，和原版 UI manager 使用的是同一套游戏逻辑坐标。
 *
 * 安全策略：
 *   1. 调原版之前只识别“鼠标是否落在当前 0x0B/0x0E 的真实矩形”，并拍目标窗口 active 快照；
 *   2. 完整调用原版 0x4B4560，任何正常行为都优先保留；
 *   3. 如果原版已经让目标窗口 active 改变，说明它自己成功，本插件什么都不做；
 *   4. 只有“鼠标确实在两个按钮之一 + 对应窗口存在 + active 完全没变化”才调用原版 vtable+0x1C 一次；
 *   5. 兜底成功后返回 1，保持原版“这个释放事件已经被 UI 处理”的返回语义，避免释放路径再落入后续备用处理；角色移动的按下穿透由独立 0x473F10 callsite guard 负责。
 */
static int __fastcall ui_manager_mouse_release_hook(LPVOID self, LPVOID unused_edx,
                                                     LONG event_type, LONG mouse_x, LONG mouse_y)
{
    int original_result;
    POINT point;
    LONG test_x = mouse_x;
    LONG test_y = mouse_y;
    BOOL have_cursor = FALSE;
    LPVOID hit_child = (LPVOID)0;
    DWORD hit_id = 0u;
    LPVOID target_before = (LPVOID)0;
    LPVOID target_after = (LPVOID)0;
    BOOL active_before = FALSE;
    BOOL active_after = FALSE;
    BOOL fallback_used = FALSE;

    (void)unused_edx;

    if (!g_original_ui_manager_mouse_release) {
        return 0;
    }

    /*
     * 只在真正的鼠标释放事件 event_type==1 时考虑两个特殊按钮。
     * 其他事件原样交给游戏，避免误伤拖拽、按下、移动等路径。
     */
    if (event_type == 1 && g_main_hud_instance) {
        /*
         * 与游戏自己的 0x4B3380 一样，优先再读一次 ComeOn.exe 的 GetCursorPos IAT。
         * cnc-ddraw 如果把桌面坐标映射成逻辑分辨率坐标，我们看到的就会和原版完全一致。
         * 如果 API 失败，才退回 callsite 已经传进来的 mouse_x/mouse_y。
         */
        if (GAME_GetCursorPos && GAME_GetCursorPos(&point)) {
            test_x = point.x;
            test_y = point.y;
            have_cursor = TRUE;
        }

        hit_id = identify_top_button_at_point(g_main_hud_instance, test_x, test_y, &hit_child);

        if (hit_id == 0x0Bu && g_top_button_0b_target_slot) {
            target_before = *g_top_button_0b_target_slot;
        } else if (hit_id == 0x0Eu && g_top_button_0e_target_slot) {
            target_before = *g_top_button_0e_target_slot;
        }

        active_before = ui_object_active_for_diagnostic(target_before);
    }

    /* 游戏自己的 UI 分派永远先跑。 */
    original_result = g_original_ui_manager_mouse_release(self, event_type, mouse_x, mouse_y);

    if (hit_id == 0x0Bu && g_top_button_0b_target_slot) {
        target_after = *g_top_button_0b_target_slot;
    } else if (hit_id == 0x0Eu && g_top_button_0e_target_slot) {
        target_after = *g_top_button_0e_target_slot;
    }

    active_after = ui_object_active_for_diagnostic(target_after);

    /*
     * 只有对象真实存在，而且原版执行前后 active 没有发生变化，才做最后一次原版式切换。
     * target_after 优先，因为原版事件有理论可能在过程中替换全局对象实例。
     */
    if ((hit_id == 0x0Bu || hit_id == 0x0Eu) && target_after && active_before == active_after) {
        fallback_used = fallback_toggle_top_button_target(hit_id, target_after,
                                                          active_before, active_after);
    }

    /*
     * 普通地图/普通 HUD 的每次释放不再写盘；只有两个特殊按钮参与判断时才记录，
     * 这样 Steam 版跑动和战斗时不会因为诊断日志持续 CreateFile/WriteFile 而增加额外抖动。
     */
    if (g_global_release_log_count < 8u && event_type == 1 &&
        (hit_id == 0x0Bu || hit_id == 0x0Eu || fallback_used)) {
        char line[1024];
        ++g_global_release_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] 全局鼠标释放 参数坐标=");
        append_int(line, (DWORD)sizeof(line), mouse_x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), mouse_y);
        str_append(line, (DWORD)sizeof(line), " 测试坐标=");
        append_int(line, (DWORD)sizeof(line), test_x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), test_y);
        str_append(line, (DWORD)sizeof(line), have_cursor ? " 光标=可用" : " 光标=回退到参数");
        str_append(line, (DWORD)sizeof(line), " 顶层ID=");
        append_hex32(line, (DWORD)sizeof(line), hit_id);
        str_append(line, (DWORD)sizeof(line), " 子控件=");
        append_child_brief(line, (DWORD)sizeof(line), hit_child);
        str_append(line, (DWORD)sizeof(line), " 目标前=");
        append_ui_target_brief(line, (DWORD)sizeof(line), target_before);
        str_append(line, (DWORD)sizeof(line), " 目标后=");
        append_ui_target_brief(line, (DWORD)sizeof(line), target_after);
        str_append(line, (DWORD)sizeof(line), " 原版结果=");
        append_int(line, (DWORD)sizeof(line), original_result);
        str_append(line, (DWORD)sizeof(line), " 兜底=");
        append_int(line, (DWORD)sizeof(line), fallback_used ? 1 : 0);
        append_runtime_line(line);
    }

    /* 如果我们确实补处理了按钮，就把这个鼠标释放标成已被 UI 消费。 */
    if (fallback_used) {
        return 1;
    }

    return original_result;
}

/*
 * 安装 0x0040CFFB -> 0x00482790 的世界鼠标按下 callsite hook。
 *
 * 这里故意不再触碰 0x0040CFDD -> 0x004C78C0。test7 的实机回归已经证明后者不能被普通 C wrapper
 * 安全包裹；它的 ESI 隐式输入细节已写入“逆向工程知识库.md”。
 *
 * 安装步骤：
 *   1. 用 WORLD_MOUSE_PRESS_CALLSITE_PATTERN 唯一定位 0x004060D6 一带；
 *   2. E8 CALL 位于签名 +21；
 *   3. 解码原目标并验证其函数头确实是 0x00482790 的 SEH/寄存器保存结构；
 *   4. 只改 E8 后 4 字节 rel32，让调用先进入 world_mouse_press_hook。
 */
static BOOL install_world_mouse_press_hook(const TextRegion* region)
{
    BYTE* site;
    BYTE* call_instruction;
    BYTE* target;

    site = find_unique_pattern(region,
                               WORLD_MOUSE_PRESS_CALLSITE_PATTERN,
                               WORLD_MOUSE_PRESS_CALLSITE_MASK,
                               (DWORD)sizeof(WORLD_MOUSE_PRESS_CALLSITE_PATTERN));
    if (!site) {
        return FALSE;
    }

    call_instruction = site + 21u;
    if (call_instruction[0] != 0xE8) {
        return FALSE;
    }

    target = decode_rel32_target(call_instruction);
    if (!target || target < region->start || target + 26u > region->start + region->size) {
        return FALSE;
    }

    /*
     * 0x00482790 函数头：
     *   64 A1 00000000 6A FF 68 ???????? 50 A1 ???????? 64 89 25 00000000
     * 只验证稳定 opcode/零常量，SEH 记录地址和游戏全局地址保持通配。
     */
    if (target[0] != 0x64 || target[1] != 0xA1 ||
        target[2] != 0x00 || target[3] != 0x00 || target[4] != 0x00 || target[5] != 0x00 ||
        target[6] != 0x6A || target[7] != 0xFF || target[8] != 0x68 ||
        target[13] != 0x50 || target[14] != 0xA1 ||
        target[19] != 0x64 || target[20] != 0x89 || target[21] != 0x25 ||
        target[22] != 0x00 || target[23] != 0x00 || target[24] != 0x00 || target[25] != 0x00) {
        return FALSE;
    }

    /*
     * test12 曾从签名开头 `8B 0D <absolute-address>` 解析 world 全局槽并把它当 gameplay gate。
     * 实机已经证明这个全局槽在主菜单也可能非空，所以 test13 不再读取/使用它。
     * 这里现在只保留已经实机通过的“0x0B/0x0E 防止世界点击穿透”职责。
     */
    g_original_world_mouse_press = (FnWorldMousePress)target;
    if (!patch_rel32_call(call_instruction, (LPVOID)&world_mouse_press_hook)) {
        g_original_world_mouse_press = (FnWorldMousePress)0;
        return FALSE;
    }

    return TRUE;
}

/*
 * 安装 test15 使用的 Strategy 状态进入/离开 callsite hook。
 *
 * 为什么改 callsite 而不是直接改 0x0040B9C0 整个状态函数：
 *   - 0x0040BA5F 只会在“新状态 == 3”分支执行，语义就是 BeforeStrategy；
 *   - 0x0040B9E8 只会在“旧状态 == 3”清理分支执行，语义就是 AfterStrategy；
 *   - 两个 callsite 都把同一个 self 放在 ECX，桥接非常简单；
 *   - 只替换 E8 rel32，不改 jump table、不改状态值，也不复制游戏自己的状态机逻辑。
 *
 * 安装前还会验证两个原目标函数：
 *   外传 Strategy-enter 必须读取 self+0x280、写 self+0x08，然后 call 原版显示模式函数；
 *   外传 Strategy-exit 必须是 mov ecx,<global> / jmp <cleanup> 的小尾调用包装。
 * 任意一步不匹配就整组拒绝安装，不留下“只装一半”的生命周期补丁。
 */
static BOOL install_strategy_state_hooks(const TextRegion* region)
{
    BYTE* exit_site;
    BYTE* enter_site;
    BYTE* exit_call;
    BYTE* enter_call;
    BYTE* enter_target;
    BYTE* exit_target;

    /*
     * 外传不直接复用本传状态函数的固定偏移，因为外传在同一状态机中多保存了 EDI，
     * 并在 BeforeStrategy 前增加了额外初始化。这里改为分别搜索“进入”和“离开”的完整语义上下文。
     */
    exit_site = find_unique_pattern(region,
                                    STRATEGY_EXIT_CALLSITE_PATTERN,
                                    STRATEGY_EXIT_CALLSITE_MASK,
                                    (DWORD)sizeof(STRATEGY_EXIT_CALLSITE_PATTERN));
    enter_site = find_unique_pattern(region,
                                     STRATEGY_ENTER_CALLSITE_PATTERN,
                                     STRATEGY_ENTER_CALLSITE_MASK,
                                     (DWORD)sizeof(STRATEGY_ENTER_CALLSITE_PATTERN));
    if (!exit_site || !enter_site) {
        return FALSE;
    }

    /*
     * exit_site 的第二条 CALL 才是外传 AfterStrategy 真正的 Strategy-exit 包装函数：
     *   8B CE / E8 <第一清理> / 8B CE / E8 <Strategy-exit>
     */
    exit_call = exit_site + 9u;

    /*
     * enter_site 中，第一条 CALL 是 BeforeStrategy 之前的准备函数；
     * 中间一次日志调用之后，第二个 `8B CE / E8` 才是 Strategy-enter。
     */
    enter_call = enter_site + 27u;

    if (exit_call[0] != 0xE8 || enter_call[0] != 0xE8) {
        return FALSE;
    }

    exit_target = decode_rel32_target(exit_call);
    enter_target = decode_rel32_target(enter_call);

    if (!enter_target || !exit_target ||
        enter_target < region->start || enter_target + 0x20u > region->start + region->size ||
        exit_target < region->start || exit_target + 0x0Au > region->start + region->size) {
        return FALSE;
    }

    /*
     * 外传 Strategy-enter 已静态闭合为：
     *   56             push esi
     *   8B F1          mov esi,ecx
     *   6A 00          push 0            ; force
     *   8B 86 80 02... mov eax,[esi+280h]
     *   50             push eax           ; mode
     *   89 46 08       mov [esi+08h],eax
     *   E8 ...         call 显示模式函数
     * 这与本传最终成功方案的关键语义完全相同。
     */
    if (enter_target[0] != 0x56 ||
        enter_target[1] != 0x8B || enter_target[2] != 0xF1 ||
        enter_target[3] != 0x6A || enter_target[4] != 0x00 ||
        enter_target[5] != 0x8B || enter_target[6] != 0x86 ||
        enter_target[7] != 0x80 || enter_target[8] != 0x02 || enter_target[9] != 0x00 || enter_target[10] != 0x00 ||
        enter_target[11] != 0x50 ||
        enter_target[12] != 0x89 || enter_target[13] != 0x46 || enter_target[14] != 0x08 ||
        enter_target[15] != 0xE8) {
        return FALSE;
    }

    /* 外传 Strategy-exit 同样是 `mov ecx,<global>; jmp <cleanup>` 的小尾调用包装。 */
    if (exit_target[0] != 0xB9 || exit_target[5] != 0xE9) {
        return FALSE;
    }

    /* 保存 enter 函数 `push 0` 的立即数字节；进入游戏时只临时改成 1，返回后马上还原。 */
    g_strategy_enter_force_immediate = enter_target + 4u;
    g_strategy_enter_force_original = enter_target[4];

    g_original_strategy_enter = (FnStrategyStateStep)enter_target;
    g_original_strategy_exit = (FnStrategyStateStep)exit_target;

    if (!patch_rel32_call(enter_call, (LPVOID)&strategy_enter_hook)) {
        g_original_strategy_enter = (FnStrategyStateStep)0;
        g_original_strategy_exit = (FnStrategyStateStep)0;
        g_strategy_enter_force_immediate = (BYTE*)0;
        g_strategy_enter_force_original = 0u;
        return FALSE;
    }

    if (!patch_rel32_call(exit_call, (LPVOID)&strategy_exit_hook)) {
        patch_rel32_call(enter_call, (LPVOID)enter_target);
        g_original_strategy_enter = (FnStrategyStateStep)0;
        g_original_strategy_exit = (FnStrategyStateStep)0;
        g_strategy_enter_force_immediate = (BYTE*)0;
        g_strategy_enter_force_original = 0u;
        return FALSE;
    }

    g_strategy_state_hooks_installed = TRUE;
    return TRUE;
}

/*
 * 安装 0x0040CF96 的全局鼠标释放 callsite hook。
 *
 * 不写死 0x0040CF96 / 0x004C7930：
 *   - 先用上面的长签名唯一定位 callsite；
 *   - 再解码 E8 rel32 得到真实目标；
 *   - 验证目标函数头和 0x4B4560 已确认结构一致；
 *   - 最后只替换 E8 后面的 rel32。
 *
 * 这个签名已经离线在外传原版、1280/12802/1366/1440/1600/1680/1920/19202 历史宽屏 EXE 和 Steam EXE 上全部唯一命中。
 */
static BOOL install_global_mouse_release_hook(const TextRegion* region)
{
    BYTE* site;
    BYTE* call_instruction;
    BYTE* target;

    site = find_unique_pattern(region,
                               GLOBAL_MOUSE_RELEASE_CALLSITE_PATTERN,
                               GLOBAL_MOUSE_RELEASE_CALLSITE_MASK,
                               (DWORD)sizeof(GLOBAL_MOUSE_RELEASE_CALLSITE_PATTERN));
    if (!site) {
        return FALSE;
    }

    /* 签名中 E8 位于 +22。 */
    call_instruction = site + 22u;
    if (call_instruction[0] != 0xE8) {
        return FALSE;
    }

    target = decode_rel32_target(call_instruction);
    if (!target || target < region->start || target + 18u > region->start + region->size) {
        return FALSE;
    }

    /*
     * 0x4B4560 函数头：
     *   56 57 8B F1 33 FF E8 ???? 85 C0 74 1C 8B 54 24 14
     * 中间 E8 的 rel32 会随位置变化，所以只检查两边固定字节。
     */
    if (target[0] != 0x56 || target[1] != 0x57 ||
        target[2] != 0x8B || target[3] != 0xF1 ||
        target[4] != 0x33 || target[5] != 0xFF ||
        target[6] != 0xE8 ||
        target[11] != 0x85 || target[12] != 0xC0 ||
        target[13] != 0x74 || target[14] != 0x1C ||
        target[15] != 0x8B || target[16] != 0x54 || target[17] != 0x24) {
        return FALSE;
    }

    g_original_ui_manager_mouse_release = (FnUiManagerMouseRelease)target;
    if (!patch_rel32_call(call_instruction, (LPVOID)&ui_manager_mouse_release_hook)) {
        g_original_ui_manager_mouse_release = (FnUiManagerMouseRelease)0;
        return FALSE;
    }

    return TRUE;
}

/*
 * 历史 +0x24 诊断桥接（v0.3-test8 不再安装）：
 *   - 仍然完整先调用游戏原版事件；
 *   - 根据 v0.3-test4 实机日志，把顶部两个按钮认定为 0x0B / 0x0E；
 *   - 记录它们控制的两个顶层窗口在点击前后的 active / 矩形；
 *   - 只有“原版明确命中按钮，但窗口状态没变化”时，才复用原版 vtable+0x1C 做一次兜底。
 *
 * 其他 0x09~0x10 控件完全不修改。
 */
static void __fastcall main_hud_event_hook(LPVOID self, LPVOID unused_edx,
                                           LONG event_type, LONG mouse_x, LONG mouse_y)
{
    POINT point;
    BOOL have_point = FALSE;
    LPVOID hit_child = (LPVOID)0;
    DWORD hit_id = 0xFFFFFFFFu;
    LPVOID target_0b_before = (LPVOID)0;
    LPVOID target_0e_before = (LPVOID)0;
    BOOL active_0b_before = FALSE;
    BOOL active_0e_before = FALSE;
    BOOL active_0b_after = FALSE;
    BOOL active_0e_after = FALSE;
    BOOL fallback_used = FALSE;
    BOOL should_log = FALSE;

    (void)unused_edx;

    if (!g_original_main_hud_event) {
        return;
    }

    if (GAME_GetCursorPos) {
        have_point = GAME_GetCursorPos(&point);
    }

    /*
     * 在原版事件执行前先拍一张目标窗口状态快照。
     * 这里只从解析出来的“全局指针槽”读取当前对象，不创建对象，也不改变它。
     */
    if (g_top_button_0b_target_slot) {
        target_0b_before = *g_top_button_0b_target_slot;
        active_0b_before = ui_object_active_for_diagnostic(target_0b_before);
    }
    if (g_top_button_0e_target_slot) {
        target_0e_before = *g_top_button_0e_target_slot;
        active_0e_before = ui_object_active_for_diagnostic(target_0e_before);
    }

    /* 游戏自己的事件逻辑永远先执行。 */
    g_original_main_hud_event(self, event_type, mouse_x, mouse_y);

    if (event_type != 1 || !self) {
        return;
    }

    /*
     * 日志只限前 32 次鼠标释放，但“顶部按钮功能兜底”不能跟着日志限次失效。
     * should_log 只控制是否写盘；下面的 hit 判断和 fallback 在整个游戏会话里始终有效。
     */
    if (g_hud_event_log_count < 32u) {
        ++g_hud_event_log_count;
        should_log = TRUE;
    }

    hit_child = *(LPVOID*)((BYTE*)self + UI_OBJECT_HIT_CHILD);
    if (hit_child) {
        hit_id = *(DWORD*)((BYTE*)hit_child + UI_OBJECT_CONTROL_ID);
    }

    /*
     * 原版事件执行完后再次读取 active。
     * 如果对象指针本身被替换了，也会在日志中分别显示 before / after 指针，便于识别生命周期问题。
     */
    if (g_top_button_0b_target_slot && *g_top_button_0b_target_slot) {
        active_0b_after = ui_object_active_for_diagnostic(*g_top_button_0b_target_slot);
    }
    if (g_top_button_0e_target_slot && *g_top_button_0e_target_slot) {
        active_0e_after = ui_object_active_for_diagnostic(*g_top_button_0e_target_slot);
    }

    /*
     * 只对当前真正命中的顶部按钮检查兜底条件。
     * target 使用“原版事件执行后的当前全局对象”，避免生命周期切换后还去碰旧指针。
     */
    if (hit_id == 0x0Bu && g_top_button_0b_target_slot && *g_top_button_0b_target_slot) {
        LPVOID target = *g_top_button_0b_target_slot;
        fallback_used = fallback_toggle_top_button_target(hit_id, target,
                                                          active_0b_before, active_0b_after);
        if (fallback_used) {
            active_0b_after = ui_object_active_for_diagnostic(target);
        }
    } else if (hit_id == 0x0Eu && g_top_button_0e_target_slot && *g_top_button_0e_target_slot) {
        LPVOID target = *g_top_button_0e_target_slot;
        fallback_used = fallback_toggle_top_button_target(hit_id, target,
                                                          active_0e_before, active_0e_after);
        if (fallback_used) {
            active_0e_after = ui_object_active_for_diagnostic(target);
        }
    }

    if (should_log) {
        char line[1536];
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] HUD鼠标释放 参数坐标=");
        append_int(line, (DWORD)sizeof(line), mouse_x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), mouse_y);

        if (have_point) {
            str_append(line, (DWORD)sizeof(line), " 光标=");
            append_int(line, (DWORD)sizeof(line), point.x);
            str_append(line, (DWORD)sizeof(line), ",");
            append_int(line, (DWORD)sizeof(line), point.y);
        } else {
            str_append(line, (DWORD)sizeof(line), " 光标=不可用");
        }

        str_append(line, (DWORD)sizeof(line), " 命中=");
        append_child_brief(line, (DWORD)sizeof(line), hit_child);

        if (have_point) {
            append_candidates_at_point(line, (DWORD)sizeof(line), self, point.x, point.y);
        }

        str_append(line, (DWORD)sizeof(line), " 目标0B前=");
        append_ui_target_brief(line, (DWORD)sizeof(line), target_0b_before);
        str_append(line, (DWORD)sizeof(line), " 目标0B后=");
        if (g_top_button_0b_target_slot) {
            append_ui_target_brief(line, (DWORD)sizeof(line), *g_top_button_0b_target_slot);
        } else {
            str_append(line, (DWORD)sizeof(line), "槽不可用");
        }

        str_append(line, (DWORD)sizeof(line), " 目标0E前=");
        append_ui_target_brief(line, (DWORD)sizeof(line), target_0e_before);
        str_append(line, (DWORD)sizeof(line), " 目标0E后=");
        if (g_top_button_0e_target_slot) {
            append_ui_target_brief(line, (DWORD)sizeof(line), *g_top_button_0e_target_slot);
        } else {
            str_append(line, (DWORD)sizeof(line), "槽不可用");
        }

        str_append(line, (DWORD)sizeof(line), " 兜底=");
        append_int(line, (DWORD)sizeof(line), fallback_used ? 1 : 0);
        append_runtime_line(line);
    }
}

/*
 * 解析 "16:9"、"3:2"、"3840:2160" 这种任意比例字符串。
 * 返回 TRUE 表示成功，并把冒号左右两边写到 numerator / denominator。
 */
static BOOL parse_ratio(const char* text, DWORD* numerator, DWORD* denominator)
{
    DWORD left = 0;
    DWORD right = 0;
    DWORD i = 0;
    BOOL have_left = FALSE;
    BOOL have_right = FALSE;

    if (!text || !numerator || !denominator) {
        return FALSE;
    }

    while (text[i] >= '0' && text[i] <= '9') {
        have_left = TRUE;
        left = left * 10u + (DWORD)(text[i] - '0');
        ++i;
    }

    if (!have_left || text[i] != ':') {
        return FALSE;
    }

    ++i;

    while (text[i] >= '0' && text[i] <= '9') {
        have_right = TRUE;
        right = right * 10u + (DWORD)(text[i] - '0');
        ++i;
    }

    if (!have_right || text[i] != '\0' || left == 0 || right == 0) {
        return FALSE;
    }

    *numerator = left;
    *denominator = right;
    return TRUE;
}

/*
 * 根据 BaseHeight 和宽高比得到逻辑宽度，并把最终宽度对齐到最接近的 8 像素倍数。
 *
 * 为什么 stripe1 不再只做“偶数对齐”：
 *   用户已经实测 BaseHeight=480 时，16:9、32:9、8:9、40:9 都会在最右侧留下竖条，
 *   而 24:9 没有。旧算法得到的 TargetWidth 正好分别是 854、1708、428、2134、1280。
 *   前四个宽度除以 8 都有余数，只有 1280 能被 8 整除。
 *
 * 这非常像老游戏内部某段横向清屏/扫描/块处理按 8 像素为一组：
 *   Surface 本身可以创建成任意偶数宽度，但最后不足 8 像素的一小段没有被完整刷新，
 *   于是只在最右侧形成历史竖条。stripe1 先用最小变量实验这个假设。
 *
 * 这里不是简单“一律向上补到 8”：那样会让某些超宽比例比必要值更宽。
 * 我们直接从精确的有理数结果选择距离最近的 8 像素倍数。
 * 例如：
 *   480 × 16 / 9 = 853.333... -> 最近的 8 倍数是 856；
 *   480 × 32 / 9 = 1706.666... -> 最近的 8 倍数是 1704；
 *   480 × 24 / 9 = 1280       -> 本来就是 8 倍数，保持 1280。
 *
 * 这样既验证 8 像素块假设，又尽量减少对原始宽高比的误差。
 */
static DWORD calculate_target_width(DWORD base_height, DWORD aspect_width, DWORD aspect_height)
{
    DWORD product;
    DWORD floor_width;
    DWORD remainder;
    DWORD block_base;
    DWORD block_offset;
    DWORD width;

    if (base_height == 0 || aspect_width == 0 || aspect_height == 0) {
        return 0;
    }

    /*
     * BaseHeight 已经解除人为上限，但这个 ASI 故意不链接 C 运行库。
     * 在 32 位 x86 上直接使用 64 位除法会让编译器引入 __aulldiv 之类的 CRT helper，
     * 从而破坏“零 CRT / 零额外依赖”的项目目标。
     *
     * 所以这里继续使用原版的 32 位乘法溢出检查：
     *   base_height <= 0x7FFFFFFE / aspect_width
     * 才允许计算 base_height * aspect_width。
     */
    if (base_height > (0x7FFFFFFEu / aspect_width)) {
        return 0;
    }

    product = base_height * aspect_width;
    floor_width = product / aspect_height;
    remainder = product % aspect_height;

    /*
     * floor_width 是理想宽度向下取整后的整数部分。
     * 把它拆成：
     *   block_base   = 前一个 8 像素边界；
     *   block_offset = 当前整数宽度距离这个边界有几像素（0~7）。
     *
     * “离哪个 8 像素边界更近”其实只需要看理想宽度是否越过 block_base+4 这个中点：
     *   offset 0~3：一定更靠近下面的 8 倍数；
     *   offset 5~7：一定更靠近上面的 8 倍数；
     *   offset 4：正好处在中点或中点右侧，选择上面的 8 倍数。
     *
     * 这样完全不需要 64 位乘除，也不会引入 CRT helper。
     */
    block_base = floor_width & ~7u;
    block_offset = floor_width & 7u;

    if (block_base == 0u) {
        /*
         * 极端窄比例可能让“最近的下方 8 倍数”变成 0。
         * 游戏不可能使用 0 像素宽 Surface，所以这种边界至少保留 8 像素。
         */
        width = 8u;
    } else if (block_offset < 4u) {
        width = block_base;
    } else {
        /*
         * offset==4 且 remainder==0 时，上下两个 8 倍数距离完全相同。
         * 这里沿用旧算法“相等时偏向不缩窄”的习惯，选择上方边界。
         * remainder>0 时理想宽度已经越过中点，上方边界本来就更近。
         */
        (void)remainder;
        if (block_base > (0x7FFFFFFEu - 8u)) {
            return 0;
        }
        width = block_base + 8u;
    }

    /*
     * ComeOn.exe 内部大量地方把宽高当作 32 位有符号 LONG 使用。
     * 这里不是设置画质上限，只拒绝 0 或已经越过正 LONG 范围的数学结果。
     */
    if (width == 0u || width > 0x7FFFFFFEu) {
        return 0;
    }

    return width;
}

/*
 * 解析动态分辨率所需的三组代码位置，但 test11 初始化阶段**不立刻写入目标宽高**。
 *
 * 旧版函数名沿用 apply_dynamic_resolution，是为了减少大面积重命名带来的审阅噪音；
 * test11 里的真实职责已经变成：
 *   1. 唯一定位模式派发与两处分辨率映射；
 *   2. 保存当前 EXE 自己的原始立即数；
 *   3. 解析并验证原版显示模式函数 0x404D30；
 *   4. 保存 TargetWidth/TargetHeight，等待游戏自己的 Strategy 状态 3 进入 callsite 才切 GAMEPLAY profile。
 *
 * 返回 TRUE 代表这些运行时 profile 的必要结构全部闭合；任意签名缺失/重复就拒绝启用。
 */
static BOOL apply_dynamic_resolution(const TextRegion* region, DWORD target_width, DWORD target_height)
{
    BYTE* mode;
    BYTE* map1;
    BYTE* map2;
    BYTE* function_start;

    /*
     * test11 这里不再“初始化时立刻改宽高”。
     * 我们先把三个唯一签名找出来、保存当前 EXE 的原始立即数，并解析真正的 0x404D30 显示模式函数。
     * 主菜单/动画阶段保持这些原始字节不变；真正进入 Strategy 状态 3 前，状态机 hook 才切到 GAMEPLAY profile。
     */
    mode = find_unique_pattern(region,
                               RES_MODE_ALL_PATTERN,
                               RES_MODE_ALL_MASK,
                               (DWORD)sizeof(RES_MODE_ALL_PATTERN));

    map1 = find_unique_pattern(region,
                               RES_MAP1_PATTERN,
                               RES_MAP1_MASK,
                               (DWORD)sizeof(RES_MAP1_PATTERN));

    map2 = find_unique_pattern(region,
                               RES_MAP2_PATTERN,
                               RES_MAP2_MASK,
                               (DWORD)sizeof(RES_MAP2_PATTERN));

    if (!mode || !map1 || !map2) {
        return FALSE;
    }

    /* 保存分辨率模式派发里 6 个会被 GAMEPLAY profile 改写的立即数。 */
    g_res_mode_original[0] = read_u32(mode + 17u);
    g_res_mode_original[1] = read_u32(mode + 27u);
    g_res_mode_original[2] = read_u32(mode + 39u);
    g_res_mode_original[3] = read_u32(mode + 49u);
    g_res_mode_original[4] = read_u32(mode + 61u);
    g_res_mode_original[5] = read_u32(mode + 71u);

    /* 保存第一处分辨率映射中的原生 640/800 比较、隐藏宽度和隐藏高度。 */
    g_res_map1_original[0] = read_u32(map1 + 1u);
    g_res_map1_original[1] = read_u32(map1 + 8u);
    g_res_map1_original[2] = read_u32(map1 + 15u);
    g_res_map1_original[3] = read_u32(map1 + 24u);

    /* 保存第二处分辨率映射中的原生 640/800 比较、隐藏宽高。 */
    g_res_map2_original[0] = read_u32(map2 + 1u);
    g_res_map2_original[1] = read_u32(map2 + 8u);
    g_res_map2_original[2] = read_u32(map2 + 15u);
    g_res_map2_original[3] = read_u32(map2 + 34u);
    g_res_map2_original[4] = read_u32(map2 + 39u);

    g_res_mode_patch = mode;
    g_res_map1_patch = map1;
    g_res_map2_patch = map2;

    /*
     * RES_MODE_ALL_PATTERN 从 0x404D7A 开始；已确认的 SetDisplayMode 包装函数从它前面 0x4A 字节的
     * 0x404D30 开始。这里继续用函数头机器码做一次结构验证，避免单纯依赖“减常数”猜函数。
     */
    function_start = mode - 0x4Au;
    if (function_start[0] != 0x64 || function_start[1] != 0xA1 ||
        function_start[2] != 0x00 || function_start[3] != 0x00 ||
        function_start[4] != 0x00 || function_start[5] != 0x00 ||
        function_start[6] != 0x6A || function_start[7] != 0xFF ||
        function_start[8] != 0x68 ||
        function_start[13] != 0x50 ||
        function_start[14] != 0x8B || function_start[15] != 0x44 ||
        function_start[16] != 0x24 || function_start[17] != 0x10) {
        g_res_mode_patch = (BYTE*)0;
        g_res_map1_patch = (BYTE*)0;
        g_res_map2_patch = (BYTE*)0;
        return FALSE;
    }

    g_display_mode_apply = (FnDisplayModeApply)function_start;

    /* 参数先写入全局；真正的 GAMEPLAY profile 后面会用它们。 */
    g_target_width = target_width;
    g_target_height = target_height;

    return TRUE;
}

/*
 * 解析 JMM 布局选择器，并保存原版 640/800 比较值。
 *
 * test10 以前会在初始化时直接把 target_width 写进这两条比较；test11 不能再这样做，
 * 因为前端/动画阶段必须继续按原版 640x480 生命周期工作。
 *
 * 真正进入 GAMEPLAY profile 后才按以下规则临时改写：
 *   BaseHeight < 600  -> TargetWidth 匹配 JMMDL.txt；
 *   BaseHeight >=600 -> 禁用第一条 640 比较，让 TargetWidth 匹配 JMMDL800.txt。
 *
 * 返回前端时 set_frontend_resolution_profile() 会把这里保存的原值完整恢复。
 */
static BOOL apply_jmm_layout_selection(const TextRegion* region, DWORD target_width, DWORD base_height)
{
    BYTE* selector;

    (void)target_width;
    (void)base_height;

    /*
     * 与分辨率 profile 一样，test11 初始化阶段只解析并保存原始 JMM 比较值，不立即写入目标宽度。
     * 这样主菜单/动画仍完整使用原版 640/800/1024 选择规则；游戏内 profile 启用时再按 BaseHeight
     * 临时改成 test10 已经实机通过的 TargetWidth -> JMMDL/JMMDL800 规则。
     */
    selector = find_unique_pattern(region,
                                   JMM_LAYOUT_SELECT_PATTERN,
                                   JMM_LAYOUT_SELECT_MASK,
                                   (DWORD)sizeof(JMM_LAYOUT_SELECT_PATTERN));

    if (!selector) {
        return FALSE;
    }

    g_jmm_selector_patch = selector;
    g_jmm_original_cmp640 = read_u32(selector + 2u);
    g_jmm_original_cmp800 = read_u32(selector + 23u);

    /* 到这里四组签名都已解析，profile 才算完整可用。 */
    g_resolution_profile_ready =
        (g_res_mode_patch && g_res_map1_patch && g_res_map2_patch && g_jmm_selector_patch && g_display_mode_apply)
        ? TRUE : FALSE;

    return g_resolution_profile_ready;
}

/*
 * 恢复前端/动画使用的原始分辨率代码配置。
 *
 * 这不是把整个游戏永久锁回 640x480；它只恢复 ComeOn.exe 自己原来的几个分支立即数。
 * 原游戏主菜单/动画本来就会请求 640x480，所以恢复原始代码后，cnc-ddraw 能重新把真正的 4:3
 * 640x480 输入等比放大并居中，而不是在一个 1920x1080 游戏 surface 左上角只画 640x480 内容。
 */
static BOOL set_frontend_resolution_profile(void)
{
    if (!g_resolution_profile_ready) {
        return FALSE;
    }

    if (!patch_u32(g_res_mode_patch + 17u, g_res_mode_original[0]) ||
        !patch_u32(g_res_mode_patch + 27u, g_res_mode_original[1]) ||
        !patch_u32(g_res_mode_patch + 39u, g_res_mode_original[2]) ||
        !patch_u32(g_res_mode_patch + 49u, g_res_mode_original[3]) ||
        !patch_u32(g_res_mode_patch + 61u, g_res_mode_original[4]) ||
        !patch_u32(g_res_mode_patch + 71u, g_res_mode_original[5])) {
        return FALSE;
    }

    if (!patch_u32(g_res_map1_patch + 1u, g_res_map1_original[0]) ||
        !patch_u32(g_res_map1_patch + 8u, g_res_map1_original[1]) ||
        !patch_u32(g_res_map1_patch + 15u, g_res_map1_original[2]) ||
        !patch_u32(g_res_map1_patch + 24u, g_res_map1_original[3])) {
        return FALSE;
    }

    if (!patch_u32(g_res_map2_patch + 1u, g_res_map2_original[0]) ||
        !patch_u32(g_res_map2_patch + 8u, g_res_map2_original[1]) ||
        !patch_u32(g_res_map2_patch + 15u, g_res_map2_original[2]) ||
        !patch_u32(g_res_map2_patch + 34u, g_res_map2_original[3]) ||
        !patch_u32(g_res_map2_patch + 39u, g_res_map2_original[4])) {
        return FALSE;
    }

    if (!patch_u32(g_jmm_selector_patch + 2u, g_jmm_original_cmp640) ||
        !patch_u32(g_jmm_selector_patch + 23u, g_jmm_original_cmp800)) {
        return FALSE;
    }

    g_gameplay_profile_active = FALSE;
    return TRUE;
}

/*
 * 启用真正游戏内的 fixed-Y / auto-X profile。
 * 这里完整复制 test10 已实机通过的目标分辨率与 JMM 路由逻辑，只是把“什么时候写进去”从 ASI 初始化
 * 改成“Strategy 状态 3 正式开始以前”。因此输入/HUD/Steam JMM 方案都不需要推翻。
 */
static BOOL set_gameplay_resolution_profile(void)
{
    BOOL use_600_layout;

    if (!g_resolution_profile_ready || g_target_width == 0u || g_target_height == 0u) {
        return FALSE;
    }

    /* 三条显示模式分支在 GAMEPLAY profile 中仍全部统一到目标宽高。 */
    if (!patch_u32(g_res_mode_patch + 17u, g_target_width) ||
        !patch_u32(g_res_mode_patch + 27u, g_target_height) ||
        !patch_u32(g_res_mode_patch + 39u, g_target_width) ||
        !patch_u32(g_res_mode_patch + 49u, g_target_height) ||
        !patch_u32(g_res_mode_patch + 61u, g_target_width) ||
        !patch_u32(g_res_mode_patch + 71u, g_target_height)) {
        return FALSE;
    }

    /* 每次切 profile 都先恢复原生 640/800 比较，再只处理真正发生数值碰撞的特殊比例。 */
    if (!patch_u32(g_res_map1_patch + 1u, g_res_map1_original[0]) ||
        !patch_u32(g_res_map1_patch + 8u, g_res_map1_original[1]) ||
        !patch_u32(g_res_map1_patch + 15u, g_target_width) ||
        !patch_u32(g_res_map1_patch + 24u, g_target_height)) {
        return FALSE;
    }

    if (!patch_u32(g_res_map2_patch + 1u, g_res_map2_original[0]) ||
        !patch_u32(g_res_map2_patch + 8u, g_res_map2_original[1]) ||
        !patch_u32(g_res_map2_patch + 15u, g_target_width) ||
        !patch_u32(g_res_map2_patch + 34u, g_target_height) ||
        !patch_u32(g_res_map2_patch + 39u, g_target_width)) {
        return FALSE;
    }

    if (g_target_width == 640u && g_target_height != 480u) {
        if (!patch_u32(g_res_map1_patch + 1u, 0x7FFFFFFEu) ||
            !patch_u32(g_res_map2_patch + 1u, 0x7FFFFFFEu)) {
            return FALSE;
        }
    }

    if (g_target_width == 800u && g_target_height != 600u) {
        if (!patch_u32(g_res_map1_patch + 8u, 0x7FFFFFFDu) ||
            !patch_u32(g_res_map2_patch + 8u, 0x7FFFFFFDu)) {
            return FALSE;
        }
    }

    use_600_layout = (g_target_height >= 600u) ? TRUE : FALSE;
    if (!use_600_layout) {
        if (!patch_u32(g_jmm_selector_patch + 2u, g_target_width) ||
            !patch_u32(g_jmm_selector_patch + 23u, g_jmm_original_cmp800)) {
            return FALSE;
        }
    } else {
        if (!patch_u32(g_jmm_selector_patch + 2u, 0u) ||
            !patch_u32(g_jmm_selector_patch + 23u, g_target_width)) {
            return FALSE;
        }
    }

    g_gameplay_profile_active = TRUE;
    return TRUE;
}

/*
 * 0x004C6970 的真实 thiscall 类型：ECX=UI 管理器，栈上依次是 mode、width、height，ret 0x0C。
 */
typedef int (__thiscall *FnJmmLoad)(LPVOID self, LONG mode, LONG width, LONG height);

static FnJmmLoad g_original_jmm_load = (FnJmmLoad)0;
/*
 * 0x4087A0 在 call 0x4B35F0 前会用 "mov ecx,<绝对地址>" 装入 UI 管理器。
 * v0.3-test8 不把这个地址硬编码，而是在验证 wrapper 签名后从机器码里读出来。
 */
static LPVOID g_jmm_manager = (LPVOID)0;

/*
 * 0x004EB9E0 内部会从游戏全局字符缓冲区读取资源根目录，再追加 "mb\\"。
 * test4 过早重放完整 0x4B35F0 时弹出 MB\JMMDL*.txt，说明资源根目录是否已经初始化
 * 是一个必须验证的时序条件。这个地址不硬编码：resolve_jmm_context() 会从 0x4B35F0
 * 内部 call 到的 0x4EB9E0 函数头里解析 `mov edi, imm32` 得到真实缓冲区。
 */
static char* g_jmm_resource_root = (char*)0;

static DWORD g_jmm_runtime_log_count = 0u;

/*
 * 通过 0x0040F9D5 改过来的桥接函数。
 * fastcall 的 ECX 正好继续接 this；EDX 只是占位；后三个参数仍在原来的栈位置。
 */
static int __fastcall jmm_load_hook(LPVOID self, LPVOID unused_edx, LONG mode, LONG width, LONG height)
{
    LONG final_width = width;
    LONG final_height = height;

    (void)unused_edx;

    if (!g_original_jmm_load) {
        return 0;
    }

    /* mode=0 就是 0x4087A0 正常“应用当前分辨率/重建 GUI”路径。mode=1 的清理路径不在本 callsite。 */
    if (mode == 0 && g_target_width != 0u) {
        final_width = (LONG)g_target_width;
        /* g_target_height 在初始化计算目标分辨率时和 g_target_width 一起写入。 */
        if (g_target_height != 0u) {
            final_height = (LONG)g_target_height;
        }
    }

    if (g_jmm_runtime_log_count < 8u) {
        char line[320];
        ++g_jmm_runtime_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] JMM应用 模式=");
        append_int(line, (DWORD)sizeof(line), mode);
        str_append(line, (DWORD)sizeof(line), " 输入=");
        append_int(line, (DWORD)sizeof(line), width);
        str_append(line, (DWORD)sizeof(line), "x");
        append_int(line, (DWORD)sizeof(line), height);
        str_append(line, (DWORD)sizeof(line), " 最终=");
        append_int(line, (DWORD)sizeof(line), final_width);
        str_append(line, (DWORD)sizeof(line), "x");
        append_int(line, (DWORD)sizeof(line), final_height);
        append_runtime_line(line);
    }

    return g_original_jmm_load(self, mode, final_width, final_height);
}

/*
 * 只“解析” GUI/JMM 上下文，不修改任何游戏代码。
 *
 * 这一层是 v0.3-test9 新增的关键拆分：
 *   - 旧的 install_jmm_first_load_hook() 一边找 UI manager，一边把 0x4087B5 的 CALL 改成 hook；
 *   - Steam 专用延迟同步其实只需要知道 UI manager 地址，不需要改写这个 CALL；
 *   - 非 Steam 已经实机稳定，因此更不能为了拿一个地址就顺手装额外 hook。
 *
 * 成功后会得到：
 *   g_jmm_manager      = 原版 UI/JMM 管理器对象地址；
 *   g_original_jmm_load = 0x4B35F0 原版函数地址（保留给历史/后续研究）；
 *   out_call_instruction（可选）= 0x4087B5 那条 E8 CALL 的机器码地址。
 */
static BOOL resolve_jmm_context(const TextRegion* region, BYTE** out_call_instruction)
{
    BYTE* wrapper;
    BYTE* call_instruction;
    BYTE* target;
    BYTE* path_call;
    BYTE* path_builder;
    DWORD resource_root_address;
    DWORD index;

    if (out_call_instruction) {
        *out_call_instruction = (BYTE*)0;
    }

    wrapper = find_unique_pattern(region,
                                  JMM_APPLY_CALLSITE_PATTERN,
                                  JMM_APPLY_CALLSITE_MASK,
                                  (DWORD)sizeof(JMM_APPLY_CALLSITE_PATTERN));
    if (!wrapper) {
        return FALSE;
    }

    /*
     * 签名里 +16 是 B9（mov ecx,imm32），+17~+20 就是原版 UI 管理器地址。
     * 当前原版是 0x0055AF98，但这里仍然从机器码解析，避免把一个绝对地址当作版本锁。
     */
    if (wrapper[16] != 0xB9) {
        return FALSE;
    }

    g_jmm_manager = (LPVOID)read_u32(wrapper + 17u);
    if ((DWORD)g_jmm_manager < 0x00400000u || (DWORD)g_jmm_manager >= 0x00600000u) {
        g_jmm_manager = (LPVOID)0;
        return FALSE;
    }

    /* 签名里 E8 位于 +21。 */
    call_instruction = wrapper + 21u;
    if (call_instruction[0] != 0xE8) {
        g_jmm_manager = (LPVOID)0;
        return FALSE;
    }

    target = decode_rel32_target(call_instruction);
    if (!target || target < region->start ||
        target + (DWORD)sizeof(JMM_LOAD_FUNCTION_HEAD) > region->start + region->size) {
        g_jmm_manager = (LPVOID)0;
        return FALSE;
    }

    /* 验证 call 的真正目标确实具有 0x4B35F0 已确认函数头。 */
    for (index = 0; index < (DWORD)sizeof(JMM_LOAD_FUNCTION_HEAD); ++index) {
        if (target[index] != JMM_LOAD_FUNCTION_HEAD[index]) {
            g_jmm_manager = (LPVOID)0;
            return FALSE;
        }
    }

    /*
     * 0x4B35F0 + 0x1E 是 `call 0x4EB9E0`，原版用它取得“资源根目录 + mb\”路径。
     * 0x4EB9E0 的稳定函数头为 `56 57 BF <root-buffer> 83 C9 FF 33 C0 ...`，
     * BF 后面的 imm32 就是资源根字符缓冲区。test10 用它判断完整 JMM 重放是否已经到了安全时机。
     */
    path_call = target + 0x1Eu;
    if (path_call[0] != 0xE8) {
        g_jmm_manager = (LPVOID)0;
        return FALSE;
    }

    path_builder = decode_rel32_target(path_call);
    if (!path_builder || path_builder < region->start || path_builder + 12u > region->start + region->size) {
        g_jmm_manager = (LPVOID)0;
        return FALSE;
    }

    if (path_builder[0] != 0x56 || path_builder[1] != 0x57 || path_builder[2] != 0xBF ||
        path_builder[7] != 0x83 || path_builder[8] != 0xC9 || path_builder[9] != 0xFF ||
        path_builder[10] != 0x33 || path_builder[11] != 0xC0) {
        g_jmm_manager = (LPVOID)0;
        return FALSE;
    }

    resource_root_address = read_u32(path_builder + 3u);
    if (resource_root_address < 0x00400000u || resource_root_address >= 0x00600000u) {
        g_jmm_manager = (LPVOID)0;
        return FALSE;
    }

    g_jmm_resource_root = (char*)resource_root_address;
    g_original_jmm_load = (FnJmmLoad)target;

    if (out_call_instruction) {
        *out_call_instruction = call_instruction;
    }

    return TRUE;
}

/*
 * 历史实验函数：安装第一次 GUI/JMM 分辨率应用 hook。
 * test9/test10 主线都不会安装这个 callsite hook；保留是为了让逆向知识库中的 test3~test5 研究仍可直接从源码追溯。
 */
static BOOL install_jmm_first_load_hook(const TextRegion* region)
{
    BYTE* call_instruction = (BYTE*)0;

    if (!resolve_jmm_context(region, &call_instruction) || !call_instruction) {
        return FALSE;
    }

    if (!patch_rel32_call(call_instruction, (LPVOID)&jmm_load_hook)) {
        g_original_jmm_load = (FnJmmLoad)0;
        g_jmm_manager = (LPVOID)0;
        return FALSE;
    }

    return TRUE;
}

/*
 * 顶层 UI 对象 vtable+0x14 的“分辨率/布局应用”虚函数。
 * 0x4B35F0 在加载完 JMMDL 文件以后，会遍历 UI manager+0x1C 链表，并对每个顶层对象调用：
 *
 *     object->vtable[0x14](mode, width, height)
 *
 * 这是已经从原版汇编逐条闭合的后半段逻辑。
 */
typedef int (__thiscall *FnUIResolutionApply)(LPVOID self, LONG mode, LONG width, LONG height);

/*
 * v0.3-test4 直接调用完整 0x4B35F0 的实机结果：
 *   BaseHeight=480  -> 弹出 “MB\\JMMDL.txt”
 *   BaseHeight=1080 -> 弹出 “MB\\JMMDL800.txt”
 *
 * 这说明“完整重载 JMMDL 文件”不能在 ASI 当前加载时点直接重放。
 * 但用户手工切一次分辨率后布局会恢复，同时 0x4B35F0 的后半段明确只是把新宽高广播给已有 UI。
 *
 * 下面这个函数保留了当时“只广播、不重载文件”的实验实现，作为完整接档证据。
 * 用户随后回溯确认非 Steam GUI 从 v0.2-test1 / v0.3-test1 起本来就正常，因此 test7 初始化
 * 明确不会调用它。未来若研究 Steam 专属初始化时序，可以继续参考这段代码，但必须先有新的运行时证据，
 * 不能把它重新无条件套到所有环境。
 */
static void broadcast_initial_ui_resolution(void)
{
    LPVOID node;
    DWORD applied = 0u;
    DWORD visited = 0u;
    char line[384];

    if (!g_jmm_manager || g_target_width == 0u || g_target_height == 0u) {
        append_runtime_line("[运行] 启动期UI广播已跳过：前置条件不可用");
        return;
    }

    node = *(LPVOID*)((BYTE*)g_jmm_manager + 0x1Cu);

    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[运行] 启动期UI广播开始 管理器=");
    append_hex32(line, (DWORD)sizeof(line), (DWORD)g_jmm_manager);
    str_append(line, (DWORD)sizeof(line), " 目标=");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_width);
    str_append(line, (DWORD)sizeof(line), "x");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_height);
    append_runtime_line(line);

    while (node && visited < 512u) {
        LPVOID vtable = *(LPVOID*)node;
        DWORD function_address = 0u;

        ++visited;

        if (vtable) {
            function_address = *(DWORD*)((BYTE*)vtable + 0x14u);
        }

        if (function_address >= 0x00400000u && function_address < 0x00600000u) {
            FnUIResolutionApply apply_resolution = (FnUIResolutionApply)function_address;
            apply_resolution(node, 0, (LONG)g_target_width, (LONG)g_target_height);
            ++applied;
        }

        node = *(LPVOID*)((BYTE*)node + UI_OBJECT_NEXT_OFFSET);
    }

    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[运行] 启动期UI广播结束 已遍历=");
    append_int(line, (DWORD)sizeof(line), (LONG)visited);
    str_append(line, (DWORD)sizeof(line), " 已应用=");
    append_int(line, (DWORD)sizeof(line), (LONG)applied);
    if (node) {
        str_append(line, (DWORD)sizeof(line), " 保护=命中");
    } else {
        str_append(line, (DWORD)sizeof(line), " 保护=正常");
    }
    append_runtime_line(line);
}

/*
 * 对“当前已经存在的顶层 UI 链”只做尺寸广播，不重新加载任何 JMMDL 文件。
 *
 * 这段逻辑等价于 0x4B35F0 已确认的后半段：
 *   node = manager+0x1C 链表头；
 *   每个 node 调 vtable+0x14(mode=0, TargetWidth, TargetHeight)；
 *   node = node+0x08 的 next。
 *
 * 返回值是成功调用了多少个顶层 UI 对象；visited_out（如果非空）则返回遍历了多少个节点。
 * 这个函数本身不判断 Steam，也不做 one-shot，所有策略由 try_steam_delayed_ui_sync() 管理。
 */
static DWORD apply_current_ui_resolution_broadcast(DWORD* visited_out)
{
    LPVOID node;
    DWORD visited = 0u;
    DWORD applied = 0u;

    if (visited_out) {
        *visited_out = 0u;
    }

    if (!g_jmm_manager || g_target_width == 0u || g_target_height == 0u) {
        return 0u;
    }

    node = *(LPVOID*)((BYTE*)g_jmm_manager + 0x1Cu);

    while (node && visited < 512u) {
        LPVOID vtable = *(LPVOID*)node;
        DWORD function_address = 0u;

        ++visited;

        if (vtable) {
            function_address = *(DWORD*)((BYTE*)vtable + 0x14u);
        }

        /*
         * ComeOn.exe 自身代码位于 0x00400000~0x00600000 这一固定 PE32 范围。
         * 只有 vtable 槽明确指回游戏代码时才调用，避免把空指针/数据当函数执行。
         */
        if (function_address >= 0x00400000u && function_address < 0x00600000u) {
            FnUIResolutionApply apply_resolution = (FnUIResolutionApply)function_address;
            apply_resolution(node, 0, (LONG)g_target_width, (LONG)g_target_height);
            ++applied;
        }

        node = *(LPVOID*)((BYTE*)node + UI_OBJECT_NEXT_OFFSET);
    }

    if (visited_out) {
        *visited_out = visited;
    }

    return applied;
}

/*
 * Steam/ComeOn.dll 专用：在 HUD 真正成熟后，补一次非 Steam 自然会发生、Steam 缺失的第二阶段 UI 布局。
 *
 * 为什么触发点选在主 HUD +0x58 自动布局之后：
 *   1. ASI 初始化时 UI manager+0x1C 还是空链，test5 日志已经证明 visited=0；
 *   2. Steam/非 Steam 对照日志证明，两边第一次 HUD 布局完全一致，之后只有非 Steam 继续发生第二阶段布局；
 *   3. 当 HUD 的 0x0B/0x0E child 都已经存在时，至少说明主 HUD 子控件树已经建立，不再是“只有空壳对象”的早期阶段；
 *   4. 此时再确认 manager+0x1C 非空，才允许广播，避免重演 test4/test5 的过早初始化问题。
 *
 * 这个函数还特别避免两个历史坑：
 *   - 不调用完整 0x4B35F0，所以不会再次打开 MB\\JMMDL.txt / JMMDL800.txt；
 *   - 不改 0x4087B5 callsite，不影响非 Steam 原本就正常的自然 JMM/UI 生命周期。
 */
static BOOL steam_jmm_resource_root_ready(void)
{
    DWORD length = 0u;

    if (!g_jmm_resource_root) {
        return FALSE;
    }

    /*
     * 不猜盘符或 Steam 安装目录，只要求游戏自己维护的资源根字符串已经非空并且正常终止。
     * 最多扫 1024 字节，防止坏状态下越界读取。
     */
    while (length < 1024u && g_jmm_resource_root[length] != '\0') {
        ++length;
    }

    if (length == 0u || length >= 1024u) {
        return FALSE;
    }

    return TRUE;
}

/*
 * Steam/ComeOn.dll 专用：在 HUD 真正成熟后补一次“完整的原版 JMM 第二阶段应用”。
 *
 * test9 已经用实机日志证明：即使 34 个顶层 UI 的 vtable+0x14 全部执行，0x0B/0x0E
 * 的矩形仍完全不变，所以非 Steam 自然第二阶段并不是单纯的“广播 TargetWidth/TargetHeight”。
 * 重新反汇编 0x004C6970 后确认完整流程是：
 *   1. 0x4EB9E0 取得资源根目录并追加 "mb\\"；
 *   2. 根据宽度选择 JMMDL.txt 或 JMMDL800.txt；
 *   3. 0x4D0500 真正加载 JMM 布局资源；
 *   4. 最后才遍历 UI manager+0x1C，调用各对象 vtable+0x14。
 *
 * 因此 test10 在 Steam 环境下不再自己模拟第 4 步，而是在以下条件全部满足后只调用一次
 * 游戏原版 0x4B35F0(mode=0, TargetWidth, TargetHeight)：
 *   - 主 HUD 0x0B/0x0E child 已存在；
 *   - UI manager 顶层链已非空；
 *   - 游戏资源根目录已经非空；
 *   - 当前没有正在进行的 Steam JMM 重放。
 *
 * test4 之所以弹 MB\\JMMDL*.txt，是因为它在 ASI 初始化阶段就过早重放；test10 明确等待
 * 资源路径和 GUI 生命周期都成熟后再调用。非 Steam 永远不会进入这条额外路径。
 */
static void try_steam_delayed_ui_sync(LPVOID hud)
{
    LPVOID child_0b;
    LPVOID child_0e;
    LPVOID hud_after;
    LPVOID list_head;
    HudRect rect_0b_before;
    HudRect rect_0e_before;
    HudRect rect_0b_after;
    HudRect rect_0e_after;
    BOOL have_0b_before;
    BOOL have_0e_before;
    BOOL have_0b_after;
    BOOL have_0e_after;
    BOOL changed = FALSE;
    int result;
    char line[896];

    /* test13：前端 HUD 也可能存在，所以 Steam full JMM 只能在 Strategy 已正式进入 gameplay 后运行。 */
    if (!hud || !g_gameplay_profile_active || g_strategy_transition_in_progress ||
        g_steam_ui_sync_done || g_steam_ui_sync_in_progress) {
        return;
    }

    /* 某些加载顺序下 ComeOn.dll 可能比 ASI 稍晚出现，所以允许在早期 HUD layout 补检测。 */
    if (!g_steam_environment && GAME_GetModuleHandleA) {
        if (GAME_GetModuleHandleA("ComeOn.dll")) {
            g_steam_environment = TRUE;
            append_runtime_line("[运行] 延迟检测到Steam环境：ComeOn.dll现已加载");
        }
    }

    if (!g_steam_environment || !g_jmm_manager || !g_original_jmm_load) {
        return;
    }

    child_0b = find_main_hud_child_by_id(hud, 0x0Bu);
    child_0e = find_main_hud_child_by_id(hud, 0x0Eu);
    if (!child_0b || !child_0e) {
        return;
    }

    list_head = *(LPVOID*)((BYTE*)g_jmm_manager + 0x1Cu);
    if (!list_head) {
        return;
    }

    /* test10 的关键安全门槛：资源根目录尚未初始化时绝不调用完整 0x4B35F0。 */
    if (!steam_jmm_resource_root_ready()) {
        if (!g_steam_ui_sync_wait_root_logged) {
            g_steam_ui_sync_wait_root_logged = TRUE;
            append_runtime_line("[运行] Steam延迟JMM应用等待中：游戏资源根节点尚未就绪");
        }
        return;
    }

    if (g_steam_ui_sync_attempts >= 2u) {
        return;
    }
    ++g_steam_ui_sync_attempts;

    have_0b_before = read_child_rect(child_0b, &rect_0b_before);
    have_0e_before = read_child_rect(child_0e, &rect_0e_before);

    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[运行] Steam延迟JMM应用开始 尝试次数=");
    append_int(line, (DWORD)sizeof(line), (LONG)g_steam_ui_sync_attempts);
    str_append(line, (DWORD)sizeof(line), " 管理器=");
    append_hex32(line, (DWORD)sizeof(line), (DWORD)g_jmm_manager);
    str_append(line, (DWORD)sizeof(line), " 头节点=");
    append_hex32(line, (DWORD)sizeof(line), (DWORD)list_head);
    str_append(line, (DWORD)sizeof(line), " 目标=");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_width);
    str_append(line, (DWORD)sizeof(line), "x");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_height);
    str_append(line, (DWORD)sizeof(line), " 资源根=就绪 ID0B前=");
    append_child_brief(line, (DWORD)sizeof(line), child_0b);
    str_append(line, (DWORD)sizeof(line), " ID0E前=");
    append_child_brief(line, (DWORD)sizeof(line), child_0e);
    append_runtime_line(line);

    /* 完整复用游戏自己的 ReceiveMsg/JMM 入口；in_progress 防止内部广播再次递归触发本函数。 */
    g_steam_ui_sync_in_progress = TRUE;
    result = g_original_jmm_load(g_jmm_manager, 0, (LONG)g_target_width, (LONG)g_target_height);
    g_steam_ui_sync_in_progress = FALSE;

    /* 原版完整应用可能重排甚至重建 HUD，所以 after 阶段优先使用最新主 HUD 实例。 */
    hud_after = g_main_hud_instance ? g_main_hud_instance : hud;
    child_0b = find_main_hud_child_by_id(hud_after, 0x0Bu);
    child_0e = find_main_hud_child_by_id(hud_after, 0x0Eu);
    have_0b_after = read_child_rect(child_0b, &rect_0b_after);
    have_0e_after = read_child_rect(child_0e, &rect_0e_after);

    if (have_0b_before && have_0b_after &&
        (rect_0b_before.left != rect_0b_after.left || rect_0b_before.top != rect_0b_after.top)) {
        changed = TRUE;
    }
    if (have_0e_before && have_0e_after &&
        (rect_0e_before.left != rect_0e_after.left || rect_0e_before.top != rect_0e_after.top)) {
        changed = TRUE;
    }

    /* 返回非 0 表示原版完整 JMM 应用成功；成功后本进程不再重复。 */
    if (result != 0) {
        g_steam_ui_sync_done = TRUE;
        g_steam_ui_sync_applied = 1u;
    }

    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[运行] Steam延迟JMM应用结束 结果=");
    append_int(line, (DWORD)sizeof(line), result);
    str_append(line, (DWORD)sizeof(line), g_steam_ui_sync_done ? " 完成=1" : " 完成=0");
    str_append(line, (DWORD)sizeof(line), changed ? " 子布局已变化=1" : " 子布局已变化=0");
    str_append(line, (DWORD)sizeof(line), " ID0B后=");
    append_child_brief(line, (DWORD)sizeof(line), child_0b);
    str_append(line, (DWORD)sizeof(line), " ID0E后=");
    append_child_brief(line, (DWORD)sizeof(line), child_0e);
    append_runtime_line(line);
}


/* ============================================================================================== */
/* 10.X 只改变“绘制先后顺序”的辅助 GUI 图层修复                                                       */
/* ============================================================================================== */

/*
 * 这一节是 layer1 相对 test1~test7 最重要的架构变化。
 *
 * 旧实验曾经尝试把物品 / 装备 / 技能窗口从屏幕边缘搬到别的位置，然后再补鼠标坐标、child 命中、
 * 键盘入口和乾坤袋位置。实机已经证明，这条路会把游戏原本已经成立的“显示坐标”和“输入坐标”拆开，
 * 修一个按钮又可能弄坏另一个按钮。
 *
 * layer1 完全停止移动这三个辅助 GUI。它们的 X/Y、parent、child、hit-test、快捷键、active 状态都由
 * 游戏原版维护。DisplayFix 只解决真正的问题：主 HUD 居中以后，原版绘制顺序会让某些贴边辅助 GUI
 * 先画，主 HUD 后画，于是辅助 GUI 的一部分被主 HUD 盖住。
 *
 * 为了只改“谁最后画”，又绝不改 UI 链表本身，本版使用三层很窄的 Hook：
 *   1. UI manager 的总 Draw 调用点只负责标记“一次完整顶层绘制正在进行”；随后仍调用原版 manager Draw。
 *   2. 如果物品 / 装备 / 技能对象在主 HUD 之前被画，并且它当前确实处于 active 状态，就暂时不画，
 *      只把“原本应该调用哪个 Draw”记下来。
 *   3. 主 HUD 自己的原版 Draw 完成后，立刻按刚才遇到的原始先后顺序，把这些延迟对象各画一次。
 *
 * 这样做有三个关键性质：
 *   - 原版 manager 仍然负责遍历 manager+0x1C / object+0x08，DisplayFix 不重写遍历器；
 *   - manager+0x18 / object+0x0C 的输入反向链完全没有被改过，所以鼠标优先级也不改变；
 *   - 每个对象仍然只调用一次原版 Draw，不会因为“最后再补画一遍”产生透明度叠加或动画推进两次。
 */

typedef int  (__thiscall *FnUIDraw)(LPVOID self, DWORD draw_context);
typedef void (__thiscall *FnUIManagerDraw)(LPVOID self, DWORD draw_context);


/*
 * 原版 0x4B4800 类型：ECX 是 UI manager，唯一显式参数是指向当前鼠标逻辑坐标的 POINT。
 * 返回值是原版判定的“这一次输入应该交给哪个顶层 root”，没有命中时返回 NULL。
 */
typedef LPVOID (__thiscall *FnUITopLevelPick)(LPVOID self, const POINT* point);

/*
 * 原版 0x4B1D30 类型：ECX 是一个 UI 对象，返回一个至少包含 x/y/width/height 的临时矩形结构。
 * 正常对象会直接返回 object+0x14；特殊 JMM 参数下会返回游戏自己的临时全局矩形。
 * layer1d 只立即复制前四个 LONG，绝不长期保存这个返回指针。
 */
typedef LONG* (__thiscall *FnUIGetHitRect)(LPVOID self);

/* 原版 0x4D0210：从对象 +0x50 指向的 JMM 属性表读取一个整数属性。顶层 picker 用 key=0x0D 判断 root 是否参与鼠标选择。 */
typedef LONG (__thiscall *FnUIPropertyGet)(LPVOID property_table, LONG key);

/*
 * 0x408510（本体）/ 0x40F730（外传）的包装函数形状完全相同：
 *
 *   mov eax,[ecx]
 *   mov ecx,<UI manager 绝对地址>
 *   push eax
 *   call <UI manager Draw>
 *   ret
 *
 * 绝对地址和 rel32 都设成通配，所以同一份代码可以在本体/外传各自从当前 EXE 现场解析。
 */
static const BYTE UI_MANAGER_DRAW_CALLSITE_PATTERN[] = {
    0x8B,0x01,
    0xB9,0,0,0,0,
    0x50,
    0xE8,0,0,0,0,
    0xC3
};
static const char UI_MANAGER_DRAW_CALLSITE_MASK[] = "xxx????xx????x";


/*
 * layer1d 新增：顶层 root 选择器的唯一 callsite。
 *
 * 原版输入路径已经从反汇编闭合为：
 *   0x4B4790：取得当前鼠标坐标；
 *   0x4B47DC：调用 0x4B4800，从顶层 UI 中选出这一次应该接收输入的 root；
 *   0x4B47E4：把返回的 root 交给 0x4B44D0；
 *   0x4B44D0：由游戏原版负责更新 manager+0x40，并通知旧 root 失去当前状态。
 *
 * 因此最窄、最安全的做法不是直接写 manager+0x40，更不是重排 manager 的链表；而是只把
 * “0x4B4800 的返回值”在一个极窄条件下从 HUD 换成菜单 root。后续 +0x20/+0x24/+0x30、
 * child hit-test、鼠标按下/释放、焦点切换都仍然由游戏原版执行。
 *
 * 下面的签名从 0x4B4790 中 `lea POINT -> mov ecx,manager -> push POINT -> call picker ->
 * push result -> mov ecx,manager -> call set-current-root -> mov eax,[manager+0x40]` 这一整段结构定位。
 * 两个 rel32 设为通配，避免把本体/外传绝对地址写死。
 */
static const BYTE UI_TOP_LEVEL_PICK_CALLSITE_PATTERN[] = {
    0x8D,0x44,0x24,0x0C,
    0x8B,0xCE,
    0x50,
    0xC7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,
    0xE8,0,0,0,0,
    0x50,
    0x8B,0xCE,
    0xE8,0,0,0,0,
    0x8B,0x4C,0x24,0x14,
    0x8B,0x46,0x40
};
static const char UI_TOP_LEVEL_PICK_CALLSITE_MASK[] = "xxxxxxxxxxxxxxxx????xxxx????xxxxxxx";

/* 保存主 HUD vtable+0x08 槽。真正安装图层 Hook 时才会改写它。 */
static BYTE* g_main_hud_draw_slot = (BYTE*)0;

/* 原版 UI manager Draw；layer scope wrapper 内部最终仍完整调用它。 */
static FnUIManagerDraw g_original_ui_manager_draw = (FnUIManagerDraw)0;

/* 主 HUD 原版 Draw；主 HUD wrapper 先完整调用它，再处理延迟的辅助 GUI。 */
static FnUIDraw g_original_main_hud_draw = (FnUIDraw)0;

/*
 * manager Draw 函数开头会直接读一个“主 HUD 全局指针槽”。
 * 我们把这个槽从原版机器码解析出来，不把本体 0x55BBB0 / 外传 0x58D164 写死进逻辑。
 */
static LPVOID* g_layer_main_hud_global_slot = (LPVOID*)0;

/* 记录唯一的 UI manager 对象地址；它同样来自 callsite 的 `mov ecx,imm32`。 */
static LPVOID g_layer_ui_manager = (LPVOID)0;

/* 目标 EXE 的 .text 范围，只用来确认 vtable+0x08 最初确实指向游戏代码。 */
static BYTE* g_layer_text_start = (BYTE*)0;
static BYTE* g_layer_text_end = (BYTE*)0;

/*
 * 三个目标类各自保存一个“原版 Draw + 已改写的 vtable 槽”。
 * target_slot 是主 HUD 原版事件函数中解析出的顶层对象全局槽：
 *   0x0B = 装备
 *   0x0D = 技能
 *   0x0E = 物品
 */
typedef struct LayerTargetHook_TAG {
    DWORD control_id;
    LPVOID* target_slot;
    BYTE* draw_slot;
    FnUIDraw original_draw;
    BOOL installed;

    /*
     * disabled 只针对当前这一类菜单。
     * 一旦对象已经真实存在，却发现 vtable/Draw/layout 不再符合已确认结构，就把这一类永久停用到进程退出。
     * 这样不会每帧重复尝试危险 Hook；其它两类仍可独立工作，稳定基线也完全不受影响。
     */
    BOOL disabled;
} LayerTargetHook;

static LayerTargetHook g_layer_equipment = { 0x0Bu, (LPVOID*)0, (BYTE*)0, (FnUIDraw)0, FALSE, FALSE };
static LayerTargetHook g_layer_skill     = { 0x0Du, (LPVOID*)0, (BYTE*)0, (FnUIDraw)0, FALSE, FALSE };
static LayerTargetHook g_layer_inventory = { 0x0Eu, (LPVOID*)0, (BYTE*)0, (FnUIDraw)0, FALSE, FALSE };

/*
 * layer1a 只会延迟三个已知主菜单；layer1d 继续允许把“属于当前菜单体系的独立顶层辅助面板”一起延迟。
 * 本轮新增短生命周期跟踪：独立面板一旦在合法主菜单上下文中被确认，即使随后物品主窗口先关闭，
 * 只要该面板自身仍 active、仍在 HUD 前的真实顶层链、vtable 未变且仍与 HUD 相交，就继续保持 HUD 后绘制。
 * 固定 16 项足够覆盖当前实机界面，而且完全不需要 malloc，不会在老游戏渲染热路径里做堆分配。
 * 超过 16 项时直接失败开放：多出来的对象保持原版 Draw，不会覆盖数组。
 */
typedef struct DeferredLayerDraw_TAG {
    LPVOID object;
    FnUIDraw original_draw;
    DWORD draw_context;
    DWORD control_id;
} DeferredLayerDraw;

static DeferredLayerDraw g_deferred_layer_draws[16];
static DWORD g_deferred_layer_count = 0u;

/* 当前是否处在“原版 UI manager Draw 的这一帧调用”内部。 */
static BOOL g_layer_draw_scope_active = FALSE;

/* 当前这一帧是否满足真正调整图层的条件。 */
static BOOL g_layer_draw_pass_enabled = FALSE;

/* 当前这一帧主 HUD 顶层 Draw 是否已经完成。 */
static BOOL g_layer_main_hud_drawn = FALSE;

/* 防止理论上的嵌套 manager Draw 把外层状态覆盖。 */
static DWORD g_layer_draw_scope_depth = 0u;

/* 运行日志只记录每个目标第一次真的被延迟，避免每帧刷盘。bit0/1/2 对应 0B/0D/0E。 */
static DWORD g_layer_runtime_logged_mask = 0u;

/* 异常兜底最多记一次。 */
static BOOL g_layer_unexpected_flush_logged = FALSE;

/* 每个菜单第一次得到原始顶层 Draw 链索引时记一次。bit0/1/2 对应 0B/0D/0E。 */
static DWORD g_layer_chain_relation_logged_mask = 0u;

/* 每个菜单类如果结构验证失败，只记一次警告。bit0/1/2 对应 0B/0D/0E。 */
static DWORD g_layer_hook_failure_logged_mask = 0u;

/*
 * layer1d 输入/动态辅助面板状态。
 *
 * layer1b 的实机日志已经证明：把 manager+0x1C/+0x18 当成“普通 NULL 结尾双向链”是不成立的，
 * 因此 layer1d 完全删除运行时链表写入。下面只保存两个原版函数指针和少量 Hook 元数据。
 */
static FnUITopLevelPick g_original_ui_top_level_pick = (FnUITopLevelPick)0;
static FnUIGetHitRect g_original_ui_get_hit_rect = (FnUIGetHitRect)0;
static FnUIPropertyGet g_original_ui_property_get = (FnUIPropertyGet)0;
static DWORD g_layer_input_override_log_count = 0u;

/*
 * 主 HUD 顶部 0x0B / 0x0E 两个圆形按钮在 HUD 居中后，功能虽然已经通过 release 兜底恢复，
 * 但原版按下动画仍可能缺失。原因是按下阶段的顶层 root picker 有时没有把这一次输入交给已经居中的 HUD，
 * 于是游戏自己的 HUD vtable+0x20 按下路径没有执行。
 *
 * 这里单独给这个“按压反馈 root 校正”一个限量日志计数器。它不记录普通鼠标移动，
 * 只在原版 picker 本来没有选中 HUD、而实时 child 矩形明确命中 0x0B/0x0E 时记录。
 */
static DWORD g_hud_button_root_correction_log_count = 0u;

/*
 * 独立左侧装备面板等对象不一定使用 0x0B/0x0D/0x0E 三个主菜单 vtable。
 * layer1d 因此允许在运行时对最多 16 个“已经由当前菜单上下文证明相关”的额外 vtable 安装同一个 Draw wrapper。
 *
 * 每一项只记录 vtable、vtable+0x08 槽和被替换掉的原版 Draw。Hook 一旦安装就不撤销；菜单关闭以后
 * wrapper 会因为“不在 layer scope / 不是当前候选”而直接调用原版 Draw，所以不会改变普通跑图行为。
 */
typedef struct DynamicLayerHook_TAG {
    BYTE* vtable;
    BYTE* draw_slot;
    FnUIDraw original_draw;
} DynamicLayerHook;

#define LAYER_DYNAMIC_HOOK_MAX 16u
static DynamicLayerHook g_layer_dynamic_hooks[LAYER_DYNAMIC_HOOK_MAX];
static DWORD g_layer_dynamic_hook_count = 0u;
static BOOL g_layer_dynamic_hook_capacity_logged = FALSE;
static DWORD g_layer_dynamic_defer_log_count = 0u;

/*
 * layer1d：给“已经在合法主菜单上下文里确认过”的独立辅助顶层对象一个很短的生命周期记忆。
 *
 * 为什么需要它：
 *   layer1c 只有在 0x0B/0x0D/0x0E 至少一个主菜单仍 active 时，才把未知独立面板当成菜单体系的一部分。
 *   实机已经证明：装备左侧面板可以在物品主窗口关闭以后继续保持 active。此时主菜单门槛突然消失，
 *   layer1c 就会把这个仍然存在的装备面板重新放回 HUD 下方。
 *
 * 这里不“永久记住 vtable”，也不保存任何游戏分配出来的内存内容副本，而是只保存：
 *   - 当时那个对象指针；
 *   - 当时那个对象的 vtable；
 *   - 它最近一次在当前顶层 Draw 链里通过验证的帧编号。
 *
 * 每次 UI manager Draw 开始前都会重新验证：对象必须仍在 HUD 之前的真实顶层链、仍 active、vtable 没变、
 * 仍和 HUD 相交。任何一项不成立就立即忘掉该对象。这样既能跨过“物品 root 已关闭但装备面板还活着”这一小段
 * 生命周期，又不会把一个已经销毁/复用的旧地址长期当成菜单。
 */
typedef struct TrackedAuxiliaryObject_TAG {
    LPVOID object;
    BYTE* vtable;
    DWORD seen_epoch;
    BOOL detached_keep_logged;
} TrackedAuxiliaryObject;

#define LAYER_TRACKED_AUXILIARY_MAX 16u
static TrackedAuxiliaryObject g_layer_tracked_auxiliary[LAYER_TRACKED_AUXILIARY_MAX];
static DWORD g_layer_tracking_epoch = 0u;
static BOOL g_layer_tracking_capacity_logged = FALSE;
/*
 * 判断一个函数地址是否确实落在当前 EXE 的 .text 中。
 * 这是 vtable Hook 的最后一道保险：如果某个兼容 EXE 的类结构已经变化，就宁可不安装目标 Hook，
 * 也绝不把数据地址或第三方 DLL 地址误当成原版 Draw。
 */
static BOOL layer_address_is_game_text(DWORD address)
{
    return g_layer_text_start && g_layer_text_end &&
           address >= (DWORD)g_layer_text_start && address < (DWORD)g_layer_text_end;
}

/*
 * UI manager 的原版顶层绘制链从 manager+0x1C 开始，沿 object+0x08 向前走。
 * 这里只读链表，用来确认“本帧主 HUD 确实在同一条绘制链上”。
 *
 * 为什么要先确认？
 * 如果某个特殊场景根本没有主 HUD，而我们仍把物品窗口延迟等待 HUD，就会导致这一帧窗口不画。
 * 所以找不到 HUD 时本版直接关闭本帧图层调整，完全回到原版 Draw 顺序。
 */
static DWORD layer_log_bit_for_id(DWORD control_id);

static BOOL layer_draw_chain_contains(LPVOID manager, LPVOID wanted)
{
    LPVOID node;
    DWORD guard = 0u;

    if (!manager || !wanted) {
        return FALSE;
    }

    node = *(LPVOID*)((BYTE*)manager + 0x1Cu);
    while (node && guard < 128u) {
        if (node == wanted) {
            return TRUE;
        }
        node = *(LPVOID*)((BYTE*)node + 0x08u);
        ++guard;
    }

    return FALSE;
}


/*
 * 返回 wanted 在 UI manager 原始顶层 Draw 链里的从 0 开始索引。
 * 返回 -1 表示当前链里没有这个对象，或者输入无效。
 *
 * 这个函数只读取 manager+0x1C / object+0x08，不修改链表，所以它不会改变输入优先级或绘制顺序。
 * layer1a 用它做诊断的原因是：把“菜单移到 HUD 后面”除了会翻转菜单/HUD 的相对关系，还可能翻转菜单与
 * 两者之间其它顶层 UI 的关系。实机日志必须先告诉我们原始链究竟是什么样，后续才能判断是否需要更窄的方案。
 */
static LONG layer_draw_chain_index(LPVOID manager, LPVOID wanted)
{
    LPVOID node;
    LONG index = 0;

    if (!manager || !wanted) {
        return -1;
    }

    node = *(LPVOID*)((BYTE*)manager + 0x1Cu);
    while (node && index < 128) {
        if (node == wanted) {
            return index;
        }
        node = *(LPVOID*)((BYTE*)node + 0x08u);
        ++index;
    }

    return -1;
}

/*
 * 对一个已经存在、但无法安全安装 Draw Hook 的菜单类执行“失败开放”。
 * 这里只关闭这一类菜单的图层调整，不触碰对象、输入、active、坐标，也不影响其它两类菜单。
 */
static void layer_disable_target_hook(LayerTargetHook* hook, const char* reason)
{
    DWORD bit;

    if (!hook) {
        return;
    }

    hook->disabled = TRUE;
    bit = layer_log_bit_for_id(hook->control_id);

    if (bit && !(g_layer_hook_failure_logged_mask & bit)) {
        char line[448];
        g_layer_hook_failure_logged_mask |= bit;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[警告] 辅助GUI绘制层：ID=");
        append_hex32(line, (DWORD)sizeof(line), hook->control_id);
        str_append(line, (DWORD)sizeof(line), " Draw Hook 已单独禁用；原因=");
        str_append(line, (DWORD)sizeof(line), reason ? reason : "未知");
        str_append(line, (DWORD)sizeof(line), "；该菜单保持原版绘制/输入");
        append_runtime_line(line);
    }
}

/*
 * 第一次看到某个目标对象进入顶层 Draw 链时，记录它与主 HUD 的原始链索引。
 * 这只是诊断，不参与是否命中按钮，也不参与是否移动对象。
 */
static void layer_log_chain_relation_once(LPVOID manager, LPVOID hud, LayerTargetHook* hook)
{
    LPVOID object;
    LONG hud_index;
    LONG target_index;
    DWORD bit;
    char line[512];

    if (!manager || !hud || !hook || !hook->target_slot) {
        return;
    }

    bit = layer_log_bit_for_id(hook->control_id);
    if (!bit || (g_layer_chain_relation_logged_mask & bit)) {
        return;
    }

    object = *hook->target_slot;
    if (!object) {
        return;
    }

    hud_index = layer_draw_chain_index(manager, hud);
    target_index = layer_draw_chain_index(manager, object);
    if (hud_index < 0 || target_index < 0) {
        return;
    }

    g_layer_chain_relation_logged_mask |= bit;
    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[运行] 辅助GUI原始Draw链 ID=");
    append_hex32(line, (DWORD)sizeof(line), hook->control_id);
    str_append(line, (DWORD)sizeof(line), " 菜单索引=");
    append_int(line, (DWORD)sizeof(line), target_index);
    str_append(line, (DWORD)sizeof(line), " 主HUD索引=");
    append_int(line, (DWORD)sizeof(line), hud_index);

    if (target_index < hud_index) {
        str_append(line, (DWORD)sizeof(line), " 关系=菜单在HUD之前；layer1d会在HUD之后延迟绘制该菜单");
    } else if (target_index > hud_index) {
        str_append(line, (DWORD)sizeof(line), " 关系=菜单已在HUD之后；layer1d不需要调整该菜单");
    } else {
        str_append(line, (DWORD)sizeof(line), " 关系=异常同索引；本帧不会据此修改任何输入状态");
    }

    append_runtime_line(line);
}

/*
 * 判断某个 target wrapper 当前是不是“UI manager 顶层遍历正在画的那个对象”。
 * manager 原版代码在每次 Draw 前都会把 manager+0x20 写成 current object。
 * 只有这个条件成立时，我们才允许延迟；如果同一个类的 Draw 从别的内部路径被调用，必须原样执行。
 */

/*
 * 判断两个已经验证有效的矩形是否有任何像素区域相交。
 *
 * 这里不用“中心点”或固定按钮坐标，因为我们需要覆盖三类情况：
 *   1. 物品/技能主窗口的底部按钮被 HUD 压住；
 *   2. 连招编辑等子菜单的关闭按钮落进 HUD 区；
 *   3. 用户实机发现的左侧独立装备/辅助面板仍然在 HUD 下面。
 *
 * 只要顶层窗口真实矩形和 HUD 矩形相交，就说明二者确实存在层级竞争。
 */
static BOOL layer_rects_intersect(const HudRect* a, const HudRect* b)
{
    if (!a || !b || !a->valid || !b->valid) {
        return FALSE;
    }

    if (a->right < b->left || b->right < a->left ||
        a->bottom < b->top || b->bottom < a->top) {
        return FALSE;
    }

    return TRUE;
}

/*
 * 只读判断三个已确认主菜单（装备 0x0B / 技能 0x0D / 物品 0x0E）当前是否至少有一个正在显示。
 *
 * layer1d 只有在这些主菜单实际打开时才允许启用辅助面板延后绘制和输入 root 覆盖。普通跑图、战斗、
 * 小地图、右侧常驻按钮等状态完全不会进入这套候选逻辑。
 */
static BOOL layer_any_primary_menu_active(void)
{
    LayerTargetHook* hooks[3];
    DWORD i;

    hooks[0] = &g_layer_equipment;
    hooks[1] = &g_layer_skill;
    hooks[2] = &g_layer_inventory;

    for (i = 0u; i < 3u; ++i) {
        LayerTargetHook* hook = hooks[i];
        LPVOID object;

        if (!hook || !hook->target_slot) {
            continue;
        }

        object = *hook->target_slot;
        if (object && child_active_for_diagnostic(object)) {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * 判断 object 自己是不是三个已确认主菜单之一。
 * 这是最可靠的候选条件：即使某个老 JMM 对象的 width/height 不完整，只要它就是 0x0B/0x0D/0x0E
 * 当前实例，就仍然应该和 HUD 使用同一份 Z 顺序。
 */
static BOOL layer_is_primary_menu_object(LPVOID object)
{
    if (!object) {
        return FALSE;
    }

    if (g_layer_equipment.target_slot && *g_layer_equipment.target_slot == object) {
        return TRUE;
    }
    if (g_layer_skill.target_slot && *g_layer_skill.target_slot == object) {
        return TRUE;
    }
    if (g_layer_inventory.target_slot && *g_layer_inventory.target_slot == object) {
        return TRUE;
    }

    return FALSE;
}

/*
 * 判断一个对象沿 +0xA4 parent 链向上追溯后，是否属于三个主菜单中的任意一个。
 *
 * 有些“子菜单”虽然也出现在 UI manager 顶层 Draw 链里，但对象内部仍保留到主菜单的 parent 关系。
 * 如果能用这条关系证明归属，就不需要猜它的 control ID、vtable 或屏幕位置。
 *
 * 最多追 16 层，既足够覆盖实际 UI，又避免坏 parent 链形成死循环时卡住游戏。
 */
static BOOL layer_belongs_to_primary_menu_tree(LPVOID object)
{
    LPVOID current = object;
    DWORD guard = 0u;

    while (current && guard < 16u) {
        if (layer_is_primary_menu_object(current)) {
            return TRUE;
        }

        current = *(LPVOID*)((BYTE*)current + 0xA4u);
        ++guard;
    }

    return FALSE;
}

/*
 * 读取“顶层输入选择器真正使用的矩形”。
 *
 * 原版 0x4B4800 不是直接固定读取 object+0x14，而是先调用 0x4B1D30。大多数对象两者相同，
 * 但某些 JMM 参数会让 0x4B1D30 对 Y 做临时修正。为了让 layer1d 的候选判断和游戏原版尽量一致，
 * 如果安装阶段已经从 0x4B4800 解析出 0x4B1D30，就优先调用它并立即复制结果；解析不到时才退回
 * read_child_rect()。这个函数只读矩形，不调用会修改 +0xB8 latch 的 active query。
 */
static BOOL layer_read_top_level_hit_rect(LPVOID object, HudRect* rect)
{
    LONG* values;
    LONG width;
    LONG height;

    if (!object || !rect) {
        return FALSE;
    }

    if (!g_original_ui_get_hit_rect) {
        return read_child_rect(object, rect);
    }

    values = g_original_ui_get_hit_rect(object);
    if (!values) {
        return FALSE;
    }

    width = values[2];
    height = values[3];
    if (width <= 0 || height <= 0) {
        rect->valid = FALSE;
        return FALSE;
    }

    rect->left = values[0];
    rect->top = values[1];
    rect->right = rect->left + width - 1;
    rect->bottom = rect->top + height - 1;
    rect->valid = TRUE;
    return TRUE;
}

/*
 * 排除世界根、整屏遮罩等“几乎覆盖整个逻辑画面”的对象。
 * 这些对象当然会和 HUD 相交，但它们绝不是本轮要提升的菜单辅助面板。
 */
static BOOL layer_rect_looks_fullscreen(const HudRect* rect)
{
    LONG width;
    LONG height;

    if (!rect || !rect->valid || g_target_width == 0u || g_target_height == 0u) {
        return FALSE;
    }

    width = rect->right - rect->left + 1;
    height = rect->bottom - rect->top + 1;

    return (width >= (LONG)((g_target_width * 7u) / 8u) &&
            height >= (LONG)((g_target_height * 7u) / 8u)) ? TRUE : FALSE;
}

/*
 * 在短生命周期跟踪表里按对象指针查找一项。
 *
 * 这个函数本身不解引用 object；所以即使表里保存的是上一帧已经消失的地址，单纯查表也不会访问坏内存。
 * 真正读取 vtable/active/矩形只会发生在“这个指针又从当前真实顶层链里被枚举出来”以后。
 */
static TrackedAuxiliaryObject* layer_find_tracked_auxiliary_slot(LPVOID object)
{
    DWORD i;

    if (!object) {
        return (TrackedAuxiliaryObject*)0;
    }

    for (i = 0u; i < LAYER_TRACKED_AUXILIARY_MAX; ++i) {
        if (g_layer_tracked_auxiliary[i].object == object) {
            return &g_layer_tracked_auxiliary[i];
        }
    }

    return (TrackedAuxiliaryObject*)0;
}

/*
 * 判断“当前这个真实对象”是否仍是我们之前确认过的独立辅助面板。
 *
 * 调用者必须已经从当前游戏对象/顶层链拿到了 object，因此这里读取 object->vtable 是安全的。
 * 指针相同但 vtable 不同，说明该地址已经被别的对象复用，立即视为不是旧面板。
 */
static BOOL layer_is_tracked_auxiliary_object(LPVOID object)
{
    TrackedAuxiliaryObject* item;
    BYTE* vtable;

    item = layer_find_tracked_auxiliary_slot(object);
    if (!item || !item->object) {
        return FALSE;
    }

    vtable = *(BYTE**)object;
    return (vtable && vtable == item->vtable) ? TRUE : FALSE;
}

/*
 * 第一次在“合法主菜单上下文”里确认一个独立面板以后，把它加入短生命周期跟踪表。
 *
 * seen_epoch 直接写成本帧编号，表示这个对象刚刚就是从当前真实顶层链发现的。
 * 表满时失败开放：不再新增跟踪对象，原来的 layer1c 行为仍然保留，不会覆盖数组或猜测对象。
 */
static void layer_track_auxiliary_object(LPVOID object)
{
    TrackedAuxiliaryObject* item;
    BYTE* vtable;
    DWORD i;

    if (!object) {
        return;
    }

    vtable = *(BYTE**)object;
    if (!vtable || (DWORD)vtable < 0x00400000u || (DWORD)vtable >= 0x00600000u) {
        return;
    }

    item = layer_find_tracked_auxiliary_slot(object);
    if (item) {
        /* 同一个地址若换了 vtable，就把它当成全新的对象重新开始生命周期。 */
        if (item->vtable != vtable) {
            item->vtable = vtable;
            item->detached_keep_logged = FALSE;
        }
        item->seen_epoch = g_layer_tracking_epoch;
        return;
    }

    for (i = 0u; i < LAYER_TRACKED_AUXILIARY_MAX; ++i) {
        item = &g_layer_tracked_auxiliary[i];
        if (!item->object) {
            item->object = object;
            item->vtable = vtable;
            item->seen_epoch = g_layer_tracking_epoch;
            item->detached_keep_logged = FALSE;
            return;
        }
    }

    if (!g_layer_tracking_capacity_logged) {
        g_layer_tracking_capacity_logged = TRUE;
        append_runtime_line("[警告] 辅助GUI短生命周期跟踪表已满；额外独立面板保持未跟踪时的原有行为");
    }
}

/*
 * 清空一项跟踪记录。
 * 这里只把我们自己的四个字段归零，绝不写游戏对象，也不尝试调用它的析构函数。
 */
static void layer_clear_tracked_auxiliary_slot(TrackedAuxiliaryObject* item)
{
    if (!item) {
        return;
    }

    item->object = (LPVOID)0;
    item->vtable = (BYTE*)0;
    item->seen_epoch = 0u;
    item->detached_keep_logged = FALSE;
}

/*
 * 离开 GAMEPLAY、HUD 不存在或顶层链暂时不可用时，直接忘掉全部短生命周期对象。
 * 下一次合法菜单打开时可以重新发现；这比跨场景保存旧对象地址安全得多。
 */
static void layer_clear_all_tracked_auxiliary_objects(void)
{
    DWORD i;

    for (i = 0u; i < LAYER_TRACKED_AUXILIARY_MAX; ++i) {
        layer_clear_tracked_auxiliary_slot(&g_layer_tracked_auxiliary[i]);
    }
}

/*
 * 每次最外层 UI manager Draw 开始前刷新一次短生命周期表。
 *
 * 步骤很机械：
 *   1. epoch + 1，相当于“这是新的一帧验证”；
 *   2. 只遍历真实正向顶层 Draw 链从 head 到 HUD 的对象；
 *   3. 只有本来就在跟踪表中的对象，才检查它现在是否仍然：vtable 相同、active、不是全屏根、与 HUD 相交；
 *   4. 通过就把 seen_epoch 更新到本帧；
 *   5. 扫描结束后，任何没在本帧重新看到的旧记录立即清掉。
 *
 * 因为我们绝不会为了验证旧记录去直接解引用“表里的旧指针”，所以对象销毁以后也不会产生悬空指针读取。
 */
static void layer_refresh_tracked_auxiliary_objects(LPVOID manager, LPVOID hud)
{
    LPVOID node;
    HudRect hud_rect;
    DWORD guard = 0u;
    DWORD i;

    if (!manager || !hud || !g_gameplay_profile_active || g_strategy_transition_in_progress ||
        !layer_read_top_level_hit_rect(hud, &hud_rect)) {
        layer_clear_all_tracked_auxiliary_objects();
        return;
    }

    ++g_layer_tracking_epoch;
    if (g_layer_tracking_epoch == 0u) {
        /* DWORD 回绕在实际游戏时间里几乎不可能发生；仍然显式处理，避免 0 和“空记录”语义混在一起。 */
        g_layer_tracking_epoch = 1u;
        for (i = 0u; i < LAYER_TRACKED_AUXILIARY_MAX; ++i) {
            if (g_layer_tracked_auxiliary[i].object) {
                g_layer_tracked_auxiliary[i].seen_epoch = 0u;
            }
        }
    }

    node = *(LPVOID*)((BYTE*)manager + 0x1Cu);
    while (node && node != hud && guard < 128u) {
        TrackedAuxiliaryObject* item = layer_find_tracked_auxiliary_slot(node);

        if (item) {
            BYTE* vtable = *(BYTE**)node;
            HudRect rect;

            if (vtable == item->vtable &&
                child_active_for_diagnostic(node) &&
                layer_read_top_level_hit_rect(node, &rect) &&
                !layer_rect_looks_fullscreen(&rect) &&
                layer_rects_intersect(&rect, &hud_rect)) {
                item->seen_epoch = g_layer_tracking_epoch;
            }
        }

        node = *(LPVOID*)((BYTE*)node + 0x08u);
        ++guard;
    }

    for (i = 0u; i < LAYER_TRACKED_AUXILIARY_MAX; ++i) {
        TrackedAuxiliaryObject* item = &g_layer_tracked_auxiliary[i];
        if (item->object && item->seen_epoch != g_layer_tracking_epoch) {
            layer_clear_tracked_auxiliary_slot(item);
        }
    }
}

/*
 * 当主菜单已经全部关闭、但一个已确认的独立面板仍然合法存活时，只记一次诊断。
 * 这条日志正好对应本轮用户实机发现的“关闭物品后装备左面板被压回 HUD 下方”边界。
 */
static void layer_log_detached_tracked_auxiliary_once(LPVOID object)
{
    TrackedAuxiliaryObject* item;
    char line[384];

    item = layer_find_tracked_auxiliary_slot(object);
    if (!item || item->detached_keep_logged) {
        return;
    }

    item->detached_keep_logged = TRUE;
    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[运行] 辅助GUI独立面板主窗口关闭后继续置于HUD上方 对象=");
    append_hex32(line, (DWORD)sizeof(line), (DWORD)object);
    str_append(line, (DWORD)sizeof(line), " vtable=");
    append_hex32(line, (DWORD)sizeof(line), (DWORD)item->vtable);
    append_runtime_line(line);
}

/*
 * 判断一个顶层对象是否属于“当前打开菜单需要压在 HUD 上方”的候选。
 *
 * 证明强度从高到低分三类：
 *   1. 它就是 0x0B/0x0D/0x0E 三个已确认主菜单之一；
 *   2. 沿 +0xA4 parent 链能追到主菜单，说明它是被提升成顶层绘制的子菜单/辅助面板；
 *   3. 当前确实有主菜单打开，而且它是活动的、不是全屏根，并且矩形与 HUD 相交。
 *
 * 第 3 条用于覆盖用户实机看到的“左侧独立装备面板”。它只在主菜单上下文、HUD 之前的顶层链扫描中使用，
 * 不会在普通跑图时把任意窗口都当成菜单。
 */
static BOOL layer_is_auxiliary_top_level_candidate(LPVOID object, LPVOID hud, HudRect* out_rect)
{
    HudRect rect;
    HudRect hud_rect;
    BOOL primary_context;
    BOOL tracked_auxiliary;
    BOOL is_primary = FALSE;
    BOOL belongs_to_primary = FALSE;
    BOOL overlaps_hud;

    if (out_rect) {
        out_rect->valid = FALSE;
    }

    if (!object || !hud || object == hud) {
        return FALSE;
    }

    if (!child_active_for_diagnostic(object)) {
        return FALSE;
    }

    if (!layer_read_top_level_hit_rect(object, &rect) || layer_rect_looks_fullscreen(&rect)) {
        return FALSE;
    }

    primary_context = layer_any_primary_menu_active();
    tracked_auxiliary = layer_is_tracked_auxiliary_object(object);

    /*
     * 新对象仍然必须从“主菜单确实打开”的上下文里第一次被证明；没有主菜单时绝不凭几何关系发现新面板。
     * 唯一例外是 layer1d 已经在前一阶段确认并且本帧重新通过顶层链/active/vtable/相交验证的短生命周期对象。
     */
    if (!primary_context && !tracked_auxiliary) {
        return FALSE;
    }

    overlaps_hud = layer_read_top_level_hit_rect(hud, &hud_rect) && layer_rects_intersect(&rect, &hud_rect);

    if (tracked_auxiliary) {
        /* 脱离主菜单以后继续置顶的目的只是在解决 HUD 覆盖，因此不再相交时就没有继续保留的理由。 */
        if (!overlaps_hud) {
            return FALSE;
        }
    } else {
        /* parent 链只在主菜单仍存在的合法上下文里追，避免主 root 生命周期结束后去追一个可能已经失效的旧 parent。 */
        is_primary = layer_is_primary_menu_object(object);
        belongs_to_primary = layer_belongs_to_primary_menu_tree(object);

        if (!is_primary && !belongs_to_primary && !overlaps_hud) {
            return FALSE;
        }
    }

    if (out_rect) {
        *out_rect = rect;
    }
    return TRUE;
}

/*
 * 从正向 Draw 链的开头扫描到 HUD 为止，判断 wanted 是否确实位于 HUD 之前。
 * 这里只读 object+0x08，不对 manager+0x18 或 object+0x0C 作任何假设，更不会写链。
 */
static BOOL layer_object_is_before_hud_in_draw_chain(LPVOID manager, LPVOID hud, LPVOID wanted)
{
    LPVOID node;
    DWORD guard = 0u;

    if (!manager || !hud || !wanted || wanted == hud) {
        return FALSE;
    }

    node = *(LPVOID*)((BYTE*)manager + 0x1Cu);
    while (node && guard < 128u) {
        if (node == wanted) {
            return TRUE;
        }
        if (node == hud) {
            return FALSE;
        }
        node = *(LPVOID*)((BYTE*)node + 0x08u);
        ++guard;
    }

    return FALSE;
}

/* 根据 vtable 找已经安装的动态 Draw Hook。 */
static DynamicLayerHook* layer_find_dynamic_hook_for_object(LPVOID object)
{
    BYTE* vtable;
    DWORD i;

    if (!object) {
        return (DynamicLayerHook*)0;
    }

    vtable = *(BYTE**)object;
    if (!vtable) {
        return (DynamicLayerHook*)0;
    }

    for (i = 0u; i < g_layer_dynamic_hook_count; ++i) {
        if (g_layer_dynamic_hooks[i].vtable == vtable) {
            return &g_layer_dynamic_hooks[i];
        }
    }

    return (DynamicLayerHook*)0;
}

/* 判断 object 是否属于三个已知主菜单 vtable。已知类继续使用各自专用 wrapper，不重复安装动态 Hook。 */
static BOOL layer_object_uses_known_target_vtable(LPVOID object)
{
    BYTE* vtable;
    LayerTargetHook* hooks[3];
    DWORD i;

    if (!object) {
        return FALSE;
    }

    vtable = *(BYTE**)object;
    if (!vtable) {
        return FALSE;
    }

    hooks[0] = &g_layer_equipment;
    hooks[1] = &g_layer_skill;
    hooks[2] = &g_layer_inventory;

    for (i = 0u; i < 3u; ++i) {
        LPVOID target = (hooks[i]->target_slot ? *hooks[i]->target_slot : (LPVOID)0);

        /*
         * 不能只看 draw_slot：某个已知主菜单如果专用 Hook 因结构异常而 fail-open，draw_slot 可能仍为空。
         * 这时动态系统也绝不能“换一个名字重新 Hook 同一 vtable”，否则就绕过了原来的安全停用。
         */
        if ((hooks[i]->draw_slot && hooks[i]->draw_slot == vtable + 0x08u) ||
            (target && *(BYTE**)target == vtable)) {
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * 前置声明：输入覆盖判断需要识别“当前 vtable+0x08 已经安装了哪一种延后绘制 wrapper”，
 * 所以四个 wrapper 的地址必须在定义前先声明。真正函数体仍放在后面，避免把实现拆散。
 */
static int __fastcall layer_equipment_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context);
static int __fastcall layer_skill_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context);
static int __fastcall layer_inventory_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context);
static int __fastcall layer_dynamic_auxiliary_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context);
static int __fastcall layer_main_hud_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context);

/*
 * 给一个已经由“当前菜单上下文 + 顶层链 + 几何关系”证明相关的额外 vtable 安装 Draw wrapper。
 *
 * 安全条件：
 *   - vtable 和原版 Draw 都必须落在主 EXE 已确认地址范围/.text；
 *   - +0x58 仍必须是已经解析的通用布局函数，证明它属于同一套 JMM UI 基类；
 *   - 不覆盖主 HUD，也不覆盖三个已知主菜单已经安装的专用 wrapper；
 *   - 最多记录 16 个 vtable，满了以后完全失败开放。
 */
static BOOL ensure_dynamic_layer_hook_for_object(LPVOID object)
{
    BYTE* vtable;
    BYTE* draw_slot;
    DWORD original_address;
    DynamicLayerHook* item;

    if (!object || object == (g_layer_main_hud_global_slot ? *g_layer_main_hud_global_slot : (LPVOID)0)) {
        return FALSE;
    }

    if (layer_object_uses_known_target_vtable(object)) {
        return TRUE;
    }

    item = layer_find_dynamic_hook_for_object(object);
    if (item) {
        return read_u32(item->draw_slot) == (DWORD)&layer_dynamic_auxiliary_draw_hook;
    }

    if (g_layer_dynamic_hook_count >= LAYER_DYNAMIC_HOOK_MAX) {
        if (!g_layer_dynamic_hook_capacity_logged) {
            g_layer_dynamic_hook_capacity_logged = TRUE;
            append_runtime_line("[警告] 辅助GUI动态Draw Hook数量已达16类；后续未知辅助面板保持原版绘制");
        }
        return FALSE;
    }

    vtable = *(BYTE**)object;
    if (!vtable || (DWORD)vtable < 0x00400000u || (DWORD)vtable >= 0x00600000u) {
        return FALSE;
    }

    draw_slot = vtable + 0x08u;

    /* 主 HUD 的 vtable+0x08 已经有专用 wrapper，动态系统绝不能覆盖同一个槽。 */
    if (draw_slot == g_main_hud_draw_slot || read_u32(draw_slot) == (DWORD)&layer_main_hud_draw_hook) {
        return FALSE;
    }

    original_address = read_u32(draw_slot);
    if (!layer_address_is_game_text(original_address)) {
        return FALSE;
    }

    /*
     * layer1b 曾要求所有额外面板的 +0x58 都必须等于主 HUD 的通用布局函数，但这对“独立左侧面板”没有证据。
     * layer1d 只 Hook Draw，因此不再人为要求它和主 HUD 使用同一个布局虚函数；vtable 在 EXE、原 Draw 在 .text、
     * 对象来自当前顶层链且已通过菜单上下文/几何候选验证，已经是本轮真正需要的安全条件。
     */

    item = &g_layer_dynamic_hooks[g_layer_dynamic_hook_count];
    item->vtable = vtable;
    item->draw_slot = draw_slot;
    item->original_draw = (FnUIDraw)original_address;

    if (!patch_u32(draw_slot, (DWORD)&layer_dynamic_auxiliary_draw_hook)) {
        item->vtable = (BYTE*)0;
        item->draw_slot = (BYTE*)0;
        item->original_draw = (FnUIDraw)0;
        return FALSE;
    }

    ++g_layer_dynamic_hook_count;
    return TRUE;
}

/*
 * 每帧 manager Draw 开始前，只读扫描 HUD 之前的顶层对象，把“当前菜单相关且会与 HUD 发生层级竞争”的
 * 独立辅助类准备好。这里只安装 vtable Draw wrapper，不改变任何对象次序、坐标或输入状态。
 */
static void ensure_dynamic_layer_hooks_for_current_menu(LPVOID manager, LPVOID hud)
{
    LPVOID node;
    DWORD guard = 0u;

    if (!manager || !hud || !layer_any_primary_menu_active()) {
        return;
    }

    node = *(LPVOID*)((BYTE*)manager + 0x1Cu);
    while (node && node != hud && guard < 128u) {
        LPVOID next = *(LPVOID*)((BYTE*)node + 0x08u);
        HudRect rect;

        if (layer_is_auxiliary_top_level_candidate(node, hud, &rect)) {
            /*
             * 只有动态 Hook 真正已经存在/安装成功以后，才把这个独立对象加入短生命周期跟踪。
             * 三个已知主菜单自己有专用 wrapper，不需要跟踪；这里记录的是它们之外的独立面板。
             */
            if (ensure_dynamic_layer_hook_for_object(node) && !layer_is_primary_menu_object(node)) {
                layer_track_auxiliary_object(node);
            }
        }

        node = next;
        ++guard;
    }
}

/*
 * 只有已经真的装有“延后到 HUD 后绘制” wrapper 的对象，才允许参与输入 root 覆盖。
 * 这样可以保证视觉优先级和输入优先级始终成对出现，不会出现“输入已经在上面、画面还在下面”的新不一致。
 */
static BOOL layer_object_has_active_deferred_draw_wrapper(LPVOID object)
{
    BYTE* vtable;
    DWORD draw_value;

    if (!object) {
        return FALSE;
    }

    vtable = *(BYTE**)object;
    if (!vtable) {
        return FALSE;
    }

    draw_value = read_u32(vtable + 0x08u);
    if (draw_value == (DWORD)&layer_equipment_draw_hook ||
        draw_value == (DWORD)&layer_skill_draw_hook ||
        draw_value == (DWORD)&layer_inventory_draw_hook ||
        draw_value == (DWORD)&layer_dynamic_auxiliary_draw_hook) {
        return TRUE;
    }

    return FALSE;
}

/*
 * 完整镜像原版 0x4B4800 对普通顶层 root 的“是否参与输入选择”门槛。
 * 原版先读取 object+0x50 的 JMM 属性 0x0D，只有值等于 1 才继续做矩形和 active 判断。
 * layer1d 手工选择菜单候选时也必须遵守同一门槛，不能把一个纯绘制面板强行变成输入 root。
 */
static BOOL layer_top_level_root_accepts_mouse_input(LPVOID object)
{
    LPVOID property_table;

    if (!object || !g_original_ui_property_get) {
        return FALSE;
    }

    property_table = *(LPVOID*)((BYTE*)object + 0x50u);
    if (!property_table) {
        return FALSE;
    }

    return (g_original_ui_property_get(property_table, 0x0D) == 1) ? TRUE : FALSE;
}

/*
 * 找出当前鼠标点上“应该压在 HUD 上方”的最高辅助 root。
 *
 * layer1a/layer1d 的补画顺序严格保留原正向链顺序：越靠近 HUD 的候选越晚补画，因此视觉上越高。
 * 这里同样从 head 走到 HUD，并不断覆盖 winner；最终得到的最后一个命中候选正好就是视觉上最高的那个。
 */
static LPVOID layer_find_input_override_root(LPVOID manager, LPVOID hud, const POINT* point)
{
    LPVOID node;
    LPVOID winner = (LPVOID)0;
    DWORD guard = 0u;

    if (!manager || !hud || !point) {
        return (LPVOID)0;
    }

    node = *(LPVOID*)((BYTE*)manager + 0x1Cu);
    while (node && node != hud && guard < 128u) {
        HudRect rect;

        if (layer_is_auxiliary_top_level_candidate(node, hud, &rect) &&
            layer_object_has_active_deferred_draw_wrapper(node) &&
            layer_top_level_root_accepts_mouse_input(node) &&
            point_in_hud_rect(point->x, point->y, &rect)) {
            winner = node;
        }

        node = *(LPVOID*)((BYTE*)node + 0x08u);
        ++guard;
    }

    return winner;
}

/*
 * 0x4B4790 -> 0x4B4800 的单次顶层 root 选择 Hook。
 *
 * 先完整调用原版 picker。只有原版最终选择了“主 HUD 本体”，才允许考虑覆盖；如果原版选中了别的窗口、
 * 返回 NULL、处于 FRONTEND/Strategy 切换、主 HUD 没居中或功能开关关闭，全部原样返回。
 *
 * 命中候选后也不写 manager+0x40。我们只是把候选指针作为 0x4B4800 的返回值交还给原版 0x4B4790；
 * 0x4B4790 随后仍会调用原版 0x4B44D0，由游戏自己完成 current-root/focus 生命周期。
 */
static LPVOID __fastcall ui_top_level_pick_layer_hook(LPVOID self, LPVOID unused_edx, const POINT* point)
{
    LPVOID original_result;
    LPVOID hud = (LPVOID)0;
    LPVOID replacement;

    (void)unused_edx;

    if (!g_original_ui_top_level_pick) {
        return (LPVOID)0;
    }

    /*
     * 永远先完整调用原版 picker。
     * 下面所有校正都只是“在原版结果已经拿到以后，极窄地替换这一次返回值”，
     * 不会写 manager+0x40，也不会碰任何 next/previous 链、child 命中字段或鼠标坐标。
     */
    original_result = g_original_ui_top_level_pick(self, point);

    if (g_layer_main_hud_global_slot) {
        hud = *g_layer_main_hud_global_slot;
    }

    /*
     * HUD 居中后的 0x0B / 0x0E 按压动画修复。
     *
     * 这两个 child 的“点击业务”此前已经由世界按下防穿透 + 全局 release 兜底闭合，
     * 但按下动画仍缺失，是因为 press 阶段原版顶层 picker 有时没有把 root 选成已经居中的主 HUD。
     * 如果 root 不对，后面的原版 0x4B44D0 -> 0x4B44F0 -> HUD vtable+0x20 就不会收到这次按下，
     * 所以按钮不会进入原版 pressed/动画状态。
     *
     * 正确修法不是手工写 pressed 字段，也不是重新包装曾经因为隐藏 ESI 语义导致严重回归的 0x4B44F0。
     * 我们只在以下条件全部成立时，把“这一次 picker 返回值”校正成 HUD：
     *   1. 已经真正进入 GAMEPLAY；
     *   2. 主 HUD 居中功能开启；
     *   3. 当前不在 Strategy 设备切换过程中；
     *   4. 鼠标命中 HUD 当前实时布局后的 0x0B 或 0x0E child 矩形。
     *
     * 返回 HUD 以后，真正的按下动画、焦点、release、child 事件仍全部由游戏原版执行。
     * 如果原版本来就已经返回 HUD，本分支只是原样返回，不产生额外状态变化。
     */
    if (g_gameplay_profile_active && g_center_main_hud &&
        !g_strategy_transition_in_progress && hud && point) {
        LPVOID hit_child = (LPVOID)0;
        DWORD hit_id = identify_top_button_at_point(hud, point->x, point->y, &hit_child);

        if (hit_id == 0x0Bu || hit_id == 0x0Eu) {
            if (original_result != hud && g_hud_button_root_correction_log_count < 8u) {
                char line[512];
                ++g_hud_button_root_correction_log_count;
                line[0] = '\0';
                str_append(line, (DWORD)sizeof(line), "[运行] 主HUD按钮按压root校正 ID=");
                append_hex32(line, (DWORD)sizeof(line), hit_id);
                str_append(line, (DWORD)sizeof(line), " 原root=");
                append_hex32(line, (DWORD)sizeof(line), (DWORD)original_result);
                str_append(line, (DWORD)sizeof(line), " -> HUD=");
                append_hex32(line, (DWORD)sizeof(line), (DWORD)hud);
                str_append(line, (DWORD)sizeof(line), " 鼠标=");
                append_int(line, (DWORD)sizeof(line), point->x);
                str_append(line, (DWORD)sizeof(line), ",");
                append_int(line, (DWORD)sizeof(line), point->y);
                str_append(line, (DWORD)sizeof(line), " child=");
                append_hex32(line, (DWORD)sizeof(line), (DWORD)hit_child);
                append_runtime_line(line);
            }
            return hud;
        }
    }

    /*
     * 下面继续执行 layer1d 已实机通过的“辅助菜单覆盖 HUD root”逻辑。
     * 只有图层功能开启，而且原版 picker 确实先选中了 HUD 时，才允许把 root 提升到视觉上位于 HUD 上方的菜单。
     */
    if (!g_auxiliary_ui_above_hud || !g_gameplay_profile_active || !g_center_main_hud ||
        g_strategy_transition_in_progress || !hud || !point) {
        return original_result;
    }

    if (original_result != hud) {
        return original_result;
    }

    replacement = layer_find_input_override_root(self, hud, point);
    if (!replacement) {
        return original_result;
    }

    if (g_layer_input_override_log_count < 16u) {
        char line[512];
        HudRect rect;
        ++g_layer_input_override_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] 辅助GUI输入root覆盖 HUD->");
        append_hex32(line, (DWORD)sizeof(line), (DWORD)replacement);
        str_append(line, (DWORD)sizeof(line), " 鼠标=");
        append_int(line, (DWORD)sizeof(line), point->x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), point->y);
        if (layer_read_top_level_hit_rect(replacement, &rect)) {
            str_append(line, (DWORD)sizeof(line), " root矩形=");
            append_int(line, (DWORD)sizeof(line), rect.left);
            str_append(line, (DWORD)sizeof(line), ",");
            append_int(line, (DWORD)sizeof(line), rect.top);
            str_append(line, (DWORD)sizeof(line), ",");
            append_int(line, (DWORD)sizeof(line), rect.right);
            str_append(line, (DWORD)sizeof(line), ",");
            append_int(line, (DWORD)sizeof(line), rect.bottom);
        }
        append_runtime_line(line);
    }

    return replacement;
}

static BOOL layer_is_current_top_level_object(LPVOID object)
{
    if (!g_layer_ui_manager || !object) {
        return FALSE;
    }

    return *(LPVOID*)((BYTE*)g_layer_ui_manager + 0x20u) == object;
}

/* 返回 control ID 对应的“一次性日志 bit”。 */
static DWORD layer_log_bit_for_id(DWORD control_id)
{
    if (control_id == 0x0Bu) {
        return 0x01u;
    }
    if (control_id == 0x0Du) {
        return 0x02u;
    }
    if (control_id == 0x0Eu) {
        return 0x04u;
    }
    return 0u;
}

/*
 * 把一个目标对象加入“等主 HUD 画完以后再画”的小队列。
 * 队列顺序就是原版 manager 第一次遇到它们的顺序，因此三个辅助窗口彼此之间的原始相对层级仍然保留。
 */
static BOOL layer_defer_target_draw(LPVOID object, FnUIDraw original_draw,
                                    DWORD draw_context, DWORD control_id)
{
    DWORD i;
    DWORD bit;

    if (!object || !original_draw || g_deferred_layer_count >= 16u) {
        return FALSE;
    }

    /* 理论上一个顶层对象一帧只出现一次；这里仍防止异常链表把同一指针重复加入。 */
    for (i = 0u; i < g_deferred_layer_count; ++i) {
        if (g_deferred_layer_draws[i].object == object) {
            return TRUE;
        }
    }

    g_deferred_layer_draws[g_deferred_layer_count].object = object;
    g_deferred_layer_draws[g_deferred_layer_count].original_draw = original_draw;
    g_deferred_layer_draws[g_deferred_layer_count].draw_context = draw_context;
    g_deferred_layer_draws[g_deferred_layer_count].control_id = control_id;
    ++g_deferred_layer_count;

    /* 只在第一次真正发生图层调整时写一条日志，方便实机确认哪个菜单确实原本在 HUD 下面。 */
    bit = layer_log_bit_for_id(control_id);
    if (bit && !(g_layer_runtime_logged_mask & bit)) {
        char line[256];
        g_layer_runtime_logged_mask |= bit;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[运行] 辅助GUI绘制层调整 ID=");
        append_hex32(line, (DWORD)sizeof(line), control_id);
        str_append(line, (DWORD)sizeof(line), " 原顺序位于主HUD之前；本帧改为HUD之后绘制");
        append_runtime_line(line);
    }

    return TRUE;
}

/*
 * 三个 target wrapper 共用这一段判断。
 *
 * 注意 active 判断只是为了“非显示状态完全不改原版顺序”。它只读取基础 UI 的 active 字段，
 * 不会调用输入函数，也不会改变 +0xA8 / hit child。
 *
 * 这里还显式要求 `hook->disabled == FALSE` 和 `depth == 1`：
 *   - 某个类一旦因为结构不符被 fail-open 禁用，即使旧 vtable wrapper 还留在内存里，也只能直通原版 Draw；
 *   - 如果原游戏递归调用 UI manager Draw，递归层 depth 会变成 2，所有 layer1d wrapper 同样只直通原版。
 */
static int layer_target_draw_common(LPVOID self, DWORD draw_context, LayerTargetHook* hook)
{
    if (!hook || !hook->original_draw) {
        return 1;
    }

    if (!hook->disabled &&
        g_layer_draw_scope_depth == 1u &&
        g_layer_draw_scope_active &&
        g_layer_draw_pass_enabled &&
        !g_layer_main_hud_drawn &&
        layer_is_current_top_level_object(self) &&
        hook->target_slot && *hook->target_slot == self &&
        ui_object_active_for_diagnostic(self)) {
        if (layer_defer_target_draw(self, hook->original_draw, draw_context, hook->control_id)) {
            /* manager 原版完全不使用顶层 Draw 的返回值；返回 1 也与这些 UI Draw 的正常成功值一致。 */
            return 1;
        }
    }

    return hook->original_draw(self, draw_context);
}

static int __fastcall layer_equipment_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context)
{
    (void)unused_edx;
    return layer_target_draw_common(self, draw_context, &g_layer_equipment);
}

static int __fastcall layer_skill_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context)
{
    (void)unused_edx;
    return layer_target_draw_common(self, draw_context, &g_layer_skill);
}

static int __fastcall layer_inventory_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context)
{
    (void)unused_edx;
    return layer_target_draw_common(self, draw_context, &g_layer_inventory);
}


/*
 * 独立辅助顶层对象共用的 Draw wrapper。
 *
 * 同一个 wrapper 可以服务多个 vtable，因为进入时通过 self->vtable 在 g_layer_dynamic_hooks 中找回
 * 对应的原版 Draw。只有当前对象确实位于 HUD 前、仍符合当前菜单候选条件、并且正处在外层 manager Draw
 * 的那一次顶层遍历时才延迟；同类对象从其它内部路径调用 Draw 时完整直通原版。
 */
static int __fastcall layer_dynamic_auxiliary_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context)
{
    DynamicLayerHook* hook;
    LPVOID hud;

    (void)unused_edx;

    hook = layer_find_dynamic_hook_for_object(self);
    if (!hook || !hook->original_draw) {
        return 1;
    }

    hud = (g_layer_main_hud_global_slot ? *g_layer_main_hud_global_slot : (LPVOID)0);

    if (g_layer_draw_scope_depth == 1u &&
        g_layer_draw_scope_active &&
        g_layer_draw_pass_enabled &&
        !g_layer_main_hud_drawn &&
        hud &&
        layer_is_current_top_level_object(self) &&
        layer_object_is_before_hud_in_draw_chain(g_layer_ui_manager, hud, self) &&
        layer_is_auxiliary_top_level_candidate(self, hud, (HudRect*)0)) {
        if (layer_defer_target_draw(self, hook->original_draw, draw_context, 0u)) {
            /*
             * 主菜单已经关闭但这个独立面板仍然由短生命周期跟踪保留时，写一次诊断，
             * 方便实机直接确认 layer1d 正在处理这次封版前的最后边界。
             */
            if (!layer_any_primary_menu_active() && layer_is_tracked_auxiliary_object(self)) {
                layer_log_detached_tracked_auxiliary_once(self);
            }
            if (g_layer_dynamic_defer_log_count < 16u) {
                char line[512];
                HudRect rect;
                ++g_layer_dynamic_defer_log_count;
                line[0] = '\0';
                str_append(line, (DWORD)sizeof(line), "[运行] 辅助GUI独立面板延后绘制 对象=");
                append_hex32(line, (DWORD)sizeof(line), (DWORD)self);
                str_append(line, (DWORD)sizeof(line), " vtable=");
                append_hex32(line, (DWORD)sizeof(line), (DWORD)(*(LPVOID*)self));
                if (layer_read_top_level_hit_rect(self, &rect)) {
                    str_append(line, (DWORD)sizeof(line), " 矩形=");
                    append_int(line, (DWORD)sizeof(line), rect.left);
                    str_append(line, (DWORD)sizeof(line), ",");
                    append_int(line, (DWORD)sizeof(line), rect.top);
                    str_append(line, (DWORD)sizeof(line), ",");
                    append_int(line, (DWORD)sizeof(line), rect.right);
                    str_append(line, (DWORD)sizeof(line), ",");
                    append_int(line, (DWORD)sizeof(line), rect.bottom);
                }
                append_runtime_line(line);
            }
            return 1;
        }
    }

    return hook->original_draw(self, draw_context);
}

/*
 * 主 HUD Draw wrapper。
 *
 * 这里先调用原版 HUD Draw。只有它完整返回以后，才逐个调用之前被延迟的辅助 GUI 原版 Draw。
 * 每次补画前临时把 manager+0x20 设置成对应对象，因为原版 manager 本来就会在该对象 Draw 时这样做；
 * 补画全部结束后再恢复成 HUD，保证 manager 自己从 HUD 返回以后仍能沿 HUD+0x08 继续原来的遍历。
 */
static int __fastcall layer_main_hud_draw_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context)
{
    int result;
    DWORD i;

    (void)unused_edx;

    if (!g_original_main_hud_draw) {
        return 1;
    }

    result = g_original_main_hud_draw(self, draw_context);

    if (g_layer_draw_scope_depth == 1u &&
        g_layer_draw_scope_active &&
        g_layer_draw_pass_enabled &&
        g_layer_main_hud_global_slot &&
        *g_layer_main_hud_global_slot == self &&
        layer_is_current_top_level_object(self)) {
        g_layer_main_hud_drawn = TRUE;

        for (i = 0u; i < g_deferred_layer_count; ++i) {
            DeferredLayerDraw* item = &g_deferred_layer_draws[i];
            if (!item->object || !item->original_draw) {
                continue;
            }

            *(LPVOID*)((BYTE*)g_layer_ui_manager + 0x20u) = item->object;
            item->original_draw(item->object, item->draw_context);
        }

        /* 原版 manager 接下来会从“当前 HUD 对象”的 +0x08 继续走，所以必须恢复 current。 */
        *(LPVOID*)((BYTE*)g_layer_ui_manager + 0x20u) = self;
        g_deferred_layer_count = 0u;
    }

    return result;
}

/* 根据 control ID 返回对应 wrapper 地址。 */
static LPVOID layer_wrapper_for_target(DWORD control_id)
{
    if (control_id == 0x0Bu) {
        return (LPVOID)&layer_equipment_draw_hook;
    }
    if (control_id == 0x0Du) {
        return (LPVOID)&layer_skill_draw_hook;
    }
    if (control_id == 0x0Eu) {
        return (LPVOID)&layer_inventory_draw_hook;
    }
    return (LPVOID)0;
}

/*
 * 目标对象在 ASI 初始化很早期可能还没有创建，因此不能要求初始化时立刻拿到它的 vtable。
 * 这个函数会在每次 manager Draw 开始前尝试一次；某个对象第一次存在时才安装它自己的 +0x08 Hook。
 */
static BOOL ensure_layer_target_draw_hook(LayerTargetHook* hook)
{
    LPVOID object;
    BYTE* vtable;
    BYTE* draw_slot;
    DWORD original_address;
    LPVOID wrapper;

    if (!hook || !hook->target_slot) {
        return FALSE;
    }

    /* 已经确认当前类不兼容时不再每帧重复探测；原版 Draw 会继续正常执行。 */
    if (hook->disabled) {
        return FALSE;
    }

    object = *hook->target_slot;
    if (!object) {
        /* “对象这时还不存在”不是错误；下一帧继续等即可。 */
        return TRUE;
    }

    vtable = *(BYTE**)object;
    if (!vtable || (DWORD)vtable < 0x00400000u || (DWORD)vtable >= 0x00600000u) {
        layer_disable_target_hook(hook, "对象 vtable 不在 ComeOn.exe 已确认地址范围");
        return FALSE;
    }

    draw_slot = vtable + 0x08u;
    wrapper = layer_wrapper_for_target(hook->control_id);
    if (!wrapper) {
        layer_disable_target_hook(hook, "没有对应的 layer1d Draw wrapper");
        return FALSE;
    }

    /*
     * 已经安装过以后，同类的新对象应继续使用同一个类 vtable，因此 draw_slot 也应该完全相同。
     * 若突然变成另一套 vtable，不尝试“猜着再 Hook”；保留已经安装的旧类 wrapper，并停止这个 target 的新调整。
     */
    if (hook->installed) {
        if (hook->draw_slot == draw_slot && read_u32(draw_slot) == (DWORD)wrapper) {
            return TRUE;
        }
        layer_disable_target_hook(hook, "已安装后 target 改用了不同 vtable/Draw 槽");
        return FALSE;
    }

    original_address = read_u32(draw_slot);
    if (!layer_address_is_game_text(original_address)) {
        layer_disable_target_hook(hook, "vtable+0x08 原版 Draw 不在主 EXE .text");
        return FALSE;
    }

    /* 三个目标都继承同一基础布局类；+0x58 必须仍然是已解析的原版通用布局函数。 */
    if (!g_original_main_hud_layout || read_u32(vtable + MAIN_HUD_LAYOUT_SLOT) != (DWORD)g_original_main_hud_layout) {
        layer_disable_target_hook(hook, "vtable+0x58 不再指向已确认通用布局函数");
        return FALSE;
    }

    hook->draw_slot = draw_slot;
    hook->original_draw = (FnUIDraw)original_address;

    if (!patch_u32(draw_slot, (DWORD)wrapper)) {
        hook->draw_slot = (BYTE*)0;
        hook->original_draw = (FnUIDraw)0;
        layer_disable_target_hook(hook, "VirtualProtect/写回 vtable+0x08 失败");
        return FALSE;
    }

    hook->installed = TRUE;
    return TRUE;
}

/* 三个对象谁已经创建就安装谁；不存在的对象不会被当成错误。 */
static void ensure_all_layer_target_draw_hooks(void)
{
    (void)ensure_layer_target_draw_hook(&g_layer_equipment);
    (void)ensure_layer_target_draw_hook(&g_layer_skill);
    (void)ensure_layer_target_draw_hook(&g_layer_inventory);
}

/*
 * 唯一 manager Draw callsite 的 scope wrapper。
 *
 * 绝大部分工作仍由原版 g_original_ui_manager_draw 完成；这个 wrapper 只负责：
 *   - 在进入前清空本帧小状态；
 *   - 确认 GAMEPLAY + 主 HUD 居中已启用 + HUD 真正在顶层绘制链；
 *   - 让三个 target/HUD 的 Draw wrapper 知道“当前是同一次顶层绘制”；
 *   - 原版 manager 返回后清理状态。
 */
static void __fastcall ui_manager_draw_layer_scope_hook(LPVOID self, LPVOID unused_edx, DWORD draw_context)
{
    LPVOID hud = (LPVOID)0;
    LPVOID original_current_after_draw = (LPVOID)0;

    (void)unused_edx;

    if (!g_original_ui_manager_draw) {
        return;
    }

    /*
     * 理论上原游戏不会递归进入这个总 Draw。为了让“极端递归”也真正保持原版：
     *   1. 先把 depth 临时加到 2；
     *   2. target/HUD wrapper 只允许 depth==1 时做 layer1d 行为，因此递归这一层一定直通原版 Draw；
     *   3. 原版递归返回后把 depth 减回 1，外层队列和标志完全继续使用。
     *
     * 旧写法虽然没有覆盖外层全局变量，但 depth 仍是 1，递归 Draw 里的 vtable wrapper 仍可能误以为自己属于
     * 外层那一帧并加入外层延迟队列。这里把这个隐患彻底堵住。
     */
    if (g_layer_draw_scope_depth != 0u) {
        ++g_layer_draw_scope_depth;
        g_original_ui_manager_draw(self, draw_context);
        --g_layer_draw_scope_depth;
        return;
    }

    ++g_layer_draw_scope_depth;

    /* 对象创建可能晚于 ASI 初始化，因此每帧开头只做三个非常小的“是否已安装”检查。 */
    ensure_all_layer_target_draw_hooks();

    if (g_layer_main_hud_global_slot) {
        hud = *g_layer_main_hud_global_slot;
    }

    g_layer_ui_manager = self;
    g_deferred_layer_count = 0u;
    g_layer_main_hud_drawn = FALSE;
    g_layer_draw_scope_active = TRUE;

    /*
     * 先刷新上一帧已经确认过的独立辅助对象。
     * 即使这一帧物品/技能/装备主 root 已经全部 inactive，只要独立面板自身仍合法存在，它就会保住短生命周期资格。
     * 新面板仍然不会在这里被发现；新发现只允许发生在下面“主菜单上下文成立”的扫描里。
     */
    if (g_auxiliary_ui_above_hud &&
        g_gameplay_profile_active &&
        g_center_main_hud &&
        !g_strategy_transition_in_progress &&
        hud &&
        layer_draw_chain_contains(self, hud)) {
        layer_refresh_tracked_auxiliary_objects(self, hud);
    } else {
        layer_clear_all_tracked_auxiliary_objects();
    }

    /*
     * layer1d 不再写顶层链。这里仅在当前主菜单上下文成立时，为 HUD 之前的独立辅助面板准备 Draw wrapper。
     * 三个已知主菜单仍使用 layer1a 已经实机证明有效的专用 wrapper。
     */
    if (g_auxiliary_ui_above_hud &&
        g_gameplay_profile_active &&
        g_center_main_hud &&
        !g_strategy_transition_in_progress &&
        hud &&
        layer_draw_chain_contains(self, hud)) {
        ensure_dynamic_layer_hooks_for_current_menu(self, hud);
    }

    /*
     * layer1d 诊断只在真正接近游戏内稳定状态时执行，而且每个目标最多写一次日志。
     * 它记录“原始链索引”，用于确认延后某个菜单是否还会跨过别的顶层 UI；不会修改任何链表节点。
     */
    if (g_gameplay_profile_active && g_center_main_hud &&
        !g_strategy_transition_in_progress && hud && layer_draw_chain_contains(self, hud)) {
        layer_log_chain_relation_once(self, hud, &g_layer_equipment);
        layer_log_chain_relation_once(self, hud, &g_layer_skill);
        layer_log_chain_relation_once(self, hud, &g_layer_inventory);
    }

    /*
     * 只有真正游戏内、独立图层开关开启、主 HUD 确实启用居中、当前不处于 Strategy 设备切换，
     * 而且 HUD 在同一顶层链，才改变这一次绘制顺序。其它所有状态都完整调用原版 manager Draw。
     */
    g_layer_draw_pass_enabled =
        g_auxiliary_ui_above_hud &&
        g_gameplay_profile_active &&
        g_center_main_hud &&
        !g_strategy_transition_in_progress &&
        hud &&
        layer_draw_chain_contains(self, hud);

    /*
     * 统一 Runtime 只广播最外层 UI manager Draw。当前 v0.1-dev1 没有其它模块订阅，
     * 所以不会改变任何实机行为；下一阶段手柄光标/提示层需要 UI 时可以直接订阅，
     * 而不必再次 Hook 这条 manager Draw callsite。
     */
    Runtime_EmitEvent(RUNTIME_EVENT_UI_DRAW_BEGIN, self, draw_context, 0u);
    g_original_ui_manager_draw(self, draw_context);
    Runtime_EmitEvent(RUNTIME_EVENT_UI_DRAW_END, self, draw_context, 0u);

    /*
     * 先记住原版 manager Draw 返回时自己留下的 current 值。正常样本很可能已经是 NULL，
     * 但 layer1d 不应该凭猜测把它强制清零；如果未来兼容版本保留其它哨兵/对象，这个值也必须原样恢复。
     */
    original_current_after_draw = *(LPVOID*)((BYTE*)self + 0x20u);

    /*
     * 正常情况下 deferred 会在 HUD Draw wrapper 里被清空。
     * 如果某个未知版本在我们预扫描以后又动态移除了 HUD，宁可把延迟对象在本帧末尾补画一次，
     * 也不能让菜单整帧消失。这个分支只是安全兜底，并会最多写一条日志提醒继续调查。
     */
    if (g_deferred_layer_count != 0u) {
        DWORD i;
        for (i = 0u; i < g_deferred_layer_count; ++i) {
            DeferredLayerDraw* item = &g_deferred_layer_draws[i];
            if (item->object && item->original_draw) {
                *(LPVOID*)((BYTE*)self + 0x20u) = item->object;
                item->original_draw(item->object, item->draw_context);
            }
        }
        /* 补画结束后恢复“原版 manager Draw 返回时的值”，而不是假定必须为 NULL。 */
        *(LPVOID*)((BYTE*)self + 0x20u) = original_current_after_draw;
        g_deferred_layer_count = 0u;

        if (!g_layer_unexpected_flush_logged) {
            g_layer_unexpected_flush_logged = TRUE;
            append_runtime_line("[警告] 辅助GUI图层：本帧预扫描找到主HUD，但HUD Draw未经过Hook；已在帧末安全补画延迟GUI");
        }
    }

    g_layer_draw_scope_active = FALSE;
    g_layer_draw_pass_enabled = FALSE;
    g_layer_main_hud_drawn = FALSE;
    g_layer_ui_manager = (LPVOID)0;
    --g_layer_draw_scope_depth;
}

/*
 * 安装 layer1d 绘制层 + 单次输入 root 优先级 Hook。
 *
 * 这里会同时验证：
 *   - manager Draw 包装 callsite 唯一；
 *   - call 真正落到“主 HUD 特殊 pass + manager+0x1C 正向链 + object+0x08 Draw”的函数；
 *   - 函数里两次读取的是同一个主 HUD 全局槽；
 *   - 主 HUD vtable+0x08 仍然指向当前 EXE .text 内的原版 Draw。
 *
 * 任何一项不成立都整项拒绝，不会只装半个图层系统。
 */
static BOOL install_auxiliary_ui_draw_layer_hook(const TextRegion* region)
{
    BYTE* wrapper;
    BYTE* call_instruction;
    BYTE* manager_draw;
    DWORD manager_address;
    DWORD hud_slot_a;
    DWORD hud_slot_b;
    DWORD main_hud_draw_address;
    BYTE* input_pick_site;
    BYTE* input_pick_call;
    BYTE* input_picker;
    BYTE* input_hit_rect_call;
    BYTE* input_hit_rect;
    BYTE* input_property_get_call;
    BYTE* input_property_get;

    if (!region || !g_main_hud_draw_slot ||
        !g_top_button_0b_target_slot || !g_top_button_0d_target_slot || !g_top_button_0e_target_slot) {
        return FALSE;
    }

    g_layer_text_start = region->start;
    g_layer_text_end = region->start + region->size;

    wrapper = find_unique_pattern(region,
                                  UI_MANAGER_DRAW_CALLSITE_PATTERN,
                                  UI_MANAGER_DRAW_CALLSITE_MASK,
                                  (DWORD)sizeof(UI_MANAGER_DRAW_CALLSITE_PATTERN));
    if (!wrapper) {
        return FALSE;
    }

    manager_address = read_u32(wrapper + 3u);
    call_instruction = wrapper + 8u;
    manager_draw = decode_rel32_target(call_instruction);

    if (manager_address < 0x00400000u || manager_address >= 0x00600000u ||
        !manager_draw || !layer_address_is_game_text((DWORD)manager_draw)) {
        return FALSE;
    }

    /* 验证 manager Draw 开头的稳定结构，避免误把别的 `mov ecx / call / ret` 包装函数当成目标。 */
    if (manager_draw[0] != 0x56 || manager_draw[1] != 0x8B || manager_draw[2] != 0xF1 ||
        manager_draw[3] != 0x8B || manager_draw[4] != 0x0D ||
        manager_draw[9] != 0x57 ||
        manager_draw[10] != 0x8B || manager_draw[11] != 0x7C || manager_draw[12] != 0x24 || manager_draw[13] != 0x0C ||
        manager_draw[14] != 0x85 || manager_draw[15] != 0xC9 ||
        manager_draw[18] != 0xE8 ||
        manager_draw[27] != 0x8B || manager_draw[28] != 0x0D ||
        manager_draw[33] != 0x57 || manager_draw[34] != 0xE8 ||
        manager_draw[39] != 0x8B || manager_draw[40] != 0x4E || manager_draw[41] != 0x1C ||
        /* 0x31: mov eax,[ecx]；0x33: push edi；0x34: call [eax+0x08]，这是顶层对象真正 Draw。 */
        manager_draw[49] != 0x8B || manager_draw[50] != 0x01 ||
        manager_draw[51] != 0x57 ||
        manager_draw[52] != 0xFF || manager_draw[53] != 0x50 || manager_draw[54] != 0x08) {
        return FALSE;
    }

    /*
     * manager Draw 在遍历顶层链以前还有一次独立 HUD 特殊 pass（本体已逆向到 0x004C3090）。
     * layer1d 不 Hook、也不重放这个特殊 pass；这里只确认该 CALL 仍然落在游戏 .text，防止结构已经变化。
     * 因为它本来就早于所有顶层窗口绘制，所以本轮只改变菜单与主 HUD 顶层 Draw 的相对先后，
     * 不会让特殊 pass 被额外执行第二次。
     */
    if (!layer_address_is_game_text((DWORD)decode_rel32_target(manager_draw + 34u))) {
        return FALSE;
    }

    hud_slot_a = read_u32(manager_draw + 5u);
    hud_slot_b = read_u32(manager_draw + 29u);
    if (hud_slot_a != hud_slot_b || hud_slot_a < 0x00400000u || hud_slot_a >= 0x00600000u) {
        return FALSE;
    }

    main_hud_draw_address = read_u32(g_main_hud_draw_slot);
    if (!layer_address_is_game_text(main_hud_draw_address)) {
        return FALSE;
    }

    /*
     * 定位 0x4B4790 -> 0x4B4800 的唯一顶层 root picker call。
     * call 位于签名 +15；picker 内部必须同时保留“manager+0x18 起步、manager+0x20 当前节点、
     * object+0x0C 反向前进、调用矩形函数”的结构。我们只验证这些实际依赖，不再假定链表头尾是 NULL。
     */
    input_pick_site = find_unique_pattern(region,
                                          UI_TOP_LEVEL_PICK_CALLSITE_PATTERN,
                                          UI_TOP_LEVEL_PICK_CALLSITE_MASK,
                                          (DWORD)sizeof(UI_TOP_LEVEL_PICK_CALLSITE_PATTERN));
    if (!input_pick_site) {
        return FALSE;
    }

    input_pick_call = input_pick_site + 15u;
    if (input_pick_call[0] != 0xE8) {
        return FALSE;
    }

    input_picker = decode_rel32_target(input_pick_call);
    if (!input_picker || !layer_address_is_game_text((DWORD)input_picker)) {
        return FALSE;
    }

    if (input_picker[0] != 0x53 || input_picker[1] != 0x8B || input_picker[2] != 0xD9 ||
        input_picker[0x4Cu] != 0x8B || input_picker[0x4Du] != 0x73 || input_picker[0x4Eu] != 0x18 ||
        input_picker[0x51u] != 0x89 || input_picker[0x52u] != 0x73 || input_picker[0x53u] != 0x20 ||
        input_picker[0x65u] != 0x8B || input_picker[0x66u] != 0xCE || input_picker[0x67u] != 0xE8 ||
        input_picker[0x9Fu] != 0x8B || input_picker[0xA0u] != 0x43 || input_picker[0xA1u] != 0x20 ||
        input_picker[0xA2u] != 0x8B || input_picker[0xA3u] != 0x4B || input_picker[0xA4u] != 0x1C ||
        input_picker[0xADu] != 0x8B || input_picker[0xAEu] != 0x40 || input_picker[0xAFu] != 0x0C ||
        input_picker[0xB0u] != 0x89 || input_picker[0xB1u] != 0x43 || input_picker[0xB2u] != 0x20) {
        return FALSE;
    }

    input_hit_rect_call = input_picker + 0x67u;
    input_hit_rect = decode_rel32_target(input_hit_rect_call);
    if (!input_hit_rect || !layer_address_is_game_text((DWORD)input_hit_rect)) {
        return FALSE;
    }

    /* picker+0x5B 是 `push 0x0D` 后调用 JMM 属性读取函数 0x4D0210 的 rel32 CALL。 */
    input_property_get_call = input_picker + 0x5Bu;
    if (input_property_get_call[0] != 0xE8) {
        return FALSE;
    }
    input_property_get = decode_rel32_target(input_property_get_call);
    if (!input_property_get || !layer_address_is_game_text((DWORD)input_property_get)) {
        return FALSE;
    }

    /* 到这里所有验证都完成，下面才真正写任何 Hook。 */
    g_layer_ui_manager = (LPVOID)manager_address;
    g_layer_main_hud_global_slot = (LPVOID*)hud_slot_a;
    g_original_ui_manager_draw = (FnUIManagerDraw)manager_draw;
    g_original_main_hud_draw = (FnUIDraw)main_hud_draw_address;
    g_original_ui_top_level_pick = (FnUITopLevelPick)input_picker;
    g_original_ui_get_hit_rect = (FnUIGetHitRect)input_hit_rect;
    g_original_ui_property_get = (FnUIPropertyGet)input_property_get;

    g_layer_equipment.target_slot = g_top_button_0b_target_slot;
    g_layer_skill.target_slot = g_top_button_0d_target_slot;
    g_layer_inventory.target_slot = g_top_button_0e_target_slot;

    /*
     * 写 Hook 的顺序刻意从“最容易恢复”到“总入口”：HUD Draw -> input picker call -> manager Draw call。
     * 任一后续步骤失败，都把前面已经写过的槽恢复原值，绝不留下半套 layer1d。
     */
    if (!patch_u32(g_main_hud_draw_slot, (DWORD)&layer_main_hud_draw_hook)) {
        return FALSE;
    }

    if (!patch_rel32_call(input_pick_call, (LPVOID)&ui_top_level_pick_layer_hook)) {
        patch_u32(g_main_hud_draw_slot, main_hud_draw_address);
        g_original_ui_top_level_pick = (FnUITopLevelPick)0;
        g_original_ui_get_hit_rect = (FnUIGetHitRect)0;
        g_original_ui_property_get = (FnUIPropertyGet)0;
        return FALSE;
    }

    if (!patch_rel32_call(call_instruction, (LPVOID)&ui_manager_draw_layer_scope_hook)) {
        patch_rel32_call(input_pick_call, (LPVOID)input_picker);
        patch_u32(g_main_hud_draw_slot, main_hud_draw_address);
        g_original_ui_manager_draw = (FnUIManagerDraw)0;
        g_original_main_hud_draw = (FnUIDraw)0;
        g_original_ui_top_level_pick = (FnUITopLevelPick)0;
        g_original_ui_get_hit_rect = (FnUIGetHitRect)0;
        g_original_ui_property_get = (FnUIPropertyGet)0;
        g_layer_main_hud_global_slot = (LPVOID*)0;
        return FALSE;
    }

    return TRUE;
}

/*
 * 安装“只居中底部主 HUD”的 vtable hook。
 *
 * 这里没有对所有 UI 坐标做全局 +X：
 *   - 小地图、右侧按钮等是其他顶层 JMM 模块，它们继续用原游戏的右锚定逻辑；
 *   - 只改主 HUD 根对象自己的布局入口；
 *   - 主 HUD 子控件原本就是相对父节点布局，所以会整体跟随。
 */
static BOOL install_main_hud_center_hook(const TextRegion* region)
{
    BYTE* root_ctor;
    BYTE* generic_layout;
    DWORD vtable_address;
    BYTE* destructor_slot;
    BYTE* draw_slot;
    BYTE* event_slot;
    BYTE* layout_slot;
    DWORD destructor_slot_value;
    DWORD draw_slot_value;
    DWORD event_slot_value;
    DWORD layout_slot_value;
    const BYTE* event_code;

    root_ctor = find_unique_pattern(region,
                                    MAIN_HUD_ROOT_PATTERN,
                                    MAIN_HUD_ROOT_MASK,
                                    (DWORD)sizeof(MAIN_HUD_ROOT_PATTERN));

    generic_layout = find_unique_pattern(region,
                                         UI_LAYOUT_PATTERN,
                                         UI_LAYOUT_MASK,
                                         (DWORD)sizeof(UI_LAYOUT_PATTERN));

    if (!root_ctor || !generic_layout) {
        return FALSE;
    }

    /* C7 06 后面的 imm32 就是主 HUD 类 vtable 地址。 */
    vtable_address = read_u32(root_ctor + 2u);

    if (vtable_address < 0x00400000u || vtable_address >= 0x00600000u) {
        return FALSE;
    }

    destructor_slot = (BYTE*)(vtable_address + MAIN_HUD_DESTRUCTOR_SLOT);
    draw_slot = (BYTE*)(vtable_address + MAIN_HUD_DRAW_SLOT);
    event_slot = (BYTE*)(vtable_address + MAIN_HUD_EVENT_SLOT);
    layout_slot = (BYTE*)(vtable_address + MAIN_HUD_LAYOUT_SLOT);
    destructor_slot_value = read_u32(destructor_slot);
    draw_slot_value = read_u32(draw_slot);
    event_slot_value = read_u32(event_slot);
    layout_slot_value = read_u32(layout_slot);

    /*
     * +0x58 必须仍然指向唯一找到的通用布局函数；
     * +0x24 必须仍然是我们已经闭合的主 HUD 事件函数形状。
     */
    /*
     * +0x00 必须是当前类自己的 scalar deleting destructor。
     * 原版函数头固定为 push esi / mov esi,ecx / call <real dtor>；这个形状在 Steam/非 Steam 样本一致。
     */
    if (destructor_slot_value < 0x00400000u || destructor_slot_value >= 0x00600000u ||
        ((BYTE*)destructor_slot_value)[0] != 0x56 ||
        ((BYTE*)destructor_slot_value)[1] != 0x8B ||
        ((BYTE*)destructor_slot_value)[2] != 0xF1 ||
        ((BYTE*)destructor_slot_value)[3] != 0xE8) {
        return FALSE;
    }

    if (layout_slot_value != (DWORD)generic_layout) {
        return FALSE;
    }

    /* +0x08 必须仍然指向主 HUD 自己的原版 Draw；layer1 后面只会包装这个槽，不改其它虚函数。 */
    if (draw_slot_value < 0x00400000u || draw_slot_value >= 0x00600000u) {
        return FALSE;
    }

    if (event_slot_value < 0x00400000u || event_slot_value >= 0x00600000u) {
        return FALSE;
    }

    event_code = (const BYTE*)event_slot_value;

    /*
     * 这里只验证“事件函数先经 vtable+0x30 更新命中 child，后面读取 self+0xA8 / child+0x28”的稳定结构。
     * 0x0F/0x10 仍属于历史逆向背景，但 test5 的真正顶部按钮修复只依赖已实机闭合的 0x0B/0x0E 分支。
     */
    if (event_code[0x19] != 0xFF || event_code[0x1A] != 0x50 || event_code[0x1B] != 0x30 ||
        event_code[0xD0] != 0x8B || event_code[0xD1] != 0xBF ||
        event_code[0xD2] != 0xA8 || event_code[0xD3] != 0x00 ||
        event_code[0xD4] != 0x00 || event_code[0xD5] != 0x00 ||
        event_code[0xDE] != 0x8B || event_code[0xDF] != 0x47 || event_code[0xE0] != 0x28) {
        return FALSE;
    }

    /*
     * v0.3-test4 的实机日志已经证明，用户点“顶部两个圆形按钮”时原版 hit child 分别是 0x0B 和 0x0E。
     * 现在继续从同一个事件函数里验证这两条原版分支，并解析它们控制的顶层窗口全局指针槽：
     *
     *   0x0E: 0x4C3F53  cmp eax,0x0E
     *         0x4C3F58  mov ecx,[absolute]
     *
     *   0x0B: 0x4C3FFB  cmp eax,0x0B
     *         0x4C4000  mov ecx,[absolute]
     *
     * 这里用相对事件函数开头的偏移验证机器码，再读 imm32；不把 0x55xxxx 绝对地址写死成兼容条件。
     */
    if (event_code[0x113] != 0x83 || event_code[0x114] != 0xF8 || event_code[0x115] != 0x0E ||
        event_code[0x118] != 0x8B || event_code[0x119] != 0x0D ||
        event_code[0x1BB] != 0x83 || event_code[0x1BC] != 0xF8 || event_code[0x1BD] != 0x0B ||
        event_code[0x1C0] != 0x8B || event_code[0x1C1] != 0x0D ||
        event_code[0x1E5] != 0x83 || event_code[0x1E6] != 0xF8 || event_code[0x1E7] != 0x0D ||
        event_code[0x1EA] != 0x8B || event_code[0x1EB] != 0x0D) {
        return FALSE;
    }

    g_top_button_0e_target_slot = (LPVOID*)read_u32(event_code + 0x11Au);
    g_top_button_0b_target_slot = (LPVOID*)read_u32(event_code + 0x1C2u);
    g_top_button_0d_target_slot = (LPVOID*)read_u32(event_code + 0x1ECu);

    if ((DWORD)g_top_button_0e_target_slot < 0x00400000u ||
        (DWORD)g_top_button_0e_target_slot >= 0x00600000u ||
        (DWORD)g_top_button_0b_target_slot < 0x00400000u ||
        (DWORD)g_top_button_0b_target_slot >= 0x00600000u ||
        (DWORD)g_top_button_0d_target_slot < 0x00400000u ||
        (DWORD)g_top_button_0d_target_slot >= 0x00600000u) {
        g_top_button_0e_target_slot = (LPVOID*)0;
        g_top_button_0b_target_slot = (LPVOID*)0;
        g_top_button_0d_target_slot = (LPVOID*)0;
        return FALSE;
    }

    /*
     * +0x24 只做静态解析，不再写回 vtable。
     * 这里仍把原版函数保存下来，并显式引用历史诊断桥接，目的是让源码把已经验证过的调用约定和
     * 失败路径完整保留下来；正式运行时不会把 main_hud_event_hook 安装到游戏对象。
     */
    g_original_main_hud_event = (FnMainHudEvent)event_slot_value;
    (void)&main_hud_event_hook;

    g_original_main_hud_destructor = (FnMainHudDestructor)destructor_slot_value;
    g_original_main_hud_layout = (FnUILayout)generic_layout;
    g_main_hud_draw_slot = draw_slot;

    if (!patch_u32(destructor_slot, (DWORD)&main_hud_destructor_hook)) {
        g_original_main_hud_destructor = (FnMainHudDestructor)0;
        g_original_main_hud_event = (FnMainHudEvent)0;
        g_original_main_hud_layout = (FnUILayout)0;
        g_top_button_0b_target_slot = (LPVOID*)0;
        g_top_button_0d_target_slot = (LPVOID*)0;
        g_top_button_0e_target_slot = (LPVOID*)0;
        g_main_hud_draw_slot = (BYTE*)0;
        return FALSE;
    }

    if (!patch_u32(layout_slot, (DWORD)&main_hud_layout_hook)) {
        /* layout 安装失败时把 destructor vtable 槽恢复原值，避免只装一半生命周期 hook。 */
        patch_u32(destructor_slot, destructor_slot_value);
        g_original_main_hud_destructor = (FnMainHudDestructor)0;
        g_original_main_hud_event = (FnMainHudEvent)0;
        g_original_main_hud_layout = (FnUILayout)0;
        g_top_button_0b_target_slot = (LPVOID*)0;
        g_top_button_0d_target_slot = (LPVOID*)0;
        g_top_button_0e_target_slot = (LPVOID*)0;
        g_main_hud_draw_slot = (BYTE*)0;
        return FALSE;
    }

    return TRUE;
}

/*
 * 读取屏幕宽高时优先使用我们从 USER32 直接解析出的函数。
 * 如果解析不到，就回退到 ComeOn.exe 自己的 IAT。
 */
static int get_screen_metric(int index)
{
    if (g_GetSystemMetrics) {
        return g_GetSystemMetrics(index);
    }

    return GAME_GetSystemMetrics(index);
}

/* ============================================================================================== */
/* 10. 读取配置                                                                                         */
/* ============================================================================================== */

typedef struct DisplayFixConfig {
    BOOL enable;
    BOOL fix_font_dpi;
    BOOL center_main_hud;
    BOOL auxiliary_ui_above_hud;
    DWORD base_height;
    DWORD aspect_width;
    DWORD aspect_height;
} DisplayFixConfig;

static void load_config(DisplayFixConfig* config)
{
    char ratio[64];
    int screen_width;
    int screen_height;

    if (!config) {
        return;
    }

    /* 默认值就是当前推荐方案：启用、字体修复、480 基准、比例自动。 */
    config->enable = TRUE;
    config->fix_font_dpi = TRUE;
    config->center_main_hud = TRUE;
    config->auxiliary_ui_above_hud = TRUE;
    config->base_height = 480u;
    config->aspect_width = 0u;
    config->aspect_height = 0u;

    if (!g_GetPrivateProfileIntA || !g_GetPrivateProfileStringA) {
        /* 理论上现代 Windows 都能解析到这两个 API；失败时仍使用默认值。 */
        screen_width = get_screen_metric(SM_CXSCREEN);
        screen_height = get_screen_metric(SM_CYSCREEN);
        config->aspect_width = (screen_width > 0) ? (DWORD)screen_width : 4u;
        config->aspect_height = (screen_height > 0) ? (DWORD)screen_height : 3u;
        return;
    }

    config->enable = g_GetPrivateProfileIntA("Display", "Enable", 1, g_ini_path) ? TRUE : FALSE;
    config->fix_font_dpi = g_GetPrivateProfileIntA("Font", "FixDPI", 1, g_ini_path) ? TRUE : FALSE;
    config->center_main_hud = g_GetPrivateProfileIntA("GUI", "CenterMainHUD", 1, g_ini_path) ? TRUE : FALSE;
    config->auxiliary_ui_above_hud = g_GetPrivateProfileIntA("GUI", "AuxiliaryUIAboveHUD", 1, g_ini_path) ? TRUE : FALSE;
    {
        int configured_height = g_GetPrivateProfileIntA("Display", "BaseHeight", 480, g_ini_path);

        /*
         * v0.3-test1 起取消 480/600 白名单，也不再设置人为最大值。
         * 用户可以把 BaseHeight 当成“世界缩放 / 类 FOV”参数自由试验。
         * 唯一的配置级保护是：0 和负数没有几何意义，因此回退到稳定基线 480。
         */
        config->base_height = (configured_height > 0) ? (DWORD)configured_height : 480u;
    }

    ratio[0] = '\0';
    g_GetPrivateProfileStringA("Display", "AspectRatio", "Auto", ratio, (DWORD)sizeof(ratio), g_ini_path);

    if (str_equal_icase(ratio, "Auto")) {
        /* Auto：读取当前系统/兼容层向游戏报告的屏幕宽高。 */
        screen_width = get_screen_metric(SM_CXSCREEN);
        screen_height = get_screen_metric(SM_CYSCREEN);

        if (screen_width > 0 && screen_height > 0) {
            config->aspect_width = (DWORD)screen_width;
            config->aspect_height = (DWORD)screen_height;
        } else {
            /* 极端失败兜底：退回原生 4:3，而不是产生 0 宽度。 */
            config->aspect_width = 4u;
            config->aspect_height = 3u;
        }
    } else {
        /* 非 Auto 时不做“比例白名单”，只要是合法 W:H 就接受，所以 5:4、3:2、32:9 都能用。 */
        if (!parse_ratio(ratio, &config->aspect_width, &config->aspect_height)) {
            /* 用户写错时也退回 Auto，而不是让游戏拿到异常分辨率。 */
            screen_width = get_screen_metric(SM_CXSCREEN);
            screen_height = get_screen_metric(SM_CYSCREEN);
            config->aspect_width = (screen_width > 0) ? (DWORD)screen_width : 4u;
            config->aspect_height = (screen_height > 0) ? (DWORD)screen_height : 3u;
        }
    }
}

/* ============================================================================================== */
/* 11. 初始化系统 API                                                                                   */
/* ============================================================================================== */

static BOOL resolve_required_apis(void)
{
    HMODULE kernel32;
    HMODULE user32;

    /* kernel32.dll 一定已经由 ComeOn.exe 自己加载；这里不主动 LoadLibrary 新模块。 */
    kernel32 = GAME_GetModuleHandleA("kernel32.dll");
    if (!kernel32) {
        return FALSE;
    }

    g_VirtualProtect = (FnVirtualProtect)GAME_GetProcAddress(kernel32, "VirtualProtect");
    g_FlushInstructionCache = (FnFlushInstructionCache)GAME_GetProcAddress(kernel32, "FlushInstructionCache");
    g_GetPrivateProfileIntA = (FnGetPrivateProfileIntA)GAME_GetProcAddress(kernel32, "GetPrivateProfileIntA");
    g_GetPrivateProfileStringA = (FnGetPrivateProfileStringA)GAME_GetProcAddress(kernel32, "GetPrivateProfileStringA");

    /*
     * user32.dll 同样已经被游戏加载。
     * 直接解析 GetSystemMetrics，主要是为了让 AspectRatio=Auto 尽量读取桌面真实比例，
     * 而不是被 cnc-ddraw 可能存在的“游戏 IAT 虚拟分辨率”影响。
     */
    user32 = GAME_GetModuleHandleA("user32.dll");
    if (user32) {
        g_GetSystemMetrics = (FnGetSystemMetrics)GAME_GetProcAddress(user32, "GetSystemMetrics");
    }

    /* 真正写代码必须有 VirtualProtect；其余 API 都有默认值或回退路径。 */
    return g_VirtualProtect ? TRUE : FALSE;
}

/* ============================================================================================== */
/* 12. 插件总初始化                                                                                     */
/* ============================================================================================== */

static void initialize_display_fix(void)
{
    char module_path[1024];
    TextRegion text_region;
    DisplayFixConfig config;
    DWORD target_width;
    int font_result = 0;
    BOOL resolution_result = FALSE;
    BOOL layout_result = FALSE;
    BOOL jmm_context_result = FALSE;
    BOOL hud_result = FALSE;
    BOOL layer_result = FALSE;
    BOOL global_release_result = FALSE;
    BOOL world_press_result = FALSE;
    BOOL strategy_state_result = FALSE;
    BOOL steam_class_atom_guard_installed = FALSE;
    LONG hud_delta = 0;

    /* 用最简单的单次标志防止 DllMain + InitializeASI 重复执行。 */
    if (g_initialized != 0) {
        return;
    }
    g_initialized = 1;

    g_log_buffer[0] = '\0';
    module_path[0] = '\0';

    /* 先算出 INI 和日志路径。即使后面初始化失败，也尽量留下诊断日志。 */
    GAME_GetModuleFileNameA((HMODULE)g_self_module, module_path, (DWORD)sizeof(module_path));
    make_sibling_path(module_path, "BladeSwordQOL.ini", g_ini_path, (DWORD)sizeof(g_ini_path));
    make_sibling_path(module_path, "BladeSwordQOL.log", g_log_path, (DWORD)sizeof(g_log_path));

    log_line("BladeSwordQOL v0.1-dev1");
    log_line("[Runtime] 游戏Profile=刀剑封魔录外传：上古传说 / DisplayFix后端=v0.2.3-stripe1等价迁移");
    log_line("架构：Win32/x86 ASI，基于内容签名的运行时补丁");

    if (!resolve_required_apis()) {
        log_line("[失败] 无法解析 VirtualProtect");
        flush_log_file();
        return;
    }

    if (!get_main_text_region(&text_region)) {
        log_line("[失败] 无法定位主 EXE 的 .text 代码段");
        flush_log_file();
        return;
    }

    /*
     * 先把实际 INI 绝对路径写进日志。
     * DisplayFix 始终读取“与当前 ASI 同目录”的 DisplayFix.ini；
     * 如果日志显示 BaseHeight 回退成 480，用户可以直接核对自己编辑的是否就是这里这份文件。
     */
    log_text("[信息] 配置文件路径=", g_ini_path);

    load_config(&config);

    /*
     * v0.1-test1 的实机反馈暴露了一个纯配置层错误：
     * Display.Enable=0 时，旧代码会在这里直接 return，导致下面本应独立的 Font.FixDPI 也完全没有机会执行。
     *
     * 这和 INI 的分节语义不一致。Display.Enable 只应该控制“宽屏 / HUD / 输入”这一整套显示运行时修复，
     * [Font] FixDPI 则是一个独立的字体修复开关。用户可能只想保留原版 4:3 分辨率，却仍然需要在 Windows
     * 125% / 150% DPI 下修复字体裁切，因此字体补丁必须先独立处理，然后才能根据 Display.Enable 决定
     * 是否继续安装后面的动态分辨率与 GUI Hook。
     *
     * 下面的顺序因此是故意设计成：
     *   1. 读取配置；
     *   2. 先根据 Font.FixDPI 决定是否修字体；
     *   3. 如果 Display.Enable=0，就在字体步骤结束后退出；
     *   4. 只有 Display.Enable=1 才继续计算目标宽高和安装宽屏/HUD/输入补丁。
     *
     * 这样两个开关才真正彼此独立，而且 Display.Enable=0 时也不会去写任何分辨率、JMM、HUD 或输入代码。
     */

    if (config.fix_font_dpi) {
        font_result = apply_font_dpi_fix(&text_region);
        if (font_result == 1) {
            log_line("[成功] 字体 DPI 路径已修正为 96 DPI");
        } else if (font_result == 2) {
            log_line("[成功] 字体 DPI 路径已经是 96 DPI 修正状态");
        } else {
            log_line("[失败] 字体 DPI 特征缺失或不唯一；已跳过字体修复");
        }
    } else {
        log_line("[信息] Font.FixDPI=0；INI 已关闭字体修复");
    }

    if (!config.enable) {
        log_line("[信息] Display.Enable=0；已关闭宽屏/HUD/输入补丁；Font.FixDPI 仍独立生效");
        flush_log_file();
        return;
    }

    log_uint("[信息] 基础高度(BaseHeight)=", config.base_height);
    log_uint("[信息] 比例宽度(AspectWidth)=", config.aspect_width);
    log_uint("[信息] 比例高度(AspectHeight)=", config.aspect_height);

    target_width = calculate_target_width(config.base_height, config.aspect_width, config.aspect_height);
    log_uint("[信息] 目标宽度(TargetWidth)=", target_width);

    /*
     * AspectRatio 当前的统一宽度规则要求最终 TargetWidth 做 8 像素对齐。
     * 这里把余数直接写进日志，用户不需要自己再拿计算器判断本轮是不是实际用了 8 像素边界。
     * 正常情况下这个值必须始终是 0；如果不是 0，就说明宽度计算逻辑出现了新的回归。
     */
    log_uint("[信息] AspectRatio目标宽度8像素对齐余数=", target_width & 7u);
    log_uint("[信息] 目标高度(TargetHeight)=", config.base_height);

    /*
     * BaseHeight 是内部逻辑高度，不是最终输出清晰度。高于 600 会扩大实际世界 surface / 可见范围，
     * 老游戏会明显增加绘制和对象处理成本。保留任意值是高级实验能力，但 4K 输出通常仍建议 480/600。
     */
    if (config.base_height > 600u) {
        log_line("[警告] BaseHeight>600 属于高级世界/FOV缩放，可能显著降低帧率");
        log_line("[信息] 4K 输出通常建议使用 BaseHeight=480 或 600，再交给 cnc-ddraw 放大");
    }

    /*
     * 世界高度现在可以任意调整，但游戏只存在两套已确认 GUI/JMM 基线。
     * 因此与 apply_jmm_layout_selection 使用同一个规则：<600 用 640 参考宽，>=600 用 800。
     * 这个值只决定固定尺寸主 HUD 的居中位置，不会把 BaseHeight 截断回 480/600。
     */
    g_target_width = target_width;
    g_target_height = config.base_height;
    g_native_base_width = (config.base_height >= 600u) ? 800u : 640u;
    g_center_main_hud = config.center_main_hud;
    g_auxiliary_ui_above_hud = config.auxiliary_ui_above_hud;

    hud_delta = ((LONG)g_target_width - (LONG)g_native_base_width) / 2;
    log_uint("[信息] 原生基准宽度(NativeBaseWidth)=", g_native_base_width);
    log_int("[信息] 主HUD水平居中偏移(MainHUDCenterDelta)=", hud_delta);

    if (target_width != 0) {
        resolution_result = apply_dynamic_resolution(&text_region, target_width, config.base_height);
    }

    if (resolution_result) {
        log_line("[成功] 动态分辨率代码点已解析；启动/前端代码保持原样");
        log_line("[信息] 主菜单与固定分辨率动画继续使用游戏原生 4:3 显示生命周期");
        log_line("[信息] 游戏内 TargetWidth/TargetHeight 仅由 Strategy 状态切换激活");
        log_line("[信息] 不需要隐藏的1024x768分支选择；游戏内会重映射所有已确认的显示模式分支");
    } else {
        log_line("[失败] 动态分辨率特征缺失或不唯一；已禁用运行时配置切换");
    }

    /*
     * test11 也只解析 JMM 选择器。前端阶段保持原版 640/800/1024 比较；主 HUD 出现后才切到
     * test10 已经实机通过的 TargetWidth -> JMMDL/JMMDL800 游戏内选择规则。
     */
    if (target_width != 0) {
        layout_result = apply_jmm_layout_selection(&text_region, target_width, config.base_height);
    }

    if (layout_result && resolution_result) {
        /* 明确把当前进程保持在 FRONTEND profile；理论上此时还是原字节，这一步也是一次完整自检。 */
        if (set_frontend_resolution_profile()) {
            log_line("[成功] 前端/动画分辨率配置已就绪：保留原版显示/JMM规则");
            if (config.base_height >= 600u) {
                log_line("[信息] Strategy 状态 3 开始时，游戏内配置会把 TargetWidth 映射到 JMMDL800.txt");
            } else {
                log_line("[信息] Strategy 状态 3 开始时，游戏内配置会把 TargetWidth 映射到 JMMDL.txt");
            }
        } else {
            resolution_result = FALSE;
            layout_result = FALSE;
            log_line("[失败] 前端配置自检失败；已禁用动态分辨率生命周期");
        }
    } else {
        log_line("[失败] JMM布局选择器特征缺失或不唯一；已禁用运行时布局配置");
    }

    /*
     * test15 继续沿用已经由 test14 实机证明时机正确的 Strategy 状态 3 enter/exit callsite 控制 profile。
     * 只有 resolution + JMM profile 都完整解析成功才安装，避免状态机触发一个半成品配置。
     */
    if (resolution_result && layout_result) {
        strategy_state_result = install_strategy_state_hooks(&text_region);
    }

    if (strategy_state_result) {
        log_line("[成功] Strategy状态生命周期Hook已安装：进入状态3=GAMEPLAY（强制重应用），退出状态3=FRONTEND（强制模式4）");
        log_line("[信息] 进入时：原版Strategy进入流程会被强制重应用一次；退出时：Strategy清理后强制应用原版显示模式4");
    } else if (resolution_result && layout_result) {
        log_line("[失败] Strategy状态切换特征/调用缺失或不唯一；已禁用游戏内分辨率切换");
    }

    /*
     * Steam 与非 Steam 的对照日志已经给出一个非常明确的差异：
     *   - 非 Steam：第一次 HUD 布局后，还会自然发生第二阶段 UI 尺寸应用；
     *   - Steam/ComeOn.dll：停在第一阶段，不再发生这轮布局，因此 GUI 最终位置不一致。
     *
     * test10 继续只“解析”0x4087A0 包装函数里的 UI manager / 0x4B35F0 上下文，绝不改写 0x4087B5 CALL。
     * test9 已实机证明“只广播 vtable+0x14”不会产生非 Steam 的第二阶段布局；test10 因此等 HUD、顶层链和
     * 资源根目录都成熟后，只在 Steam 环境补调用一次完整原版 0x4B35F0(mode=0,TargetWidth,TargetHeight)。
     * 非 Steam 路径完全不会执行这次额外 JMM 应用。
     */
    jmm_context_result = resolve_jmm_context(&text_region, (BYTE**)0);

    if (GAME_GetModuleHandleA && GAME_GetModuleHandleA("ComeOn.dll")) {
        HMODULE steam_module = GAME_GetModuleHandleA("ComeOn.dll");

        /*
         * v0.2.0 以 clean1 为稳定基线并正式封版：
         *   - test4 已实机通过的 ResJM.Lib 多语言兜底继续保留；
         *   - test3~test9 的 DirectShow/影片实验仍然全部不回归；
         *   - 全局 USER32!CreateWindowExA Hook、官方 EDIT WndProc 子类化、Steam/语言功能全部继续保留；
         *   - Steam/OpenGL 只保留已实机闭环的 MAKEINTATOM(lpClassName) 安全修复。
         */
        if (install_steam_resjm_language_shim(&text_region)) {
            log_line("[成功] Steam多语言 ResJM.Lib CreateFileA 兜底已安装");
        } else {
            log_line("[警告] Steam ResJM.Lib兜底特征尚未就绪或未匹配");
        }

        steam_class_atom_guard_installed = install_steam_createwindow_class_atom_guard(steam_module);
        if (steam_class_atom_guard_installed) {
            log_line("[成功] Steam ComeOn.dll CreateWindowExA 类Atom兼容修复已安装；官方EDIT处理保持完整");
            log_line("[信息] Steam OpenGL兼容修复仅绕过ComeOn.dll对NULL/MAKEINTATOM类名的不安全字符串解析");
        } else {
            log_line("[失败] Steam CreateWindowExA callback的Atom保护特征未匹配；ComeOn.dll callback保持原样");
        }

        g_steam_environment = FALSE;
        log_line("[信息] 已检测到外传Steam/ComeOn.dll；稳定的显示/HUD时序保持不变");
        log_line("[信息] Steam启动器控制的开场动画不属于DisplayFix职责；当前未启用任何历史影片实验");
    } else {
        g_steam_environment = FALSE;
        log_line("[信息] 已检测到外传非Steam环境");
        if (!jmm_context_result) {
            log_line("[信息] 可选JMM上下文未解析；非Steam核心宽屏路径仍可继续");
        }
    }

    /* 历史实验函数仍保留在源码供接档研究，但 test10 主线不安装这些 callsite hook / startup 广播实验。 */
    (void)&install_jmm_first_load_hook;
    (void)&broadcast_initial_ui_resolution;
    (void)&apply_current_ui_resolution_broadcast;

    /*
     * v0.3-test8 即使 GUI.CenterMainHUD=0 也解析同一个主 HUD 类：
     *   - +0x58 layout hook 在 CenterMainHUD=0 时只调用原版，不做平移；
     *   - +0x24 只做静态验证/解析，不再安装事件 hook；
     *   - 顶部 0x0B/0x0E 的真正行为兜底统一交给更上层的 global mouse-release hook。
     */
    hud_result = install_main_hud_center_hook(&text_region);

    if (hud_result) {
        if (config.center_main_hud) {
            log_line("[成功] 底部主HUD视觉居中Hook已安装（v0.2-test1稳定算法）");
        } else {
            log_line("[信息] GUI.CenterMainHUD=0；已关闭视觉居中，但HUD诊断仍会安装");
        }
        log_line("[成功] 主HUD Hook已安装：FRONTEND保持原布局；仅通过Strategy GAMEPLAY门控后在+0x58执行居中/同步");
        log_line("[成功] 已解析主HUD +0x24结构并确认顶部按钮ID 0x0B/0x0E；+0x24本身不做Hook");
        log_line("[信息] 边缘锚定的顶层UI按设计保持不变");
    } else {
        log_line("[失败] 主HUD根对象/事件/布局特征验证失败；已跳过HUD Hook");
    }

    /*
     * layer1d：继续保留 layer1a 已实机证明正确的“HUD 后延迟绘制”，但彻底删除 layer1b 的顶层链重排。
     * 输入侧只 Hook 原版 0x4B4790 -> 顶层 picker 的那一次 call：只有原版选中 HUD、鼠标同时落在已经延后绘制的菜单 root 时，
     * 才把 picker 返回值换成该菜单；随后 manager+0x40、+0x20/+0x24/+0x30、child hit-test 全部继续由原版处理。
     * 绘制侧会动态纳入当前菜单体系里与 HUD 相交的独立顶层辅助面板，用来覆盖用户实机看到的左侧装备面板。
     */
    /*
     * 只要主 HUD 居中功能开启，就安装同一套 UI manager / root-picker 基础 Hook。
     * 原因有两个：
     *   1. 0x0B/0x0E 的原版按压动画需要 picker root 校正，它属于 HUD 居中兼容，不应被 AuxiliaryUIAboveHUD 开关绑死；
     *   2. 统一 Runtime 的 UI_DRAW_BEGIN/END 事件也由这个稳定 manager Draw Hook 提供，未来手柄模块需要复用。
     *
     * 当 AuxiliaryUIAboveHUD=0 时，所有辅助菜单 Draw 延后逻辑仍由 g_auxiliary_ui_above_hud 在运行时完整关闭；
     * 也就是说安装基础 Hook 不等于强制开启辅助 GUI 图层。
     */
    if (hud_result && config.center_main_hud) {
        layer_result = install_auxiliary_ui_draw_layer_hook(&text_region);
    }

    if (layer_result) {
        log_line("[成功] HUD/UI共享输入与绘制基础Hook已安装；0x0B/0x0E按压动画使用原版root路径恢复");

        if (config.auxiliary_ui_above_hud) {
            log_line("[成功] 辅助GUI layer1d 已安装：保留HUD后延迟绘制，并同步单次顶层输入root优先级");
            log_line("[信息] layer1d 不再写 manager+0x18/+0x1C 或 object+0x08/+0x0C；layer1b 顶层链重排路线已撤销");
            log_line("[信息] 输入只在原版picker已经选中主HUD、且鼠标命中已延后绘制的活动菜单root时覆盖这一次返回值；manager+0x40仍由原版0x4B44D0更新");
            log_line("[信息] 当前菜单体系里与HUD相交的独立顶层辅助面板会动态安装Draw wrapper，并按原Draw链顺序在HUD后绘制");
            log_line("[信息] 独立辅助面板一旦在合法菜单上下文中确认，会做短生命周期跟踪；主窗口先关闭时，只要面板自身仍active且仍与HUD相交，就继续保持HUD上方");
            log_line("[信息] UI manager 的原版HUD特殊绘制pass保持原样，只执行一次；不Hook、不重放该pass");
            log_line("[信息] layer1d 不改X/Y、child、active、GetCursorPos、self+0xA8、键盘快捷键或按钮业务；test1~test7位移/坐标补偿路线未继承");
        } else {
            log_line("[信息] GUI.AuxiliaryUIAboveHUD=0；辅助GUI保持原版绘制层/菜单root优先级");
            log_line("[信息] 0x0B/0x0E HUD按压root校正仍保留，因为它属于CenterMainHUD输入反馈兼容，不属于辅助GUI图层开关");
        }
    } else if (!config.center_main_hud) {
        log_line("[信息] GUI.CenterMainHUD=0；主HUD未居中，因此不安装HUD/UI共享root与绘制Hook");
    } else if (hud_result) {
        log_line("[警告] HUD/UI共享root与绘制Hook结构验证失败；0x0B/0x0E按压动画校正和辅助GUI layer1d均保持原版行为");
    }


    /*
     * 顶部两个按钮的真正兜底必须安装在主 HUD hook 之后：
     * install_main_hud_center_hook() 会从已经验证的 +0x24 原版事件机器码解析出
     * 0x0B / 0x0E 对应的两个目标窗口全局指针槽；global release hook 需要复用这两个槽。
     */
    if (hud_result) {
        world_press_result = install_world_mouse_press_hook(&text_region);
        global_release_result = install_global_mouse_release_hook(&text_region);
    }

    if (world_press_result) {
        log_line("[成功] 世界鼠标按下保护已安装，仅用于防止0x0B/0x0E点击穿透");
        log_line("[信息] 原版0x4B44F0 UI按下分发保持完全不变（已移除test7回归）");
    } else {
        log_line("[失败] 世界鼠标按下调用点验证失败；已跳过防点击穿透保护");
    }

    if (global_release_result) {
        log_line("[成功] 全局鼠标释放保护已安装，在HUD路由前处理顶部按钮0x0B/0x0E");
        log_line("[信息] 鼠标释放：始终先执行原版UI分发；只有目标激活状态未变化时才执行兜底切换");
    } else {
        log_line("[失败] 全局鼠标释放调用点验证失败；已跳过顶部按钮路由前兜底");
    }

    /*
     * 初始化日志写盘。v0.3 封版仍不在 DLL/ASI 初始化阶段扩大主菜单/动画分辨率，也不主动重播 GUI/JMM；
     * 只有游戏自己的 Strategy 状态 3 进入 callsite 才切 GAMEPLAY profile，Steam 专用 JMM 同步还要继续
     * 等 HUD / 顶层 UI / 资源根成熟，并且必须确认当前已经是 GAMEPLAY profile。
     * 普通地图 WORLD press / GLOBAL release 的高频诊断
     * 已关闭，只有顶部按钮真正被拦截/兜底和 Steam one-shot 原版 JMM 应用才写运行时日志，减少性能干扰。
     */
    flush_log_file();
}

/* ============================================================================================== */
/* 13. 统一工程中的 Profile 后端入口                                                               */
/* ============================================================================================== */

/*
 * v0.1-dev1 起，本文件不再拥有 DllMain / InitializeASI。
 * 整个 BladeSwordQOL.asi 只能有一个 Windows 入口，否则把本体和外传链接进同一个 DLL 时会发生符号冲突，
 * 也会让两个后端都尝试初始化。真正的唯一入口在 src/Main.c，Runtime 先识别游戏 Profile，
 * 然后只调用与当前游戏匹配的这个函数。
 *
 * module 是 BladeSwordQOL.asi 自己的模块句柄。后端继续用它寻找同目录 BladeSwordQOL.ini / .log，
 * 因此把入口合并以后不会改变配置文件相对路径语义。
 */
int DisplayFixWaiZhuan_Initialize(void* module)
{
    if (!module) {
        return 0;
    }

    g_self_module = (HINSTANCE)module;
    initialize_display_fix();
    return 1;
}
