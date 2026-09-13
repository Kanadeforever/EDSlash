/*
 * DisplayFix.c
 *
 * 《刀剑封魔录》ComeOn.exe 显示修复 ASI 插件。
 * 当前版本：v0.3-test15
 *
 * ----------------------------------------------------------------------------------------------
 * v0.3-test15 的核心目标：保留 test14 已实机通过的“进入游戏切宽屏”，并恢复原游戏真正的标题 640x480 生命周期。
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
 *   - 离开 Strategy：先让原版 0x00407040 做完游戏内清理，再恢复 FRONTEND 代码 profile，最后复用原版
 *     0x00404D30(self, mode=4, force=1) 强制重建一次真正的前端 mode 4 surface。
 *
 * 这里的 mode 4 不是新猜的魔法数字。进一步反汇编已经确认原游戏启动前端自己就在 0x004053D8~0x004053E4
 * 明确执行 `push 0 / push 4 / call 0x00404D30`；而 0x00404D30 的 mode 4 原始分支就是 640x480。
 * 因此 test15 做的是“返回前端时补回原游戏本来就使用的显示模式语义”，不是另造一套菜单缩放规则。
 * ----------------------------------------------------------------------------------------------
 *
 * test13 的实机日志新增了一条决定性证据：
 *   [RUNTIME] Strategy enter state=3 GAMEPLAY profile=ready
 *   [RUNTIME] Strategy enter original apply finished live=640x480
 *
 * 这说明 test13 的 Strategy gate 本身已经命中正确时机，但 0x00407000 调 0x00404D30 时传入 force=0。
 * 原版 0x00404D30 会先比较 self+0x04 的“当前 mode ID”和这次请求的 mode ID；二者相同且 force=0 时，
 * 会在真正写入 self+0x228/self+0x22C 新宽高之前直接返回。DisplayFix 虽然已经把 mode 4 对应的立即数改成
 * 854x480，但 mode ID 仍然是 4，所以实际 live display 继续保持 640x480。随后 Steam delayed JMM 却按 854x480
 * 重排 GUI，便出现用户截图里的严重错位；1080 时 center delta 更大，所以几乎整套 HUD 都被移出可见区域。
 *
 * test14 不再增加新的 gameplay 判据。它继续使用 test13 的 Strategy state=3 gate，只在调用原版 0x00407000
 * 的极短时间内，把该函数开头的 `push 0` 临时改成 `push 1`，也就是把原版 0x00404D30 的 force 参数设为 1。
 * 原函数返回后立刻恢复 `push 0`。这样显示设备重建仍然完整走游戏自己的 0x00407000 -> 0x00404D30 路径，
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
 *   0x00404A00  状态切换函数
 *       self+0x0C = 当前高层状态
 *
 *   新状态 3：
 *       0x00404A83  打印 "BeforeStrategy() Begin"
 *       0x00404A97  call 0x00407000
 *       0x00404A9C  打印 "BeforeStrategy() End"
 *
 *   离开旧状态 3：
 *       0x00404A17  call 0x00408690
 *       0x00404A1E  call 0x00407040
 *       0x00404A23  打印 "AfterStrategy()  End"
 *
 * 0x00407000 不是我们猜出来的“可能会切分辨率”的函数：它自己读取显示管理器 self+0x280 的游戏设置模式，
 * 然后直接调用原版 0x00404D30 重新应用显示模式。因此 test13 只做两件事：
 *   1. 在 0x00404A97 调原版 0x00407000 之前，把代码立即数切成 GAMEPLAY profile；
 *   2. 在 0x00404A1E 调原版 0x00407040 之前，恢复 FRONTEND profile。
 *
 * 这样真正的 SetDisplayMode 仍然由游戏原来的 Strategy 进入流程自己执行，DisplayFix 不再额外插入一次
 * 0x00404D30 Reset，也不再依赖 HUD/world 对象生命周期。前端/主菜单完整保留原版 4:3 逻辑 surface。
 *
 * HUD 与 Steam GUI 路径也同步加一道防线：
 *   - FRONTEND profile 时，主 HUD +0x58 只执行原版布局，不做宽屏平移；
 *   - 只有 GAMEPLAY profile 已经由 Strategy 状态机正式启用后，才执行 v0.2-test1 已实机通过的 HUD 居中；
 *   - Steam/ComeOn.dll 的 test10 delayed full JMM apply 也只允许在 GAMEPLAY profile 中执行；
 *   - 每次重新进入 Strategy 状态 3，会重置 Steam one-shot 状态，保证“返回菜单再开新局”也能重新同步。
 *
 * Steam 特殊路径继续保留 v0.3-test10 已实机通过的修复：
 *   - Steam/ComeOn.dll 环境缺少非 Steam 自然发生的一次完整后续 JMM apply；
 *   - 等 HUD、顶层 UI、资源根都成熟后，one-shot 调用原版 0x004B35F0(0,W,H)；
 *   - test10 实机已确认 GUI 最终可恢复正确位置；test13 不改变这个原版 JMM 调用本身，只修正它的触发阶段。
 *
 * 输入修复继续保留 test8 已实机验证的方案：
 *   - 顶部“属性/道具”真实 control ID 是 0x0B / 0x0E；
 *   - 释放阶段在 0x00406086 -> 0x004B4560 之后，仅在原版没有切换窗口时补一次原版式 toggle；
 *   - 按下阶段绝不包装 0x004B44F0（test7 已证明它依赖调用者隐藏寄存器状态）；
 *   - 只在真正世界输入 0x004060EB -> 0x00473F10 的最后 callsite 上，命中 0x0B/0x0E 时跳过
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
 *   - INI 第一节 BOM 防护；
 *   - 机器码/上下文签名验证，不用整个 EXE SHA-256 锁死兼容版本。
 *
 * 重要测试状态：
 *   - test10：Steam delayed full JMM apply 稳定基线，已实机通过；
 *   - test11：HUD 存在误判 gameplay，实机失败；
 *   - test12：world 对象存在误判 gameplay，实机失败；
 *   - test13：Strategy gate 时机正确，但原版 0x407000 以 force=0 重应用相同 mode ID，实际 live 仍停在 640x480，实机失败；
 *   - test14：Strategy enter force=1 实机成功，进入游戏 live 已正确变成目标宽高；但退出只恢复代码 profile，
 *             没有把 live surface 强制重建回 640x480，因此返回标题后仍停留宽屏，实机失败；
 *   - test15：保留 test14 的进入路径；离开 Strategy 后强制恢复原版 mode 4 前端 surface，待实机验收。
 *
 * 代码里的注释故意写得非常细，目标是让只学过一天编程的人也能顺着看懂每一步。
 */

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
#define GAME_GetModuleHandleA   (*(FnGetModuleHandleA*)0x00528158u)
#define GAME_GetProcAddress     (*(FnGetProcAddress*)0x00528240u)
#define GAME_GetModuleFileNameA (*(FnGetModuleFileNameA*)0x00528238u)
#define GAME_OutputDebugStringA (*(FnOutputDebugStringA*)0x00528154u)
#define GAME_CreateFileA        (*(FnCreateFileA*)0x0052818Cu)
#define GAME_WriteFile          (*(FnWriteFile*)0x00528184u)
#define GAME_CloseHandle        (*(FnCloseHandle*)0x0052820Cu)
#define GAME_GetCurrentProcess  (*(FnGetCurrentProcess*)0x00528190u)
#define GAME_GetSystemMetrics   (*(FnGetSystemMetrics*)0x005283E4u)

/*
 * 0x005283C8 是 ComeOn.exe 自己的 GetCursorPos IAT 槽。
 * 这里必须故意走“游戏自己的 IAT”，而不是直接从 USER32 重新取函数地址。
 * 原因是 cnc-ddraw 之类的兼容层可能会接管这个 IAT，把桌面坐标转换成游戏逻辑坐标。
 * 主 HUD 原版命中检测 0x4B3380 也是走这个槽，所以 DisplayFix 的补偿检测必须和它看到同一套坐标。
 */
#define GAME_GetCursorPos       (*(FnGetCursorPos*)0x005283C8u)

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

/* 保存当前 DisplayFix.asi 自己的模块句柄，用来找到同目录的 DisplayFix.ini。 */
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
    0xFF,0x15,0x5C,0x80,0x52,0x00,
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
    0x75,0x42
};
static const char JMM_LAYOUT_SELECT_MASK[] = "xxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx";

/*
 * test13：0x00404A00 是游戏自己的高层状态切换函数。
 *
 * 这个长签名从函数头一路覆盖到“离开旧状态 3”的两次清理 call：
 *   0x00404A17 -> 0x00408690
 *   0x00404A1E -> 0x00407040
 * 后面的两个字符串地址分别落在 AfterStrategy 日志附近，是很强的语义锚点。
 *
 * E8 的 rel32 和 jump-table 绝对地址不作为固定版本地址使用：安装时会重新解码目标函数，
 * 并逐字验证 0x00407000 / 0x00407040 的函数头结构。这样历史宽屏 EXE 只要状态机结构未变就能兼容。
 */
static const BYTE STRATEGY_STATE_TRANSITION_PATTERN[] = {
    0x56,                         /* push esi */
    0x8B,0xF1,                   /* mov esi,ecx */
    0x8B,0x46,0x0C,              /* mov eax,[esi+0x0C]：旧状态 */
    0x83,0xC0,0xFE,
    0x83,0xF8,0x04,
    0x77,0x42,
    0xFF,0x24,0x85,0,0,0,0,     /* jmp [eax*4+jump_table]，表地址通配 */
    0x8B,0xCE,
    0xE8,0,0,0,0,               /* call 0x00408690，rel32 通配 */
    0x8B,0xCE,
    0xE8,0,0,0,0,               /* call 0x00407040：Strategy 离开清理 */
    0x68,0xF4,0x4D,0x54,0x00,   /* "AfterStrategy()  End" 附近字符串 */
    0x68,0xD0,0x5D,0x54,0x00,
    0xE8,0,0,0,0,
    0x83,0xC4,0x08,
    0xEB,0x19
};
static const char STRATEGY_STATE_TRANSITION_MASK[] =
    "xxxxxxxxxxxxxxxxx????xxx????xxx????xxxxxxxxxxx????xxxxx";

/*
 * 0x004060D6 一带是“UI 按下分派已经明确返回 0，接下来准备把同一次按下交给游戏世界”的路径。
 *
 * 原版机器码顺序已经闭合：
 *   0x004060CD  CALL 0x004B44F0    ; UI manager 按下分派
 *   0x004060D2  test eax,eax
 *   0x004060D4  jne  0x004060F0    ; UI 已处理时直接跳过世界输入
 *   0x004060D6  mov  ecx,[world]    ; 世界输入对象
 *   ...
 *   0x004060EB  CALL 0x00473F10    ; 真正的世界鼠标按下/角色移动路径
 *
 * v0.3-test7 曾在更早的 0x004060CD 包装 0x004B44F0。实机证明这会让普通地图左键和 Alt+F4 都失效。
 * 进一步反汇编发现 0x004B44F0 会把调用者保留的 ESI 直接压给下游 vtable+0x20，说明它存在
 * 非标准隐藏寄存器输入；用普通 C wrapper 再调用原函数会破坏这个上下文。
 *
 * test8 不再碰 0x004B44F0，而只改 0x004060EB 这一条世界调用：
 *   - 当前鼠标在主 HUD ID 0x0B / 0x0E 实时矩形内 -> 只跳过本次世界输入；
 *   - 其他任何位置 -> 原样调用 0x00473F10；
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
 *   1. 0x00405FB0 先通过 ComeOn.exe 自己的 GetCursorPos IAT 取得游戏逻辑鼠标坐标；
 *   2. 检测到鼠标从按下变成抬起时，把 event_type=1、mouse_x、mouse_y 压栈；
 *   3. ECX 放 UI manager（原版是 0x0055AF98）；
 *   4. 0x00406086 CALL 0x004B4560，把释放事件交给当前顶层 UI。
 *
 * v0.3-test4 的实机 A/B 日志已经确认，属性/道具顶部两个按钮真正对应直属 child ID 0x0B / 0x0E。
 * v0.3-test5 又证明：当 HUD 居中以后，鼠标释放有时根本到不了主 HUD 的 +0x24 事件函数，
 * 所以任何放在 +0x24 里面的兜底都“太晚了”。
 *
 * v0.3-test6 起把窗口 fallback 提升到这个全局释放分派点，test8 继续保留这条已实机成功路径：
 *   - 先根据主 HUD 当前真实 child 矩形判断鼠标是否落在 0x0B / 0x0E；
 *   - 永远先完整调用原版 0x004B4560；
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
 * 0x004087A0 是“把当前分辨率重新应用给 GUI/JMM 管理器”的小包装函数。
 * 它从分辨率对象 +0x228/+0x22C 读取 Width/Height，然后在 0x004087B5 调 0x004B35F0。
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

/* 0x004B35F0 已确认的函数头；用来验证 0x4087B5 真正 call 到我们理解的 JMM/UI 广播函数。 */
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
#define UI_OBJECT_CHILD_HEAD     0x9Cu
#define UI_OBJECT_CHILD_ITER     0xA0u
#define UI_OBJECT_HIT_CHILD      0xA8u
#define UI_OBJECT_CONTROL_ID     0x28u
#define UI_OBJECT_ACTIVE_A       0x64u
#define UI_OBJECT_ACTIVE_B       0x68u
#define UI_OBJECT_ACTIVE_LATCH   0xB8u

/*
 * 主 HUD vtable 中已经闭合的三个关键槽：
 *   +0x24 = 鼠标释放/点击事件分派 0x004C3E40；
 *   +0x30 = 原版通用子控件命中更新 0x004A1F90（本版不再 hook）；
 *   +0x58 = 通用布局 0x004B2B90。
 * v0.3-test8 只实际 hook +0x58 做视觉居中。
 * +0x24 仍会被静态验证和解析，用来取得 0x0B/0x0E 对应的原版目标窗口指针槽，
 * 但不再改写它：test5 已经实机证明，问题发生时鼠标释放可能根本到不了 +0x24，
 * 因此在这里继续安装行为补丁既太晚，也会增加不必要的变量。
 */
#define MAIN_HUD_DESTRUCTOR_SLOT 0x00u
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
 * 0x00405FB0 读取光标，0x00406086 在鼠标由按下变成抬起时经 0x004B4560 调这个虚函数；
 * 此时三个参数已经闭合为：event_type=1、mouse_x、mouse_y。
 *
 * v0.3-test8 不再实际 hook 这个 +0x24 函数。下面保留的桥接实现只作为历史诊断代码和
 * 静态研究依据，不会写进 vtable；真正的新兜底已经提升到 0x00406086 -> 0x004B4560 的全局释放层。
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
 * 分辨率 profile 生命周期已经完全交给 0x00404A00 的 Strategy 状态 enter/exit callsite，
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
 * UI manager 的全局鼠标释放函数 0x004B4560：
 *   ECX=this(UI manager)，栈参数依次是 event_type、mouse_x、mouse_y，返回非 0 表示事件已处理。
 * callsite 改到 fastcall 桥接后，unused_edx 只占住 EDX；三个真实参数的栈位置保持不变。
 */
typedef int (__thiscall *FnUiManagerMouseRelease)(LPVOID self, LONG event_type, LONG mouse_x, LONG mouse_y);
static FnUiManagerMouseRelease g_original_ui_manager_mouse_release = (FnUiManagerMouseRelease)0;

/*
 * 0x00473F10 是 UI manager 已经明确“没有处理本次按下”以后才会进入的世界输入函数。
 * 调用点使用 ECX=世界对象，栈上仍是 event_type / mouse_x / mouse_y，并由原函数 ret 0x0C 清栈。
 *
 * 与 0x004B44F0 不同，0x00473F10 的函数头会先保存 EBX/EBP/ESI/EDI，然后立即 mov esi,ecx；
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
 * 0x00407000 / 0x00407040 是 Strategy 状态进入/离开时由 0x00404A00 状态机直接调用的原版函数。
 * 二者都使用 thiscall：ECX 是同一个显示/高层状态管理对象，栈上没有参数。
 */
typedef void (__thiscall *FnStrategyStateStep)(LPVOID self);
static FnStrategyStateStep g_original_strategy_enter = (FnStrategyStateStep)0;
static FnStrategyStateStep g_original_strategy_exit = (FnStrategyStateStep)0;

/*
 * test14 起保存 0x00407000 开头 `push 0` 的“立即数字节”地址。
 * test15 继续原样使用这条进入游戏 force 补丁；本轮新增逻辑只发生在 Strategy exit 之后。
 * 原机器码是：
 *     56          push esi
 *     8B F1       mov  esi,ecx
 *     6A 00       push 0        ; 传给 0x00404D30 的 force 参数
 *
 * 进入 Strategy state=3 时，我们只把最后这个 00 临时改成 01。
 * 这样原版 0x00407000 自己仍然负责读取 mode、写 self+0x08、调用 0x00404D30 和后续 Strategy 初始化；
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
 * Steam 版的 ComeOnSteam.exe 会额外 LoadLibraryA("ComeOn.dll")。
 * 非 Steam 版没有这个模块，因此它可以作为“是否启用 Steam 专用兼容路径”的直接运行时条件。
 *
 * 这里故意不根据 EXE SHA-256 判断 Steam：
 *   - 项目总原则是按真实结构/内容签名兼容不同 EXE；
 *   - ComeOn.dll 是否真的已经装进当前进程，比“文件来自哪个发行渠道”更准确。
 */
static BOOL g_steam_environment = FALSE;

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
        str_append(line, line_size, "none");
        return;
    }

    control_id = *(DWORD*)((BYTE*)child + UI_OBJECT_CONTROL_ID);

    str_append(line, line_size, "ptr=");
    append_hex32(line, line_size, (DWORD)child);
    str_append(line, line_size, " id=");
    append_hex32(line, line_size, control_id);
    str_append(line, line_size, " active=");
    append_int(line, line_size, child_active_for_diagnostic(child) ? 1 : 0);

    if (read_child_rect(child, &rect)) {
        str_append(line, line_size, " rect=");
        append_int(line, line_size, rect.left);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.top);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.right);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.bottom);
    } else {
        str_append(line, line_size, " rect=invalid");
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
            str_append(line, (DWORD)sizeof(line), "[RUNTIME] HUD candidate ");
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
 * 这个 hook 是从 0x00404A97 的原版 callsite 进入的。到这里时，状态机已经先执行：
 *     self+0x0C = 3
 * 所以不需要靠 HUD、world、鼠标或资源对象去“猜”是不是 gameplay。
 *
 * 顺序非常重要：
 *   1. 先把 ComeOn.exe 的几处分辨率/JMM 立即数切成 GAMEPLAY profile；
 *   2. 再调用原版 0x00407000；
 *   3. 原版 0x00407000 自己会读取 self+0x280 的显示模式并调用 0x00404D30。
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

    if (g_strategy_enter_log_count < 4u) {
        ++g_strategy_enter_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[RUNTIME] Strategy enter state=");
        if (self) {
            append_int(line, (DWORD)sizeof(line), *(LONG*)((BYTE*)self + 0x0Cu));
        } else {
            append_int(line, (DWORD)sizeof(line), -1);
        }
        str_append(line, (DWORD)sizeof(line), profile_ok ? " GAMEPLAY profile=ready" : " GAMEPLAY profile=FAILED");
        append_runtime_line(line);
    }

    /*
     * test13 的关键失败点就在这里。
     *
     * 原版 0x00407000 会执行：
     *     push 0               ; force = 0
     *     mov eax,[self+0x280] ; 当前显示模式 ID，例如 4
     *     push eax
     *     mov [self+0x08],eax
     *     call 0x00404D30
     *
     * 0x00404D30 一开始会比较 self+0x04（当前 mode ID）和传入 mode ID。
     * 如果二者相同，并且第二个参数 force 也是 0，它就直接返回，不会走到我们已经改成 854x480 / 1068x600
     * 的宽高立即数。因此 test13 日志才会出现“GAMEPLAY profile=ready，但 live=640x480”。
     *
     * test14 不额外调用一次 0x00404D30，而是临时把原函数自己的 `push 0` 改成 `push 1`。
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
        append_runtime_line("[RUNTIME] Strategy enter force-reapply patch FAILED; fall back to FRONTEND profile");
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
     * 这样其他任何潜在的 0x00407000 调用都继续保持游戏原始语义；强制重应用只发生在我们的 Strategy hook 这一次。
     */
    if (force_patch_ok) {
        force_restore_ok = patch_bytes(g_strategy_enter_force_immediate, &g_strategy_enter_force_original, 1u);
        if (!force_restore_ok) {
            append_runtime_line("[RUNTIME] Strategy enter force-reapply restore FAILED; 0x407000 remains force=1");
        }
    }

    /*
     * test14 的第二道保险：不要只相信“补丁写成功”，还要看游戏显示管理器最终记录的 live 宽高。
     * 只有 live 真正等于 TargetWidth x TargetHeight，HUD 居中和 Steam delayed JMM 才有资格继续执行。
     */
    live_matches_target =
        (*(LONG*)((BYTE*)GAME_DISPLAY_MANAGER + DISPLAY_CURRENT_WIDTH_OFFSET) == (LONG)g_target_width) &&
        (*(LONG*)((BYTE*)GAME_DISPLAY_MANAGER + DISPLAY_CURRENT_HEIGHT_OFFSET) == (LONG)g_target_height);

    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[RUNTIME] Strategy enter original apply finished live=");
    append_int(line, (DWORD)sizeof(line), *(LONG*)((BYTE*)GAME_DISPLAY_MANAGER + DISPLAY_CURRENT_WIDTH_OFFSET));
    str_append(line, (DWORD)sizeof(line), "x");
    append_int(line, (DWORD)sizeof(line), *(LONG*)((BYTE*)GAME_DISPLAY_MANAGER + DISPLAY_CURRENT_HEIGHT_OFFSET));
    str_append(line, (DWORD)sizeof(line), " expected=");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_width);
    str_append(line, (DWORD)sizeof(line), "x");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_height);
    str_append(line, (DWORD)sizeof(line), force_patch_ok ? " force=1" : " force=0");
    append_runtime_line(line);

    if (profile_ok && !live_matches_target) {
        append_runtime_line("[RUNTIME] Strategy enter live-size mismatch; disable GAMEPLAY HUD/JMM and restore FRONTEND code profile");
        set_frontend_resolution_profile();
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
 *   第 2 步：设置 transition guard，然后完整调用原版 0x00407040。
 *           这一步先让游戏自己结束 Strategy 资源；我们绝不在旧 HUD/旧 world 仍处于清理中时重建显示设备。
 *   第 3 步：原版清理返回后，恢复 FRONTEND 分辨率/JMM 立即数。
 *   第 4 步：复用已经由内容签名解析并验证过的原版 0x00404D30，强制请求 mode 4。
 *           force=1 很重要：当前 self+0x04 很可能仍然也是 mode 4；如果 force=0，0x00404D30 会和 test13
 *           一样因为“mode ID 没变”直接早退，live surface 就还是宽屏。
 *   第 5 步：读取 self+0x228/self+0x22C，确认真的回到了当前 EXE 自己保存下来的 mode 4 原始宽高。
 *
 * 为什么使用 mode 4：
 *   - 原游戏启动路径 0x004053D8~0x004053E4 本身就明确 `push 0; push 4; call 0x00404D30`；
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
     * callsite 0x00404A1E 只有“旧状态是 Strategy/state 3”时才会执行。
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
     *   3. 0x00404D30 已经在初始化时通过函数头签名解析成功。
     *
     * 第二个参数固定 mode=4；第三个参数 force=1，专门绕开“当前 mode ID 同样是 4”的原版早退。
     */
    if (profile_ok && self && g_display_mode_apply) {
        /*
         * 原版启动前端在 0x004053DD 还会先写 self+0x08 = 4，然后才调用 0x00404D30。
         * 这个字段不是 live 宽高，而是对象内部保存的“本轮请求/准备使用的模式”。
         * test15 也照着原版顺序写回 4，避免 BaseHeight=600 等情况下刚离开的 gameplay 曾使用 mode 5，
         * 结果虽然 surface 已回 640x480，但对象内部的请求模式仍残留 5，影响后续前端状态或下一轮切换。
         */
        *(LONG*)((BYTE*)self + 0x08u) = 4;
        reset_result = g_display_mode_apply(self, 4, 1);
    }

    /*
     * 现在再读取真实 live 宽高。self 就是 0x00407000 进入路径使用的同一个显示/高层对象，
     * 其 +0x228/+0x22C 也是 0x00404D30 写入并在前几版日志中已经验证过的当前宽高字段。
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

    if (g_strategy_exit_log_count < 4u) {
        ++g_strategy_exit_log_count;
        line[0] = '\0';
        str_append(line, (DWORD)sizeof(line), "[RUNTIME] Strategy exit old_state=");
        append_int(line, (DWORD)sizeof(line), old_state);
        str_append(line, (DWORD)sizeof(line), profile_ok ? " FRONTEND profile=restored" : " FRONTEND profile=FAILED");
        str_append(line, (DWORD)sizeof(line), " reset_mode=4 result=");
        append_int(line, (DWORD)sizeof(line), (LONG)reset_result);
        str_append(line, (DWORD)sizeof(line), " live=");
        append_int(line, (DWORD)sizeof(line), live_width);
        str_append(line, (DWORD)sizeof(line), "x");
        append_int(line, (DWORD)sizeof(line), live_height);
        str_append(line, (DWORD)sizeof(line), " expected=");
        append_int(line, (DWORD)sizeof(line), expected_width);
        str_append(line, (DWORD)sizeof(line), "x");
        append_int(line, (DWORD)sizeof(line), expected_height);
        append_runtime_line(line);
    }

    if (!live_matches_frontend) {
        append_runtime_line("[RUNTIME] Strategy exit FRONTEND force-reset FAILED; title may remain on gameplay surface");
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
            append_runtime_line("[RUNTIME] FRONTEND HUD auto-layout: keep original 4:3 layout; no centering/JMM sync");
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
        str_append(line, (DWORD)sizeof(line), "[RUNTIME] HUD layout self=");
        append_hex32(line, (DWORD)sizeof(line), (DWORD)self);
        str_append(line, (DWORD)sizeof(line), " root=");
        append_int(line, (DWORD)sizeof(line), *(LONG*)((BYTE*)self + UI_OBJECT_X_OFFSET));
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), *(LONG*)((BYTE*)self + UI_OBJECT_Y_OFFSET));
        str_append(line, (DWORD)sizeof(line), " delta=");
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

    str_append(line, line_size, " contains=");

    if (!self) {
        str_append(line, line_size, "none");
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
            str_append(line, line_size, child_active_for_diagnostic(child) ? " active]" : " inactive]");
        }

        child = *(LPVOID*)((BYTE*)child + UI_OBJECT_NEXT_OFFSET);
    }

    if (!found) {
        str_append(line, line_size, "none");
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
        str_append(line, line_size, "none");
        return;
    }

    str_append(line, line_size, "ptr=");
    append_hex32(line, line_size, (DWORD)object);
    str_append(line, line_size, " active=");
    append_int(line, line_size, ui_object_active_for_diagnostic(object) ? 1 : 0);

    if (read_child_rect(object, &rect)) {
        str_append(line, line_size, " rect=");
        append_int(line, line_size, rect.left);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.top);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.right);
        str_append(line, line_size, ",");
        append_int(line, line_size, rect.bottom);
    } else {
        str_append(line, line_size, " rect=invalid");
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
 * 这和 test7 最大的区别是：这里已经处在 0x004B44F0 返回 0 之后。也就是说游戏自己的 UI 按下分派
 * 已经完整执行过，DisplayFix 不需要、也绝不能再包装那条存在隐藏 ESI 语义的路径。
 *
 * 此桥接只做三件事：
 *   1. 用 callsite 原本传给世界函数的 mouse_x / mouse_y 检查当前主 HUD 的 0x0B / 0x0E 实时矩形；
 *   2. 命中两个特殊按钮时直接返回，相当于“只跳过这一次 0x00473F10”；
 *   3. 其他位置完整调用原版 0x00473F10。
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
        str_append(line, (DWORD)sizeof(line), "[RUNTIME] WORLD press arg=");
        append_int(line, (DWORD)sizeof(line), mouse_x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), mouse_y);
        str_append(line, (DWORD)sizeof(line), " top_id=");
        append_hex32(line, (DWORD)sizeof(line), hit_id);
        str_append(line, (DWORD)sizeof(line), " child=");
        append_child_brief(line, (DWORD)sizeof(line), hit_child);
        str_append(line, (DWORD)sizeof(line), " block_world=");
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
 *   - 0x00406086 的 call 一定发生在鼠标释放检测成立之后；
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
        str_append(line, (DWORD)sizeof(line), "[RUNTIME] GLOBAL release arg=");
        append_int(line, (DWORD)sizeof(line), mouse_x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), mouse_y);
        str_append(line, (DWORD)sizeof(line), " test=");
        append_int(line, (DWORD)sizeof(line), test_x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), test_y);
        str_append(line, (DWORD)sizeof(line), have_cursor ? " cursor=ok" : " cursor=fallback-arg");
        str_append(line, (DWORD)sizeof(line), " top_id=");
        append_hex32(line, (DWORD)sizeof(line), hit_id);
        str_append(line, (DWORD)sizeof(line), " child=");
        append_child_brief(line, (DWORD)sizeof(line), hit_child);
        str_append(line, (DWORD)sizeof(line), " target_before=");
        append_ui_target_brief(line, (DWORD)sizeof(line), target_before);
        str_append(line, (DWORD)sizeof(line), " target_after=");
        append_ui_target_brief(line, (DWORD)sizeof(line), target_after);
        str_append(line, (DWORD)sizeof(line), " original_result=");
        append_int(line, (DWORD)sizeof(line), original_result);
        str_append(line, (DWORD)sizeof(line), " fallback=");
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
 * 安装 0x004060EB -> 0x00473F10 的世界鼠标按下 callsite hook。
 *
 * 这里故意不再触碰 0x004060CD -> 0x004B44F0。test7 的实机回归已经证明后者不能被普通 C wrapper
 * 安全包裹；它的 ESI 隐式输入细节已写入“逆向工程知识库.md”。
 *
 * 安装步骤：
 *   1. 用 WORLD_MOUSE_PRESS_CALLSITE_PATTERN 唯一定位 0x004060D6 一带；
 *   2. E8 CALL 位于签名 +21；
 *   3. 解码原目标并验证其函数头确实是 0x00473F10 的 SEH/寄存器保存结构；
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
     * 0x00473F10 函数头：
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
 * 为什么改 callsite 而不是直接改 0x00404A00 整个状态函数：
 *   - 0x00404A97 只会在“新状态 == 3”分支执行，语义就是 BeforeStrategy；
 *   - 0x00404A1E 只会在“旧状态 == 3”清理分支执行，语义就是 AfterStrategy；
 *   - 两个 callsite 都把同一个 self 放在 ECX，桥接非常简单；
 *   - 只替换 E8 rel32，不改 jump table、不改状态值，也不复制游戏自己的状态机逻辑。
 *
 * 安装前还会验证两个原目标函数：
 *   0x00407000 必须读取 self+0x280、写 self+0x08，然后 call 原版显示模式函数；
 *   0x00407040 必须是 mov ecx,<global> / jmp <cleanup> 的小尾调用包装。
 * 任意一步不匹配就整组拒绝安装，不留下“只装一半”的生命周期补丁。
 */
static BOOL install_strategy_state_hooks(const TextRegion* region)
{
    BYTE* state_function;
    BYTE* exit_call;
    BYTE* enter_call;
    BYTE* enter_target;
    BYTE* exit_target;

    state_function = find_unique_pattern(region,
                                         STRATEGY_STATE_TRANSITION_PATTERN,
                                         STRATEGY_STATE_TRANSITION_MASK,
                                         (DWORD)sizeof(STRATEGY_STATE_TRANSITION_PATTERN));
    if (!state_function) {
        return FALSE;
    }

    /* 0x404A00 -> 0x404A1E，所以离开 Strategy 的 call 在函数头 +0x1E。 */
    exit_call = state_function + 0x1Eu;

    /* 0x404A00 -> 0x404A97，所以进入 Strategy 的 call 在函数头 +0x97。 */
    enter_call = state_function + 0x97u;

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
     * 进入函数 0x407000 的已确认开头：
     *   push esi
     *   mov  esi,ecx
     *   push 0
     *   mov  eax,[esi+0x280]
     *   push eax
     *   mov  [esi+0x08],eax
     *   call 0x404D30
     *
     * call 的 rel32 不锁死，只检查稳定 opcode 和字段偏移。
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

    /* 离开函数 0x407040：mov ecx,<absolute global> / jmp rel32。两个地址都不写死。 */
    if (exit_target[0] != 0xB9 || exit_target[5] != 0xE9) {
        return FALSE;
    }

    /*
     * test14 依赖 0x407000+3 的 `6A 00`。上面的函数头验证已经确认 enter_target[3]==0x6A、
     * enter_target[4]==0x00；这里把“00 这个立即数字节”的地址保存下来，进入 Strategy 时临时改成 01。
     */
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
        /* 第二条失败时立即把第一条 call 恢复到原目标，绝不留下半安装状态。 */
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
 * 安装 0x00406086 的全局鼠标释放 callsite hook。
 *
 * 不写死 0x00406086 / 0x004B4560：
 *   - 先用上面的长签名唯一定位 callsite；
 *   - 再解码 E8 rel32 得到真实目标；
 *   - 验证目标函数头和 0x4B4560 已确认结构一致；
 *   - 最后只替换 E8 后面的 rel32。
 *
 * 这个签名已经离线在原版、480P/540P/720P/768P/900P/1080P 改版和 Steam EXE 上全部唯一命中。
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
        str_append(line, (DWORD)sizeof(line), "[RUNTIME] HUD release arg=");
        append_int(line, (DWORD)sizeof(line), mouse_x);
        str_append(line, (DWORD)sizeof(line), ",");
        append_int(line, (DWORD)sizeof(line), mouse_y);

        if (have_point) {
            str_append(line, (DWORD)sizeof(line), " cursor=");
            append_int(line, (DWORD)sizeof(line), point.x);
            str_append(line, (DWORD)sizeof(line), ",");
            append_int(line, (DWORD)sizeof(line), point.y);
        } else {
            str_append(line, (DWORD)sizeof(line), " cursor=unavailable");
        }

        str_append(line, (DWORD)sizeof(line), " hit=");
        append_child_brief(line, (DWORD)sizeof(line), hit_child);

        if (have_point) {
            append_candidates_at_point(line, (DWORD)sizeof(line), self, point.x, point.y);
        }

        str_append(line, (DWORD)sizeof(line), " target0B_before=");
        append_ui_target_brief(line, (DWORD)sizeof(line), target_0b_before);
        str_append(line, (DWORD)sizeof(line), " target0B_after=");
        if (g_top_button_0b_target_slot) {
            append_ui_target_brief(line, (DWORD)sizeof(line), *g_top_button_0b_target_slot);
        } else {
            str_append(line, (DWORD)sizeof(line), "slot-unavailable");
        }

        str_append(line, (DWORD)sizeof(line), " target0E_before=");
        append_ui_target_brief(line, (DWORD)sizeof(line), target_0e_before);
        str_append(line, (DWORD)sizeof(line), " target0E_after=");
        if (g_top_button_0e_target_slot) {
            append_ui_target_brief(line, (DWORD)sizeof(line), *g_top_button_0e_target_slot);
        } else {
            str_append(line, (DWORD)sizeof(line), "slot-unavailable");
        }

        str_append(line, (DWORD)sizeof(line), " fallback=");
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
 * 根据 BaseHeight 和宽高比得到逻辑宽度。
 *
 * 例：480 × 16 / 9 = 853.333...
 * 先四舍五入得到 853，再把奇数向上对齐成偶数 854。
 * 老 DirectDraw 对偶数宽度更友好，而 854x480 也正好是附件宽屏改版已经实机跑过的组合。
 */
static DWORD calculate_target_width(DWORD base_height, DWORD aspect_width, DWORD aspect_height)
{
    DWORD product;
    DWORD width;
    DWORD remainder;
    DWORD round_threshold;

    if (base_height == 0 || aspect_width == 0 || aspect_height == 0) {
        return 0;
    }

    /*
     * BaseHeight 已经解除人为上限，但这个 ASI 故意不链接 C 运行库。
     * 在 32 位 x86 上直接使用 64 位除法会让编译器引入 __aulldiv 之类的 CRT helper，
     * 从而破坏“零 CRT / 零额外依赖”的项目目标。
     *
     * 所以这里用最直接的乘法溢出检查：
     *   base_height <= 0x7FFFFFFE / aspect_width
     * 才执行 32 位乘法。
     *
     * 这个边界已经远远超过老 DirectDraw/本游戏现实可创建的任何分辨率，
     * 因此它不是用户可感知的“BaseHeight 上限”，只是整数安全边界。
     */
    if (base_height > (0x7FFFFFFEu / aspect_width)) {
        return 0;
    }

    product = base_height * aspect_width;
    width = product / aspect_height;
    remainder = product % aspect_height;

    /*
     * 不写 product + aspect_height/2，是为了连这一步也避免无符号加法溢出。
     * remainder >= ceil(aspect_height/2) 与“四舍五入时余数至少一半”完全等价。
     */
    round_threshold = (aspect_height / 2u) + (aspect_height & 1u);
    if (remainder >= round_threshold) {
        ++width;
    }

    /* 老 DirectDraw 对偶数宽度更友好，奇数仍然向上补成偶数。 */
    if ((width & 1u) != 0u) {
        ++width;
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
 * 0x004B35F0 的真实 thiscall 类型：ECX=UI 管理器，栈上依次是 mode、width、height，ret 0x0C。
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
 * 通过 0x004087B5 改过来的桥接函数。
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
        str_append(line, (DWORD)sizeof(line), "[RUNTIME] JMM apply mode=");
        append_int(line, (DWORD)sizeof(line), mode);
        str_append(line, (DWORD)sizeof(line), " input=");
        append_int(line, (DWORD)sizeof(line), width);
        str_append(line, (DWORD)sizeof(line), "x");
        append_int(line, (DWORD)sizeof(line), height);
        str_append(line, (DWORD)sizeof(line), " final=");
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
        append_runtime_line("[RUNTIME] startup UI broadcast skipped: prerequisites unavailable");
        return;
    }

    node = *(LPVOID*)((BYTE*)g_jmm_manager + 0x1Cu);

    line[0] = '\0';
    str_append(line, (DWORD)sizeof(line), "[RUNTIME] startup UI broadcast begin manager=");
    append_hex32(line, (DWORD)sizeof(line), (DWORD)g_jmm_manager);
    str_append(line, (DWORD)sizeof(line), " target=");
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
    str_append(line, (DWORD)sizeof(line), "[RUNTIME] startup UI broadcast end visited=");
    append_int(line, (DWORD)sizeof(line), (LONG)visited);
    str_append(line, (DWORD)sizeof(line), " applied=");
    append_int(line, (DWORD)sizeof(line), (LONG)applied);
    if (node) {
        str_append(line, (DWORD)sizeof(line), " guard=hit");
    } else {
        str_append(line, (DWORD)sizeof(line), " guard=ok");
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
 * 重新反汇编 0x004B35F0 后确认完整流程是：
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
            append_runtime_line("[RUNTIME] Steam environment detected late: ComeOn.dll is now loaded");
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
            append_runtime_line("[RUNTIME] Steam delayed JMM apply waiting: game resource root is not ready yet");
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
    str_append(line, (DWORD)sizeof(line), "[RUNTIME] Steam delayed JMM apply begin attempt=");
    append_int(line, (DWORD)sizeof(line), (LONG)g_steam_ui_sync_attempts);
    str_append(line, (DWORD)sizeof(line), " manager=");
    append_hex32(line, (DWORD)sizeof(line), (DWORD)g_jmm_manager);
    str_append(line, (DWORD)sizeof(line), " head=");
    append_hex32(line, (DWORD)sizeof(line), (DWORD)list_head);
    str_append(line, (DWORD)sizeof(line), " target=");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_width);
    str_append(line, (DWORD)sizeof(line), "x");
    append_int(line, (DWORD)sizeof(line), (LONG)g_target_height);
    str_append(line, (DWORD)sizeof(line), " resource_root=ready id0B_before=");
    append_child_brief(line, (DWORD)sizeof(line), child_0b);
    str_append(line, (DWORD)sizeof(line), " id0E_before=");
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
    str_append(line, (DWORD)sizeof(line), "[RUNTIME] Steam delayed JMM apply end result=");
    append_int(line, (DWORD)sizeof(line), result);
    str_append(line, (DWORD)sizeof(line), g_steam_ui_sync_done ? " done=1" : " done=0");
    str_append(line, (DWORD)sizeof(line), changed ? " child_layout_changed=1" : " child_layout_changed=0");
    str_append(line, (DWORD)sizeof(line), " id0B_after=");
    append_child_brief(line, (DWORD)sizeof(line), child_0b);
    str_append(line, (DWORD)sizeof(line), " id0E_after=");
    append_child_brief(line, (DWORD)sizeof(line), child_0e);
    append_runtime_line(line);
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
    BYTE* event_slot;
    BYTE* layout_slot;
    DWORD destructor_slot_value;
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
    event_slot = (BYTE*)(vtable_address + MAIN_HUD_EVENT_SLOT);
    layout_slot = (BYTE*)(vtable_address + MAIN_HUD_LAYOUT_SLOT);
    destructor_slot_value = read_u32(destructor_slot);
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
        event_code[0x1C0] != 0x8B || event_code[0x1C1] != 0x0D) {
        return FALSE;
    }

    g_top_button_0e_target_slot = (LPVOID*)read_u32(event_code + 0x11Au);
    g_top_button_0b_target_slot = (LPVOID*)read_u32(event_code + 0x1C2u);

    if ((DWORD)g_top_button_0e_target_slot < 0x00400000u ||
        (DWORD)g_top_button_0e_target_slot >= 0x00600000u ||
        (DWORD)g_top_button_0b_target_slot < 0x00400000u ||
        (DWORD)g_top_button_0b_target_slot >= 0x00600000u) {
        g_top_button_0e_target_slot = (LPVOID*)0;
        g_top_button_0b_target_slot = (LPVOID*)0;
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

    if (!patch_u32(destructor_slot, (DWORD)&main_hud_destructor_hook)) {
        g_original_main_hud_destructor = (FnMainHudDestructor)0;
        g_original_main_hud_event = (FnMainHudEvent)0;
        g_original_main_hud_layout = (FnUILayout)0;
        g_top_button_0b_target_slot = (LPVOID*)0;
        g_top_button_0e_target_slot = (LPVOID*)0;
        return FALSE;
    }

    if (!patch_u32(layout_slot, (DWORD)&main_hud_layout_hook)) {
        /* layout 安装失败时把 destructor vtable 槽恢复原值，避免只装一半生命周期 hook。 */
        patch_u32(destructor_slot, destructor_slot_value);
        g_original_main_hud_destructor = (FnMainHudDestructor)0;
        g_original_main_hud_event = (FnMainHudEvent)0;
        g_original_main_hud_layout = (FnUILayout)0;
        g_top_button_0b_target_slot = (LPVOID*)0;
        g_top_button_0e_target_slot = (LPVOID*)0;
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
    BOOL global_release_result = FALSE;
    BOOL world_press_result = FALSE;
    BOOL strategy_state_result = FALSE;
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
    make_sibling_path(module_path, "DisplayFix.ini", g_ini_path, (DWORD)sizeof(g_ini_path));
    make_sibling_path(module_path, "DisplayFix.log", g_log_path, (DWORD)sizeof(g_log_path));

    log_line("DisplayFix v0.3-test15");
    log_line("Architecture: Win32/x86 ASI, content-signature runtime patch");

    if (!resolve_required_apis()) {
        log_line("[FAIL] cannot resolve VirtualProtect");
        flush_log_file();
        return;
    }

    if (!get_main_text_region(&text_region)) {
        log_line("[FAIL] cannot locate main EXE .text section");
        flush_log_file();
        return;
    }

    /*
     * 先把实际 INI 绝对路径写进日志。
     * DisplayFix 始终读取“与当前 ASI 同目录”的 DisplayFix.ini；
     * 如果日志显示 BaseHeight 回退成 480，用户可以直接核对自己编辑的是否就是这里这份文件。
     */
    log_text("[INFO] ConfigPath=", g_ini_path);

    load_config(&config);

    if (!config.enable) {
        log_line("[INFO] Display.Enable=0, no runtime patch applied");
        flush_log_file();
        return;
    }

    log_uint("[INFO] BaseHeight=", config.base_height);
    log_uint("[INFO] AspectWidth=", config.aspect_width);
    log_uint("[INFO] AspectHeight=", config.aspect_height);

    target_width = calculate_target_width(config.base_height, config.aspect_width, config.aspect_height);
    log_uint("[INFO] TargetWidth=", target_width);
    log_uint("[INFO] TargetHeight=", config.base_height);

    /*
     * BaseHeight 是内部逻辑高度，不是最终输出清晰度。高于 600 会扩大实际世界 surface / 可见范围，
     * 老游戏会明显增加绘制和对象处理成本。保留任意值是高级实验能力，但 4K 输出通常仍建议 480/600。
     */
    if (config.base_height > 600u) {
        log_line("[WARN] BaseHeight>600 is advanced world/FOV scaling and can heavily reduce FPS");
        log_line("[INFO] For 4K output, normally use BaseHeight=480 or 600 and let cnc-ddraw upscale");
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

    hud_delta = ((LONG)g_target_width - (LONG)g_native_base_width) / 2;
    log_uint("[INFO] NativeBaseWidth=", g_native_base_width);
    log_int("[INFO] MainHUDCenterDelta=", hud_delta);

    if (config.fix_font_dpi) {
        font_result = apply_font_dpi_fix(&text_region);
        if (font_result == 1) {
            log_line("[OK] font DPI path patched to 96 DPI");
        } else if (font_result == 2) {
            log_line("[OK] font DPI path was already patched");
        } else {
            log_line("[FAIL] font DPI signature is missing/ambiguous; font patch skipped");
        }
    } else {
        log_line("[INFO] Font.FixDPI=0, font patch disabled by INI");
    }

    if (target_width != 0) {
        resolution_result = apply_dynamic_resolution(&text_region, target_width, config.base_height);
    }

    if (resolution_result) {
        log_line("[OK] dynamic-resolution code points resolved; startup/frontend code is left original");
        log_line("[INFO] main menu and fixed-resolution animations keep the game's native 4:3 display lifecycle");
        log_line("[INFO] gameplay TargetWidth/TargetHeight will be activated only by the game's Strategy state transition");
        log_line("[INFO] No 1024x768 menu option is required; the game menu only exposes up to 800x600");
    } else {
        log_line("[FAIL] dynamic-resolution signatures are missing/ambiguous; runtime profile switching disabled");
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
            log_line("[OK] frontend/animation resolution profile armed: original display/JMM rules preserved");
            if (config.base_height >= 600u) {
                log_line("[INFO] gameplay profile will map TargetWidth to JMMDL800.txt when Strategy state 3 begins");
            } else {
                log_line("[INFO] gameplay profile will map TargetWidth to JMMDL.txt when Strategy state 3 begins");
            }
        } else {
            resolution_result = FALSE;
            layout_result = FALSE;
            log_line("[FAIL] frontend profile self-check failed; dynamic resolution lifecycle disabled");
        }
    } else {
        log_line("[FAIL] JMM layout-selector signature is missing/ambiguous; runtime layout profile disabled");
    }

    /*
     * test15 继续沿用已经由 test14 实机证明时机正确的 Strategy 状态 3 enter/exit callsite 控制 profile。
     * 只有 resolution + JMM profile 都完整解析成功才安装，避免状态机触发一个半成品配置。
     */
    if (resolution_result && layout_result) {
        strategy_state_result = install_strategy_state_hooks(&text_region);
    }

    if (strategy_state_result) {
        log_line("[OK] Strategy state lifecycle hooks installed: state 3 enter=GAMEPLAY(force reapply), state 3 exit=FRONTEND(force mode 4)");
        log_line("[INFO] enter: original 0x407000 is forced once; exit: original display mode 4 is forced once after Strategy cleanup");
    } else if (resolution_result && layout_result) {
        log_line("[FAIL] Strategy state transition signature/calls are missing or ambiguous; gameplay resolution switching disabled");
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
        g_steam_environment = TRUE;
        log_line("[INFO] Steam/ComeOn.dll environment detected");
        if (jmm_context_result) {
            log_line("[OK] Steam delayed JMM apply armed; waits for mature HUD + top-level UI + resource root");
            log_line("[INFO] Steam fix reuses original 0x4B35F0 once; non-Steam path remains untouched");
        } else {
            log_line("[FAIL] Steam environment detected but UI/JMM context signature is missing/ambiguous; Steam layout sync disabled");
        }
    } else {
        log_line("[INFO] non-Steam environment detected; Steam delayed UI sync remains dormant");
        if (!jmm_context_result) {
            log_line("[INFO] optional Steam UI/JMM context was not resolved; non-Steam path is unaffected");
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
            log_line("[OK] bottom main HUD visual centering hook installed (v0.2-test1 stable algorithm)");
        } else {
            log_line("[INFO] GUI.CenterMainHUD=0; visual centering disabled, pure HUD diagnostics still installed");
        }
        log_line("[OK] main HUD hooks installed: FRONTEND keeps original layout; +0x58 centers/syncs only after Strategy GAMEPLAY gate");
        log_line("[OK] main HUD +0x24 structure parsed for confirmed top-button IDs 0x0B/0x0E; +0x24 itself is not hooked");
        log_line("[INFO] edge-anchored top-level UI is intentionally left untouched");
    } else {
        log_line("[FAIL] main HUD root/event/layout signature validation failed; HUD hook skipped");
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
        log_line("[OK] world mouse-press guard installed for 0x0B/0x0E click-through protection only");
        log_line("[INFO] original 0x4B44F0 UI press dispatch is left completely untouched (test7 regression removed)");
    } else {
        log_line("[FAIL] world mouse-press callsite validation failed; click-through protection skipped");
    }

    if (global_release_result) {
        log_line("[OK] global mouse-release guard installed before HUD routing for top buttons 0x0B/0x0E");
        log_line("[INFO] release: original UI dispatch always runs first; fallback toggles only when target active state did not change");
    } else {
        log_line("[FAIL] global mouse-release callsite validation failed; top-button pre-routing fallback skipped");
    }

    /*
     * 初始化日志写盘。test15 仍不在 DLL/ASI 初始化阶段扩大主菜单/动画分辨率，也不主动重播 GUI/JMM；
     * 只有游戏自己的 Strategy 状态 3 进入 callsite 才切 GAMEPLAY profile，Steam 专用 JMM 同步还要继续
     * 等 HUD / 顶层 UI / 资源根成熟，并且必须确认当前已经是 GAMEPLAY profile。
     * 普通地图 WORLD press / GLOBAL release 的高频诊断
     * 已关闭，只有顶部按钮真正被拦截/兜底和 Steam one-shot 原版 JMM 应用才写运行时日志，减少性能干扰。
     */
    flush_log_file();
}

/* ============================================================================================== */
/* 13. DLL / ASI 入口                                                                                   */
/* ============================================================================================== */

/*
 * 某些 ASI Loader 会主动查找并调用 InitializeASI。
 * 即使当前加载器只依赖 DllMain，保留这个导出也能提高兼容性。
 */
__declspec(dllexport) void InitializeASI(void)
{
    initialize_display_fix();
}

/*
 * Windows 每次把 .asi 当 DLL LoadLibrary 时都会调用 DllMain。
 * reason==DLL_PROCESS_ATTACH 时说明插件刚刚进入游戏进程，此时开始初始化。
 */
BOOL __stdcall DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    /* reserved 在这个插件里不用；显式读一下可以避免某些编译器警告。 */
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        g_self_module = module;
        initialize_display_fix();
    }

    return TRUE;
}
