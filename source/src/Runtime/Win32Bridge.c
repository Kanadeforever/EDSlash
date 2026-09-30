#include "Win32Bridge.h"

#define GAME_IMAGE_BASE 0x00400000ul

#define MEM_COMMIT 0x00001000ul
#define MEM_RELEASE 0x00008000ul
#define MEM_RESERVE 0x00002000ul
#define PAGE_NOACCESS 0x00000001ul
#define PAGE_GUARD 0x00000100ul
#define PAGE_EXECUTE_READWRITE 0x00000040ul

typedef unsigned long (__stdcall *FnGetModuleHandleA)(const char* name);
typedef unsigned long (__stdcall *FnGetProcAddressRaw)(unsigned long module, const char* name);
typedef unsigned long (__stdcall *FnGetTickCount)(void);
typedef unsigned long (__stdcall *FnGetModuleFileNameA)(unsigned long module, char* buffer, unsigned long size);
typedef unsigned int (__stdcall *FnGetPrivateProfileIntA)(const char* section,
                                                           const char* key,
                                                           int fallback,
                                                           const char* file_name);
typedef unsigned long (__stdcall *FnVirtualQuery)(const void* address, void* info, unsigned long size);
typedef void* (__stdcall *FnVirtualAlloc)(void* address,
                                         unsigned long size,
                                         unsigned long allocation_type,
                                         unsigned long protect);
typedef int (__stdcall *FnVirtualFree)(void* address, unsigned long size, unsigned long free_type);
typedef int (__stdcall *FnVirtualProtect)(void* address,
                                         unsigned long size,
                                         unsigned long new_protect,
                                         unsigned long* old_protect);
typedef int (__stdcall *FnFlushInstructionCache)(unsigned long process,
                                                  const void* address,
                                                  unsigned long size);
typedef unsigned long (__stdcall *FnGetCurrentProcess)(void);

typedef struct MemoryBasicInformation32 {
    unsigned long base_address;
    unsigned long allocation_base;
    unsigned long allocation_protect;
    unsigned long region_size;
    unsigned long state;
    unsigned long protect;
    unsigned long type;
} MemoryBasicInformation32;

static FnGetTickCount g_get_tick_count;
static FnGetModuleFileNameA g_get_module_file_name_a;
static FnGetPrivateProfileIntA g_get_private_profile_int_a;
static FnVirtualQuery g_virtual_query;
static FnVirtualAlloc g_virtual_alloc;
static FnVirtualFree g_virtual_free;
static FnVirtualProtect g_virtual_protect;
static FnFlushInstructionCache g_flush_instruction_cache;
static FnGetCurrentProcess g_get_current_process;
static int g_ready;

/* 用 union 在同尺寸的 32 位地址与函数指针之间传递位模式，避免数据指针转换。 */
#define DEFINE_ADDRESS_CONVERTER(name, type) \
    static type name(unsigned long address) \
    { \
        union { unsigned long address; type function; } value; \
        value.address = address; \
        return value.function; \
    }

DEFINE_ADDRESS_CONVERTER(as_get_module_handle_a, FnGetModuleHandleA)
DEFINE_ADDRESS_CONVERTER(as_get_proc_address_raw, FnGetProcAddressRaw)
DEFINE_ADDRESS_CONVERTER(as_get_tick_count, FnGetTickCount)
DEFINE_ADDRESS_CONVERTER(as_get_module_file_name_a, FnGetModuleFileNameA)
DEFINE_ADDRESS_CONVERTER(as_get_private_profile_int_a, FnGetPrivateProfileIntA)
DEFINE_ADDRESS_CONVERTER(as_virtual_query, FnVirtualQuery)
DEFINE_ADDRESS_CONVERTER(as_virtual_alloc, FnVirtualAlloc)
DEFINE_ADDRESS_CONVERTER(as_virtual_free, FnVirtualFree)
DEFINE_ADDRESS_CONVERTER(as_virtual_protect, FnVirtualProtect)
DEFINE_ADDRESS_CONVERTER(as_flush_instruction_cache, FnFlushInstructionCache)
DEFINE_ADDRESS_CONVERTER(as_get_current_process, FnGetCurrentProcess)

#undef DEFINE_ADDRESS_CONVERTER

static unsigned long read_u32(unsigned long address)
{
    const unsigned char* p = (const unsigned char*)address;
    return (unsigned long)p[0]
        | ((unsigned long)p[1] << 8)
        | ((unsigned long)p[2] << 16)
        | ((unsigned long)p[3] << 24);
}

static unsigned long resolve(FnGetProcAddressRaw get_proc_address,
                             unsigned long module,
                             const char* name)
{
    if (!get_proc_address || module == 0ul || !name) {
        return 0ul;
    }
    return get_proc_address(module, name);
}

int RuntimeWin32_Initialize(const GameProfile* profile)
{
    unsigned long get_module_handle_address;
    unsigned long get_proc_address_address;
    FnGetModuleHandleA get_module_handle_a;
    FnGetProcAddressRaw get_proc_address;
    unsigned long kernel32;

    if (g_ready) {
        return 1;
    }
    if (!profile) {
        return 0;
    }

    /*
     * 两个 IAT 槽已经由 Windows Loader 写入真实函数地址。
     * Profile 先经过 PE 身份识别，所以这里不会在未知 EXE 上盲目读取固定位置。
     */
    get_module_handle_address = read_u32(GAME_IMAGE_BASE + profile->runtime_api.get_module_handle_a_iat_rva);
    get_proc_address_address = read_u32(GAME_IMAGE_BASE + profile->runtime_api.get_proc_address_iat_rva);
    if (get_module_handle_address == 0ul || get_proc_address_address == 0ul) {
        return 0;
    }

    get_module_handle_a = as_get_module_handle_a(get_module_handle_address);
    get_proc_address = as_get_proc_address_raw(get_proc_address_address);

    kernel32 = get_module_handle_a("kernel32.dll");
    if (kernel32 == 0ul) {
        return 0;
    }

    g_get_tick_count = as_get_tick_count(resolve(get_proc_address, kernel32, "GetTickCount"));
    g_get_module_file_name_a = as_get_module_file_name_a(resolve(get_proc_address, kernel32, "GetModuleFileNameA"));
    g_get_private_profile_int_a = as_get_private_profile_int_a(resolve(get_proc_address, kernel32, "GetPrivateProfileIntA"));
    g_virtual_query = as_virtual_query(resolve(get_proc_address, kernel32, "VirtualQuery"));
    g_virtual_alloc = as_virtual_alloc(resolve(get_proc_address, kernel32, "VirtualAlloc"));
    g_virtual_free = as_virtual_free(resolve(get_proc_address, kernel32, "VirtualFree"));
    g_virtual_protect = as_virtual_protect(resolve(get_proc_address, kernel32, "VirtualProtect"));
    g_flush_instruction_cache = as_flush_instruction_cache(resolve(get_proc_address, kernel32, "FlushInstructionCache"));
    g_get_current_process = as_get_current_process(resolve(get_proc_address, kernel32, "GetCurrentProcess"));

    if (!g_get_tick_count ||
        !g_get_module_file_name_a ||
        !g_get_private_profile_int_a ||
        !g_virtual_query ||
        !g_virtual_alloc ||
        !g_virtual_free ||
        !g_virtual_protect ||
        !g_flush_instruction_cache ||
        !g_get_current_process) {
        return 0;
    }

    g_ready = 1;
    return 1;
}

int RuntimeWin32_IsReady(void)
{
    return g_ready ? 1 : 0;
}

unsigned long RuntimeWin32_TickCount(void)
{
    return g_ready ? g_get_tick_count() : 0ul;
}

int RuntimeWin32_GetPrivateProfileInt(const char* section,
                                      const char* key,
                                      int fallback,
                                      const char* file_name)
{
    if (!g_ready || !section || !key || !file_name) {
        return fallback;
    }
    return (int)g_get_private_profile_int_a(section, key, fallback, file_name);
}

unsigned long RuntimeWin32_GetModuleFileName(void* module, char* buffer, unsigned long capacity)
{
    union { void* pointer; unsigned long value; } module_value;

    if (!g_ready || !buffer || capacity == 0ul) {
        return 0ul;
    }

    module_value.pointer = module;
    return g_get_module_file_name_a(module_value.value, buffer, capacity);
}

int RuntimeWin32_BuildSiblingPath(void* module,
                                  const char* file_name,
                                  char* output,
                                  unsigned long capacity)
{
    unsigned long length;
    unsigned long slash;
    unsigned long name_length;
    unsigned long i;

    if (!file_name || !output || capacity < 2ul) {
        return 0;
    }

    length = RuntimeWin32_GetModuleFileName(module, output, capacity);
    if (length == 0ul || length >= capacity) {
        return 0;
    }

    slash = length;
    while (slash > 0ul) {
        if (output[slash - 1ul] == '\\' || output[slash - 1ul] == '/') {
            break;
        }
        --slash;
    }

    name_length = 0ul;
    while (file_name[name_length] != '\0') {
        ++name_length;
    }

    if (slash + name_length + 1ul > capacity) {
        return 0;
    }

    for (i = 0ul; i < name_length; ++i) {
        output[slash + i] = file_name[i];
    }
    output[slash + name_length] = '\0';
    return 1;
}

int RuntimeWin32_Query(unsigned long address, RuntimeMemoryRegion* region)
{
    MemoryBasicInformation32 info;
    unsigned long result;

    if (!g_ready || address == 0ul || !region) {
        return 0;
    }

    result = g_virtual_query((const void*)address, &info, (unsigned long)sizeof(info));
    if (result != (unsigned long)sizeof(info)) {
        return 0;
    }

    region->base = info.base_address;
    region->size = info.region_size;
    region->state = info.state;
    region->protect = info.protect;
    return 1;
}

int RuntimeWin32_IsReadable(unsigned long address, unsigned long size)
{
    RuntimeMemoryRegion region;
    unsigned long end;
    unsigned long region_end;

    if (address == 0ul || size == 0ul) {
        return 0;
    }
    end = address + size;
    if (end < address) {
        return 0;
    }
    if (!RuntimeWin32_Query(address, &region)) {
        return 0;
    }
    if (region.state != MEM_COMMIT) {
        return 0;
    }
    if ((region.protect & PAGE_NOACCESS) != 0ul || (region.protect & PAGE_GUARD) != 0ul) {
        return 0;
    }

    region_end = region.base + region.size;
    if (region_end < region.base) {
        return 0;
    }
    return end <= region_end ? 1 : 0;
}

int RuntimeWin32_Read(unsigned long address, void* output, unsigned long size)
{
    unsigned char* destination;
    const unsigned char* source;
    unsigned long i;

    if (!output || !RuntimeWin32_IsReadable(address, size)) {
        return 0;
    }

    destination = (unsigned char*)output;
    source = (const unsigned char*)address;
    for (i = 0ul; i < size; ++i) {
        destination[i] = source[i];
    }
    return 1;
}

void* RuntimeWin32_AllocateExecutable(unsigned long size)
{
    if (!g_ready || size == 0ul) {
        return (void*)0;
    }
    return g_virtual_alloc((void*)0,
                           size,
                           MEM_COMMIT | MEM_RESERVE,
                           PAGE_EXECUTE_READWRITE);
}

void RuntimeWin32_FreeExecutable(void* memory)
{
    if (g_ready && memory) {
        (void)g_virtual_free(memory, 0ul, MEM_RELEASE);
    }
}

int RuntimeWin32_WriteCode(unsigned long address, const void* bytes, unsigned long size)
{
    unsigned long old_protect;
    unsigned long ignored;
    const unsigned char* source;
    unsigned char* destination;
    unsigned long i;

    if (!g_ready || address == 0ul || !bytes || size == 0ul) {
        return 0;
    }

    if (!g_virtual_protect((void*)address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return 0;
    }

    source = (const unsigned char*)bytes;
    destination = (unsigned char*)address;
    for (i = 0ul; i < size; ++i) {
        destination[i] = source[i];
    }

    /* 字节一旦写入，入口就可能已经指向新函数，不能再报告“完全没有写入”。
     * 缓存刷新或保护恢复失败时返回 2，让调用者保留跳转及其原函数网关。
     * 返回 0 只表示写入前失败，因此调用者此时才可以释放尚未使用的网关。 */
    /* 网关先在新分配的可执行区写好，再安装入口；刷新当前进程全部指令缓存，
     * 同时覆盖新网关与目标入口，避免只刷新入口而漏掉原函数网关。 */
    int cache_flushed = g_flush_instruction_cache(g_get_current_process(), (const void*)0, 0ul);

    ignored = 0ul;
    if (!g_virtual_protect((void*)address, size, old_protect, &ignored)) {
        return 2;
    }

    return cache_flushed ? 1 : 2;
}
