#include "Plugin.h"
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>
#include "ProfileData.h"

const Profile *g_profile;
static unsigned selected;

bool Profile_Attach(GameId game)
{
    /* 身份来自统一Runtime，不再重复识别本体／外传。局部签名和磁盘SHA仍独立验证。 */
    if (game!=GAME_ID_DAOJIAN && game!=GAME_ID_WAIZHUAN) return false;
    selected=game==GAME_ID_DAOJIAN ? 0u:1u;g_profile=&profiles[selected];return true;
}

bool Profile_Select(void)
{
    /* 接受本体/外传Steam与非Steam四份准确基线。只看 PE 大小不够区分被改过的 EXE；
       用系统 SHA-256 对磁盘 EXE 求值，再决定整套地址，未知改版继续拒绝。 */
    WCHAR path[MAX_PATH];
    DWORD count = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!count || count >= MAX_PATH) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = false;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) goto done;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) goto done;
    BYTE buffer[4096], digest[32];
    DWORD read;
    for (;;) {
        if (!ReadFile(file, buffer, sizeof buffer, &read, NULL)) goto done;
        if (!read) break;
        if (!CryptHashData(hash, buffer, read, 0)) goto done;
    }
    DWORD size = sizeof digest;
    if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0)) goto done;
    char hex[65];
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + i*2, 3, "%02x", digest[i]);
    Log_Write("[基线] 当前 EXE SHA-256=%s", hex);
    /* 同一游戏的Steam/非Steam准确档案可按文件散列切换，不能跨本体/外传改身份。
     * 四样本的共用入口已逐项离线核对；切换后仍完整检查各自的内存签名。 */
    unsigned game=g_profile ? g_profile->game_id:0;
    for (unsigned i=0;i<sizeof profiles/sizeof profiles[0];++i) {
        if (profiles[i].game_id==game && !strcmp(hex,profiles[i].sha256)) {
            selected=i;g_profile=&profiles[i];ok=true;break;
        }
    }

done:
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);
    return ok;
}

bool Profile_Verify(void)
{
    if (!g_profile || (uintptr_t)GetModuleHandleW(NULL) != 0x400000u) return false;
    /* 磁盘文件正确也可能被其它 ASI 在内存中改过。先完整验证，之后才允许写任何 Hook。 */
    for (unsigned i = 0; i < sizeof signatures[0] / sizeof signatures[0][0]; ++i) {
        const Signature *sig = &signatures[selected][i];
        if (!Memory_Readable((void *)sig->address, sizeof sig->bytes) ||
            memcmp((void *)sig->address, sig->bytes, sizeof sig->bytes)) {
            Log_Write("[签名失败] 地址=0x%08lx，保持原游戏输入。", (unsigned long)sig->address);
            return false;
        }
    }
    /* 原静态选择器第一条指令是mov edx,[地图全局地址]。
     * 局部签名正确不等于业务所用的全局字段正确：从实际指令取出地址再比较，
     * 外传曾因档案把589E30误写为589F28而扫描不到任何机关/出口。 */
    const BYTE *picker=(const BYTE *)g_profile->inspect_static_picker;
    uint32_t map_address;
    if (!Memory_Readable(picker,6) || picker[0]!=0x8B || picker[1]!=0x15) return false;
    memcpy(&map_address,picker+2,4);
    if (map_address!=g_profile->inspect_map_global) {
        Log_Write("[签名失败] 静态选择器地图地址=%08lx，档案地址=%08lx，保持原游戏输入。",
            (unsigned long)map_address,(unsigned long)g_profile->inspect_map_global);
        return false;
    }
    const BYTE *call = (const BYTE *)g_profile->resolver_call;
    int32_t offset;
    if (!Memory_Readable(call, 5) || call[0] != 0xE8) return false;
    memcpy(&offset, call + 1, 4);
    if ((uintptr_t)(call+5+offset)!=g_profile->resolver) return false;
    call=(const BYTE *)g_profile->history_call;
    if (!Memory_Readable(call,5) || call[0]!=0xE8) return false;
    memcpy(&offset,call+1,4);
    if ((uintptr_t)(call+5+offset)!=g_profile->history_record) return false;
    call=(const BYTE *)g_profile->retry_call;
    if (!Memory_Readable(call,5) || call[0]!=0xE8) return false;
    memcpy(&offset,call+1,4);
    if ((uintptr_t)(call+5+offset)!=g_profile->skill_release) return false;
    call=(const BYTE *)g_profile->end_call;
    if (!Memory_Readable(call,5) || call[0]!=0xE8) return false;
    memcpy(&offset,call+1,4);
    return (uintptr_t)(call+5+offset)==g_profile->end_record;
}
