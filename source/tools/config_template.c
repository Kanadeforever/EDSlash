#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "../src/Runtime/Config.h"
/* 模板生成器只写指定输出，不读游戏或玩家旧配置。 */
int wmain(int argc,wchar_t **argv)
{
    static char bytes[65536];size_t size;
    if (argc!=2 || !RuntimeConfig_DefaultText(bytes,sizeof bytes,&size)) return 2;
    FILE *file=_wfopen(argv[1],L"wb");
    if (!file) return 1;
    int ok=fwrite(bytes,1,size,file)==size;
    if (fclose(file)) ok=0;
    return ok ? 0:1;
}
