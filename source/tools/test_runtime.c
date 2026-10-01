/* 只在测试进程模拟 Windows API 失败，不接触游戏进程。
 * 包含桥的实现是为了替换它内部的 API 指针，从而测试真实写入代码的失败路径。 */
#include <stdio.h>
#include <string.h>
#include "../src/Runtime/Win32Bridge.c"
#include "../src/Runtime/X86Detour.h"

static unsigned char target[16];
static unsigned char gateway_memory[32];
static int protect_calls, fail_protect_call, flush_ok = 1, frees, checks;
static char log_output[512],log_path[260];
static unsigned long log_size,log_access,log_creation;
static int log_closes;
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
    protect_calls = fail_protect_call = frees = 0;
    flush_ok = 1;
}
/* 检查真实日志桥是否追加到同一文件；替身不碰磁盘。 */
static unsigned long __stdcall name_stub(unsigned long module,char *buffer,unsigned long capacity)
{
    const char *name="C:\\test\\BladeSwordQOL.asi";
    (void)module;
    if(strlen(name)+1>capacity)return 0;
    strcpy(buffer,name);return (unsigned long)strlen(name);
}
static unsigned long __stdcall create_stub(const char *path,unsigned long access,unsigned long share,
                                           void *security,unsigned long creation,unsigned long flags,unsigned long template_file)
{
    (void)share;(void)security;(void)flags;(void)template_file;
    strcpy(log_path,path);log_access=access;log_creation=creation;return 7ul;
}
static int __stdcall write_stub(unsigned long file,const void *text,unsigned long size,
                               unsigned long *written,void *overlapped)
{
    (void)file;(void)overlapped;
    if(log_size+size>=sizeof log_output)return 0;
    memcpy(log_output+log_size,text,size);log_size+=size;log_output[log_size]=0;*written=size;return 1;
}
static int __stdcall close_stub(unsigned long file)
{ (void)file;++log_closes;return 1; }
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

    /* 写入前失败：游戏字节保持原样，尚未使用的网关可以释放。 */
    reset_case(&detour); fail_protect_call = 1;
    CHECK(!X86Detour_Install(&detour, (unsigned long)target, (unsigned long)target + 100, 7));
    CHECK(!detour.installed && target[0] == 0x90 && frees == 1);

    /* 恢复保护失败：跳转已经存在，必须保留可用网关和安装记录。 */
    reset_case(&detour); fail_protect_call = 2;
    CHECK(X86Detour_Install(&detour, (unsigned long)target, (unsigned long)target + 100, 7));
    CHECK(detour.installed && target[0] == 0xE9 && frees == 0);
    fail_protect_call = 0;
    CHECK(X86Detour_Remove(&detour));
    CHECK(!detour.installed && target[0] == 0x90 && frees == 1);

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
    CHECK(detour.installed && frees == 0 && target[0] == 0x90);
    flush_ok = 1;
    CHECK(X86Detour_Remove(&detour));
    CHECK(!detour.installed && frees == 1);
    strcpy(log_output,"[DisplayFix] 已加载\r\n");log_size=(unsigned long)strlen(log_output);
    g_get_module_file_name_a=name_stub;g_create_file=create_stub;g_write_file=write_stub;g_close_handle=close_stub;
    RuntimeWin32_Log((void*)1,"[QoL] 初始化成功");
    CHECK(!strcmp(log_path,"C:\\test\\BladeSwordQOL.log"));
    CHECK(log_access==4ul && log_creation==4ul && log_closes==1);
    CHECK(strstr(log_output,"[DisplayFix] 已加载\r\n[QoL] 初始化成功\r\n")!=NULL);
    RuntimeWin32_LogNumber((void*)1,"[QoL] 数值=",4294967295ul);
    CHECK(strstr(log_output,"[QoL] 数值=4294967295\r\n")!=NULL && log_closes==2);
    RuntimeWin32_LogNumber((void*)1,"[QoL] 关闭=",0ul);
    CHECK(strstr(log_output,"[QoL] 关闭=0\r\n")!=NULL);
    printf("运行时写入失败、Hook 回滚与共用日志追加检查通过：%d 项\n", checks);
    return 0;
}
