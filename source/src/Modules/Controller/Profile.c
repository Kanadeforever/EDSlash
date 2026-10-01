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
    /* 第一版只接受两份准确基线。只看 PE 大小不够区分被改过的 EXE；
       用系统 SHA-256 对磁盘 EXE 求值，再决定整套地址，不猜 Steam 或第三方改版。 */
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
    /* 只验证Runtime选定的档案，散列匹配不能改选另一游戏身份。 */
    ok=g_profile && !strcmp(hex,g_profile->sha256);

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
