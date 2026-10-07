#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include "../src/Runtime/Config.h"
#include "../src/Runtime/Toml.h"
#include "../src/Runtime/FileIO.h"
static unsigned checks;
#define CHECK(test) do {++checks;if(!(test)){fprintf(stderr,"配置检查失败，第%d行：%s；%s\n",__LINE__,#test,RuntimeConfig_Error());return 1;}}while(0)
static TomlDocument doc;
static char text[TOML_CAPACITY],updated[TOML_CAPACITY],saved[TOML_CAPACITY];
int wmain(void)
{
    size_t size;
    /* 同一生产描述表生成模板，检查最终格式和默认体验参数。 */
    CHECK(RuntimeConfig_DefaultText(text,sizeof text,&size));
    CHECK(Toml_Parse(&doc,text,size));
    CHECK(strstr(text,"\r\n") && !strstr(text,"legacy") && !strstr(text,".ini"));
    int value;char string[40];
    CHECK(Toml_Integer(&doc,Toml_Find(&doc,"controller.input","deadzone"),&value) && value==8000);
    CHECK(Toml_String(&doc,Toml_Find(&doc,"gameplay.stamina","guard_mode"),string,sizeof string) && !strcmp(string,"original"));
    const char *sample="# 中文说明\r\n[x]\r\np = 12.50 # 保留行内注释\r\ns = \"名字#不是注释\"\r\n";
    CHECK(Toml_Parse(&doc,sample,strlen(sample)));
    CHECK(Toml_Percent(&doc,Toml_Find(&doc,"x","p"),&value) && value==1250);
    CHECK(Toml_Update(&doc,"x","p","5.25",updated,sizeof updated,&size));
    CHECK(strstr(updated,"# 中文说明") && strstr(updated,"# 保留行内注释"));
    CHECK(Toml_Parse(&doc,updated,size));
    CHECK(Toml_Percent(&doc,Toml_Find(&doc,"x","p"),&value) && value==525);
    const char *invalid[]={
        "[x]\r\na=1\r\na=2\r\n","[x]\r\na=1\r\n[x]\r\nb=2\r\n",
        "[x]\r\na=\"没有收尾\r\n","[x]\r\na=01\r\n","[x]\r\na=1__0\r\n",
        "[[x]]\r\na=1\r\n","[x]\r\na=100.01\r\n","[x]\r\na=12.501\r\n",
        "[x]\r\na=\"\\q\"\r\n","\xEF\xBB\xBF[x]\r\na=1\r\n","[x]\r\na=\"\xC0\xAF\"\r\n"
    };
    for (unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i)
        CHECK(!Toml_Parse(&doc,invalid[i],strlen(invalid[i])));
    /* 文件测试只使用构建目录内的独立中文子目录，不接触玩家配置。 */
    WCHAR root[1024],dir[1100],path[1200];
    CHECK(GetCurrentDirectoryW(1024,root)>0);
    swprintf(dir,1100,L"%ls\\配置回归_%lu",root,GetCurrentProcessId());
    CHECK(CreateDirectoryW(dir,NULL));
    swprintf(path,1200,L"%ls\\EDSlash.toml",dir);
    CHECK(RuntimeConfig_OpenPath(path));
    CHECK(!RuntimeConfig_HasPending());
    CHECK(RuntimeConfig_GetInt(CONFIG_AIM_EXPAND_MS)==1000);
    CHECK(RuntimeConfig_SetInt(CONFIG_AIM_EXPAND_MS,250));
    CHECK(!RuntimeConfig_SetInt(CONFIG_AIM_EXPAND_MS,99) && !RuntimeConfig_SetInt(CONFIG_AIM_EXPAND_MS,10001));
    CHECK(RuntimeConfig_ApplyFrame(0) && RuntimeConfig_GetInt(CONFIG_AIM_EXPAND_MS)==250);
    CHECK(RuntimeConfig_GetInt(CONFIG_PICKUP_MODE)==2 && RuntimeConfig_GetInt(CONFIG_DODGE_DISTANCE)==128);
    /* 新模式缺项即false/face；真实配置保存服务校验二选一，并在输入边界应用。 */
    CHECK(RuntimeConfig_GetInt(CONFIG_LEGACY_ULTIMATE)==0 && RuntimeConfig_GetInt(CONFIG_COMBO_SWITCH)==0);
    CHECK(RuntimeConfig_SetText(CONFIG_COMBO_SWITCH,"dpad"));
    CHECK(RuntimeConfig_SetInt(CONFIG_LEGACY_ULTIMATE,1));
    CHECK(!RuntimeConfig_SetText(CONFIG_COMBO_SWITCH,"both"));
    CHECK(RuntimeConfig_ApplyFrame(0));
    CHECK(RuntimeConfig_GetInt(CONFIG_LEGACY_ULTIMATE)==1 && RuntimeConfig_GetInt(CONFIG_COMBO_SWITCH)==1);
    CHECK(RuntimeConfig_SetInt(CONFIG_DEADZONE,12000));
    CHECK(RuntimeConfig_HasPending());
    CHECK(RuntimeConfig_GetInt(CONFIG_DEADZONE)==8000);
    CHECK(RuntimeConfig_ApplyFrame(0));
    CHECK(RuntimeConfig_GetInt(CONFIG_DEADZONE)==12000 && !RuntimeConfig_HasPending());
    CHECK(!RuntimeConfig_SetInt(CONFIG_DEADZONE,99999));
    CHECK(RuntimeConfig_SetInt(CONFIG_DODGE_DISTANCE,256));
    RuntimeConfig_ApplyFrame(0);
    CHECK(RuntimeConfig_GetInt(CONFIG_DODGE_DISTANCE)==128 && RuntimeConfig_HasPending());
    CHECK(RuntimeConfig_ApplyFrame(1));
    CHECK(RuntimeConfig_GetInt(CONFIG_DODGE_DISTANCE)==256);
    CHECK(RuntimeConfig_SetInt(CONFIG_BASE_HEIGHT,720));
    RuntimeConfig_ApplyFrame(1);
    CHECK(RuntimeConfig_GetInt(CONFIG_BASE_HEIGHT)==480 && RuntimeConfig_NeedsRestart());
    /* 新调查距离走同一真实TOML描述/保存/应用链：缺项默认160，边界必须验证。 */
    CHECK(RuntimeConfig_GetInt(CONFIG_INSPECT_DISTANCE)==160);
    CHECK(!RuntimeConfig_SetInt(CONFIG_INSPECT_DISTANCE,15));
    CHECK(!RuntimeConfig_SetInt(CONFIG_INSPECT_DISTANCE,481));
    CHECK(RuntimeConfig_SetInt(CONFIG_INSPECT_DISTANCE,480));
    RuntimeConfig_ApplyFrame(0);CHECK(RuntimeConfig_GetInt(CONFIG_INSPECT_DISTANCE)==480);
    CHECK(RuntimeConfig_SetInt(CONFIG_GUARD_PERCENT,1250));
    CHECK(RuntimeConfig_SetInt(CONFIG_GUARD_MODE,1));
    RuntimeConfig_ApplyFrame(1);
    CHECK(RuntimeConfig_GetInt(CONFIG_GUARD_PERCENT)==1250 && RuntimeConfig_GetInt(CONFIG_GUARD_MODE)==1);
    CHECK(!RuntimeConfig_SetText(CONFIG_ASPECT_RATIO,"16:0"));
    CHECK(RuntimeConfig_SetText(CONFIG_ASPECT_RATIO,"32:9"));
    ConfigBinding binding={1,1001,1};
    CHECK(RuntimeConfig_SetBinding(1,4,1,binding));
    CHECK(!RuntimeConfig_GetBinding(1,4,1).custom);
    RuntimeConfig_ApplyFrame(0);
    CHECK(!RuntimeConfig_GetBinding(1,4,1).custom);
    RuntimeConfig_ApplyFrame(1);
    CHECK(RuntimeConfig_GetBinding(1,4,1).custom && RuntimeConfig_GetBinding(1,4,1).selector==1001);
    CHECK(!RuntimeConfig_GetBinding(2,4,1).custom && !RuntimeConfig_GetBinding(1,1,1).custom);
    /* 重新读取模拟重启：重启项和技能绑定必须确实落盘。 */
    CHECK(RuntimeConfig_OpenPath(path));
    CHECK(RuntimeConfig_GetInt(CONFIG_BASE_HEIGHT)==720 && !strcmp(RuntimeConfig_Current()->aspect_ratio,"32:9"));
    CHECK(RuntimeConfig_GetBinding(1,4,1).right);CHECK(RuntimeConfig_GetInt(CONFIG_INSPECT_DISTANCE)==480);
    CHECK(RuntimeFile_Read(path,saved,sizeof saved,&size));
    CHECK(size>0 && !memcmp(saved,"# EDSlash",9));
    for (size_t i=0;i<size;++i) if(saved[i]=='\n') CHECK(i>0 && saved[i-1]=='\r');
    /* 设置窗口一次提交多个字段和两个角色槽，不能在第二项无效时保存第一项。 */
    ConfigEdit edits[]={{CONFIG_DEADZONE,11000,NULL},{CONFIG_AIM_EXPAND_MS,1500,NULL},{CONFIG_BASE_HEIGHT,600,NULL}};
    ConfigBindingEdit binds[]={{1,4,2,{1,1002,1}},{2,50,14,{1,3000,0}}};
    CHECK(RuntimeConfig_SaveBatch(edits,3,binds,2));
    CHECK(RuntimeConfig_Saved()->values[CONFIG_BASE_HEIGHT]==600 && RuntimeConfig_GetInt(CONFIG_BASE_HEIGHT)==720);
    CHECK(RuntimeConfig_GetSavedBinding(1,4,2).selector==1002 && !RuntimeConfig_GetBinding(1,4,2).custom);
    CHECK(RuntimeFile_Read(path,saved,sizeof saved,&size));size_t before_size=size;
    ConfigEdit invalid_batch[]={{CONFIG_DEADZONE,12000,NULL},{CONFIG_AIM_EXPAND_MS,0,NULL}};
    CHECK(!RuntimeConfig_SaveBatch(invalid_batch,2,NULL,0));
    CHECK(RuntimeFile_Read(path,text,sizeof text,&size) && size==before_size && !memcmp(text,saved,size));
    CHECK(RuntimeConfig_Saved()->values[CONFIG_DEADZONE]==11000);
    ConfigEdit duplicate[]={{CONFIG_DEADZONE,12000,NULL},{CONFIG_DEADZONE,13000,NULL}};
    CHECK(!RuntimeConfig_SaveBatch(duplicate,2,NULL,0));
    ConfigBindingEdit invalid_binds[]={{1,4,3,{1,1003,1}},{1,0,4,{1,1004,1}}};
    CHECK(!RuntimeConfig_SaveBatch(NULL,0,invalid_binds,2));
    CHECK(!RuntimeConfig_GetSavedBinding(1,4,3).custom);
    CHECK(RuntimeFile_Read(path,text,sizeof text,&size) && size==before_size && !memcmp(text,saved,size));
    CHECK(RuntimeConfig_ApplyFrame(0) && RuntimeConfig_GetInt(CONFIG_AIM_EXPAND_MS)==1500);
    CHECK(!RuntimeConfig_GetBinding(1,4,2).custom && RuntimeConfig_HasPending() && RuntimeConfig_NeedsRestart());
    CHECK(RuntimeConfig_ApplyFrame(1) && RuntimeConfig_GetBinding(1,4,2).selector==1002);
    CHECK(RuntimeConfig_GetBinding(2,50,14).selector==3000 && RuntimeConfig_GetInt(CONFIG_BASE_HEIGHT)==720);
    /* 外部程序改过文件时，保存不能覆盖新内容。 */
    const char *external="[meta]\r\nschema=1\r\n[controller.input]\r\ndeadzone=9000\r\n";
    CHECK(RuntimeFile_WriteAtomic(path,external,strlen(external),0));
    CHECK(!RuntimeConfig_SetInt(CONFIG_DEADZONE,13000));
    CHECK(!RuntimeConfig_SaveBatch(edits,3,binds,2));
    CHECK(RuntimeFile_Read(path,text,sizeof text,&size) && !strcmp(text,external));
    CHECK(RuntimeConfig_OpenPath(path));
    /* 禁止删除共享可模拟最后替换失败：正式文件和有效快照都应保持。 */
    HANDLE lock=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    CHECK(lock!=INVALID_HANDLE_VALUE);
    CHECK(!RuntimeConfig_SetInt(CONFIG_DEADZONE,10000));
    CHECK(RuntimeConfig_GetInt(CONFIG_DEADZONE)==9000);
    CloseHandle(lock);
    CHECK(RuntimeFile_Read(path,text,sizeof text,&size) && !strcmp(text,external));
    /* 坏配置重载也不能污染原有效内存快照。 */
    const char *broken="[meta]\r\nschema=1\r\n[controller.input]\r\ndeadzone=\"错类型\"\r\n";
    CHECK(RuntimeFile_WriteAtomic(path,broken,strlen(broken),0));
    CHECK(!RuntimeConfig_OpenPath(path) && RuntimeConfig_GetInt(CONFIG_DEADZONE)==9000);
    CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(dir));
    printf("TOML实际读写、快照、绑定隔离、中文路径与失败保存检查通过：%u项\n",checks);
    return 0;
}
