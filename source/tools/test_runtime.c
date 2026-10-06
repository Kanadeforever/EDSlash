/* 只在测试进程模拟 Windows API 失败，不接触游戏进程。
 * 包含桥的实现是为了替换它内部的 API 指针，从而测试真实写入代码的失败路径。 */
#include <stdio.h>
#include <string.h>
#include "../src/Runtime/Win32Bridge.c"
#include "../src/Runtime/X86Detour.h"

static unsigned char target[16];
static unsigned char gateway_memory[32];
static int protect_calls, fail_protect_call, flush_ok = 1, frees, allocations, checks;
static char log_output[512];
static unsigned long log_size;

#define CHECK(test) do { ++checks; if (!(test)) { \
    printf("运行时回归失败：第 %d 行\n", __LINE__); return 1; } } while (0)

/* 每次先开放写保护、随后恢复。指定失败调用序号可分别模拟两个阶段。 */
static int __stdcall protect_stub(void* address, unsigned long size,
                                 unsigned long protect, unsigned long* old)
{
    (void)address; (void)size; (void)protect;
    *old = 0x20ul;
    return ++protect_calls != fail_protect_call;
}
static int __stdcall flush_stub(unsigned long process, const void* address, unsigned long size)
{
    (void)process; (void)address; (void)size;
    return flush_ok;
}
static unsigned long __stdcall process_stub(void) { return 1ul; }
static unsigned long __stdcall query_stub(const void* address, void* output, unsigned long size)
{
    MemoryBasicInformation32* info = (MemoryBasicInformation32*)output;
    /* 测试缓冲区都是静态有效内存，只为这次读提供足够大的连续区域。 */
    info->base_address = (unsigned long)address;
    info->region_size = 32ul;
    info->state = MEM_COMMIT;
    info->protect = 0x04ul;
    return size;
}
static void* __stdcall allocate_stub(void* address, unsigned long size,
                                    unsigned long type, unsigned long protect)
{
    (void)address; (void)size; (void)type; (void)protect;
    ++allocations;
    return gateway_memory;
}
static int __stdcall free_stub(void* memory, unsigned long size, unsigned long type)
{
    (void)memory; (void)size; (void)type;
    ++frees;
    return 1;
}
static void reset_case(X86Detour* detour)
{
    memset(detour, 0, sizeof(*detour));
    memset(target, 0x90, sizeof(target));
    memcpy(target, "\x83\xEC\x08\x8D\x54\x24\x00", 7);
    protect_calls = fail_protect_call = frees = allocations = 0;
    flush_ok = 1;
}
/* 模拟公共日志出口，桥只负责转发；实际宽字符文件写入另由配置／日志测试验证。 */
void RuntimeLog_Line(const char *text)
{
    size_t size=strlen(text);
    if (log_size+size+3>=sizeof log_output) return;
    memcpy(log_output+log_size,text,size);log_size+=(unsigned long)size;
    memcpy(log_output+log_size,"\r\n",3);log_size+=2;
}
int main(void)
{
    X86Detour detour;
    unsigned char own_patch[7];
    g_ready = 1;
    g_virtual_query = query_stub;
    g_virtual_protect = protect_stub;
    g_flush_instruction_cache = flush_stub;
    g_get_current_process = process_stub;
    g_virtual_alloc = allocate_stub;
    g_virtual_free = free_stub;

    /* 相对转移、未知前缀及截断均应在分配/写入之前拒绝。 */
    const unsigned char rejected[][7] = {
        {0xE8,0,0,0,0,0x90,0x90}, {0xE9,0,0,0,0,0x90,0x90},
        {0x75,0,0x90,0x90,0x90,0x90,0x90}, {0x0F,0x85,0,0,0,0,0x90},
        {0x90,0x90,0x90,0x90,0x90,0x90,0x90}
    };
    for (unsigned n=0;n<sizeof rejected/sizeof rejected[0];++n) {
        reset_case(&detour);memcpy(target,rejected[n],7);
        CHECK(!X86Detour_Install(&detour,(unsigned long)target,(unsigned long)target+100,7));
        CHECK(!detour.installed && !protect_calls && !allocations && !frees && !memcmp(target,rejected[n],7));
    }
    reset_case(&detour);
    CHECK(!X86Detour_Install(&detour,(unsigned long)target,(unsigned long)target+100,5));
    CHECK(!protect_calls && !allocations && !frees);
    const unsigned char accepted[][5]={{0x33,0xC0,0x8D,0x51,0x08},{0x56,0x8B,0x74,0x24,0x0C}};
    for (unsigned n=0;n<2;++n) {
        reset_case(&detour);memcpy(target,accepted[n],5);
        CHECK(X86Detour_Install(&detour,(unsigned long)target,(unsigned long)target+100,5));
        CHECK(!memcmp(gateway_memory,accepted[n],5));
        CHECK(X86Detour_Remove(&detour) && !memcmp(target,accepted[n],5));
    }

    /* 写入前失败：游戏字节保持原样，尚未使用的网关可以释放。 */
    reset_case(&detour); fail_protect_call = 1;
    CHECK(!X86Detour_Install(&detour, (unsigned long)target, (unsigned long)target + 100, 7));
    CHECK(!detour.installed && target[0] == 0x83 && frees == 1);

    /* 恢复保护失败：跳转已经存在，必须保留可用网关和安装记录。 */
    reset_case(&detour); fail_protect_call = 2;
    CHECK(X86Detour_Install(&detour, (unsigned long)target, (unsigned long)target + 100, 7));
    CHECK(detour.installed && target[0] == 0xE9 && frees == 0);
    fail_protect_call = 0;
    CHECK(X86Detour_Remove(&detour));
    CHECK(!detour.installed && target[0] == 0x83 && frees == 1);

    /* 缓存刷新失败也不能把已经写入的入口当成没有修改。 */
    reset_case(&detour); flush_ok = 0;
    CHECK(X86Detour_Install(&detour, (unsigned long)target, (unsigned long)target + 100, 7));
    CHECK(detour.installed && frees == 0);
    flush_ok = 1;
    CHECK(X86Detour_Remove(&detour));

    /* 撤销时保护调整失败：保留所有资源，下一次可以重试。 */
    reset_case(&detour);
    CHECK(X86Detour_Install(&detour, (unsigned long)target, (unsigned long)target + 100, 7));
    fail_protect_call = protect_calls + 1;
    CHECK(!X86Detour_Remove(&detour));
    CHECK(detour.installed && frees == 0 && target[0] == 0xE9);
    fail_protect_call = 0;
    CHECK(X86Detour_Remove(&detour));

    /* 其它人也写 E9 时仍能识别归属变化，不能只看第一个字节。 */
    reset_case(&detour);
    CHECK(X86Detour_Install(&detour, (unsigned long)target, (unsigned long)target + 100, 7));
    memcpy(own_patch, target, sizeof(own_patch));
    target[1] ^= 1;
    CHECK(!X86Detour_Remove(&detour));
    CHECK(detour.installed && frees == 0 && target[1] != own_patch[1]);
    memcpy(target, own_patch, sizeof(own_patch));
    CHECK(X86Detour_Remove(&detour));
    CHECK(X86Detour_Remove(&detour) && frees == 1);

    /* 撤销已写回原字节但刷新失败：网关保留，再次撤销可以完成清理。 */
    reset_case(&detour);
    CHECK(X86Detour_Install(&detour, (unsigned long)target, (unsigned long)target + 100, 7));
    flush_ok = 0;
    CHECK(!X86Detour_Remove(&detour));
    CHECK(detour.installed && frees == 0 && target[0] == 0x83);
    flush_ok = 1;
    CHECK(X86Detour_Remove(&detour));
    CHECK(!detour.installed && frees == 1);
    strcpy(log_output,"[DisplayFix] 已加载\r\n");log_size=(unsigned long)strlen(log_output);
    RuntimeWin32_Log((void*)1,"[QoL] 初始化成功");
    CHECK(strstr(log_output,"[DisplayFix] 已加载\r\n[QoL] 初始化成功\r\n")!=NULL);
    RuntimeWin32_LogNumber((void*)1,"[QoL] 数值=",4294967295ul);
    CHECK(strstr(log_output,"[QoL] 数值=4294967295\r\n")!=NULL);
    RuntimeWin32_LogNumber((void*)1,"[QoL] 关闭=",0ul);
    CHECK(strstr(log_output,"[QoL] 关闭=0\r\n")!=NULL);
    printf("运行时写入失败、Hook 回滚与共用日志追加检查通过：%d 项\n", checks);
    return 0;
}
