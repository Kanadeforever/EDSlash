#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include "../src/Runtime/Config.h"
#include "../src/Runtime/SettingsModel.h"
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
    CHECK(Toml_String(&doc,Toml_Find(&doc,"controller.menu","skill_menu_navigation"),string,sizeof string) && !strcmp(string,"independent"));
    const ConfigDescriptor *navigation=RuntimeConfig_Descriptor(CONFIG_ACTION_MENU_NAV);
    CHECK(navigation && navigation->default_value==2 && navigation->maximum==2 && navigation->apply==CONFIG_APPLY_IDLE);
    unsigned navigation_fields=0;for(unsigned i=0;i<SettingsModel_Count(1);++i)if(SettingsModel_Field(1,i)==CONFIG_ACTION_MENU_NAV)++navigation_fields;
    CHECK(navigation_fields==1);
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
    WCHAR root[1024],dir[1100],path[1200],skill_file[1200];
    CHECK(GetCurrentDirectoryW(1024,root)>0);
    swprintf(dir,1100,L"%ls\\配置回归_%lu",root,GetCurrentProcessId());
    CHECK(CreateDirectoryW(dir,NULL));
    swprintf(path,1200,L"%ls\\EDSlash.toml",dir);
    swprintf(skill_file,1200,L"%ls\\EDSlash.SkillContols.toml",dir);
    CHECK(RuntimeConfig_OpenPath(path));
    CHECK(GetFileAttributesW(skill_file)==INVALID_FILE_ATTRIBUTES);
    CHECK(!RuntimeConfig_HasPending());
    CHECK(RuntimeConfig_GetInt(CONFIG_AIM_EXPAND_MS)==1000);
    CHECK(RuntimeConfig_GetInt(CONFIG_MENU_SWAP_AB)==0);
    CHECK(RuntimeConfig_GetInt(CONFIG_ACTION_MENU_NAV)==2);
    const char *navigation_choices[]={"left","right","independent"};
    for(int mode=0;mode<3;++mode){
        CHECK(RuntimeConfig_SetText(CONFIG_ACTION_MENU_NAV,navigation_choices[mode]));
        RuntimeConfig_ApplyFrame(1);CHECK(RuntimeConfig_GetInt(CONFIG_ACTION_MENU_NAV)==mode);
        CHECK(RuntimeConfig_OpenPath(path) && RuntimeConfig_GetInt(CONFIG_ACTION_MENU_NAV)==mode);
    }
    CHECK(!RuntimeConfig_SetText(CONFIG_ACTION_MENU_NAV,"both") && !RuntimeConfig_SetInt(CONFIG_ACTION_MENU_NAV,3));

    ConfigEdit key_swap[]={{CONFIG_WORLD_INTERACT,2,NULL},{CONFIG_WORLD_LEFT,0,NULL}};
    CHECK(!RuntimeConfig_SetInt(CONFIG_WORLD_INTERACT,2));
    CHECK(RuntimeConfig_SaveBatch(key_swap,2,NULL,0));RuntimeConfig_ApplyFrame(0);
    CHECK(RuntimeConfig_GetInt(CONFIG_WORLD_INTERACT)==0);
    CHECK(RuntimeConfig_ApplyFrame(1) && RuntimeConfig_GetInt(CONFIG_WORLD_INTERACT)==2 && RuntimeConfig_GetInt(CONFIG_WORLD_LEFT)==0);
    CHECK(RuntimeConfig_SetInt(CONFIG_MENU_SWAP_AB,1));RuntimeConfig_ApplyFrame(0);
    CHECK(RuntimeConfig_GetInt(CONFIG_MENU_SWAP_AB)==0 && RuntimeConfig_HasPending());
    CHECK(RuntimeConfig_ApplyFrame(1) && RuntimeConfig_GetInt(CONFIG_MENU_SWAP_AB)==1);
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
    CHECK(GetFileAttributesW(skill_file)==INVALID_FILE_ATTRIBUTES);
    CHECK(RuntimeConfig_SetBinding(1,4,1,(ConfigBinding){0,0,0}));
    CHECK(GetFileAttributesW(skill_file)==INVALID_FILE_ATTRIBUTES);
    ConfigBinding binding={1,1001,1};
    CHECK(RuntimeConfig_SetBinding(1,4,1,binding));
    CHECK(RuntimeFile_Read(skill_file,saved,sizeof saved,&size) && Toml_Parse(&doc,saved,size));
    CHECK(Toml_String(&doc,Toml_Find(&doc,"version","game_version"),string,sizeof string) && !strcmp(string,"DaoJian"));
    CHECK(Toml_Integer(&doc,Toml_Find(&doc,"character_4.skills","rt_a"),&value) && value==1001);
    CHECK(RuntimeFile_Read(path,text,sizeof text,&size) && !strstr(text,"controller.bindings"));
    CHECK(!RuntimeConfig_GetBinding(1,4,1).custom);
    RuntimeConfig_ApplyFrame(0);
    CHECK(RuntimeConfig_GetBinding(1,4,1).custom);
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
    /* 双列模型在末行停留，滚动跟随焦点；Y说明不改变导航或候选保存。 */
    SettingsModel model;CHECK(SettingsModel_Open(&model,1,4));
    CHECK(SettingsModel_Count(0)+SettingsModel_Count(1)==CONFIG_COUNT && SettingsModel_Count(2)==14);
    SettingsModel_Page(&model,-1);CHECK(model.page==SETTINGS_PAGE_ABOUT && !SettingsModel_Count(model.page));
    CHECK(SettingsModel_Field(model.page,0)==CONFIG_COUNT);
    ConfigSnapshot about_draft=model.draft;SettingsModel_ResetPage(&model);SettingsModel_Move(&model,2,4);
    CHECK(!memcmp(&about_draft,&model.draft,sizeof about_draft));
    SettingsModel_Page(&model,-1);CHECK(model.page==2);
    model.help=1;
    for(unsigned i=0;i<20;++i)SettingsModel_Move(&model,2,4);
    CHECK(model.focus[2]==12 && model.scroll[2]==3 && model.help);
    SettingsModel_Move(&model,4,4);CHECK(model.focus[2]==13);
    SettingsModel_Move(&model,2,4);CHECK(model.focus[2]==13);
    SettingsModel_Page(&model,1);SettingsModel_Page(&model,-1);CHECK(model.focus[2]==13 && model.scroll[2]==3);
    CHECK(SettingsModel_SetInt(&model,CONFIG_AIM_EXPAND_MS,450) && SettingsModel_Dirty(&model));
    SettingsModel_Discard(&model);CHECK(!SettingsModel_Dirty(&model));
    CHECK(SettingsModel_SetInt(&model,CONFIG_AIM_EXPAND_MS,600));
    ConfigBinding draft_binding={1,444,0};CHECK(SettingsModel_SetBinding(&model,4,draft_binding));
    CHECK(SettingsModel_Save(&model) && !SettingsModel_Dirty(&model));
    CHECK(RuntimeConfig_GetSavedBinding(1,4,4).selector==444);
    CHECK(SettingsModel_SetText(&model,"16:0") && !SettingsModel_Save(&model) && SettingsModel_Dirty(&model));
    SettingsModel_Discard(&model);CHECK(!SettingsModel_Dirty(&model));
    /* 设置窗口一次提交多个字段和两个角色槽，不能在第二项无效时保存第一项。 */
    ConfigEdit edits[]={{CONFIG_DEADZONE,11000,NULL},{CONFIG_AIM_EXPAND_MS,1500,NULL},{CONFIG_BASE_HEIGHT,600,NULL}};
    ConfigBindingEdit binds[]={{1,4,2,{1,1002,1}},{1,50,14,{1,3000,0}}};
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
    CHECK(RuntimeConfig_GetBinding(1,4,2).custom && RuntimeConfig_NeedsRestart());
    RuntimeConfig_ApplyFrame(1);CHECK(RuntimeConfig_GetBinding(1,4,2).selector==1002);
    CHECK(RuntimeConfig_GetBinding(1,50,14).selector==3000 && RuntimeConfig_GetInt(CONFIG_BASE_HEIGHT)==720);
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
    /* 日志首次补入旧配置必须位于首表，保存与实际生效仍按帧边界分开。 */
    CHECK(RuntimeConfig_SetInt(CONFIG_LOG_ENABLED,0));
    CHECK(RuntimeFile_Read(path,text,sizeof text,&size));CHECK(strstr(text,"[logging]")<strstr(text,"[meta]"));
    CHECK(RuntimeConfig_GetInt(CONFIG_LOG_ENABLED)==1);CHECK(RuntimeConfig_ApplyFrame(1));CHECK(!RuntimeConfig_GetInt(CONFIG_LOG_ENABLED));
    CHECK(RuntimeConfig_SetInt(CONFIG_LOG_ENABLED,1) && RuntimeConfig_ApplyFrame(1));
    unsigned field_seen[CONFIG_COUNT]={0};
    for(unsigned page=0;page<2;++page)for(unsigned i=0;i<SettingsModel_Count(page);++i){ConfigId id=SettingsModel_Field(page,i);CHECK(id<CONFIG_COUNT);++field_seen[id];}
    for(unsigned i=0;i<CONFIG_COUNT;++i)CHECK(field_seen[i]==1);
    /* 坏配置重载也不能污染原有效内存快照。 */
    const char *broken="[meta]\r\nschema=1\r\n[controller.input]\r\ndeadzone=\"错类型\"\r\n";
    CHECK(RuntimeFile_WriteAtomic(path,broken,strlen(broken),0));
    CHECK(!RuntimeConfig_OpenPath(path) && RuntimeConfig_GetInt(CONFIG_DEADZONE)==9000);
    /* 主TOML被新发行默认模板替换后，技能仍从独立文件恢复，不随更新丢失。 */
    CHECK(RuntimeConfig_DefaultText(updated,sizeof updated,&size));
    CHECK(RuntimeFile_WriteAtomic(path,updated,size,0) && RuntimeConfig_OpenPathForGame(path,1));
    CHECK(RuntimeConfig_GetBinding(1,4,2).selector==1002 && RuntimeConfig_GetBinding(1,50,14).selector==3000);
    CHECK(!RuntimeConfig_SetBinding(2,4,1,(ConfigBinding){1,777,1}));
    const char *external_skills="[version]\r\ngame_version=\"DaoJian\"\r\nschema=1\r\n[character_4]\r\nselector=4\r\n[character_4.skills]\r\nrt_a=4321 # 手工说明\r\n";
    CHECK(RuntimeFile_WriteAtomic(skill_file,external_skills,strlen(external_skills),0));
    CHECK(!RuntimeConfig_SetBinding(1,4,1,(ConfigBinding){1,888,1}));
    CHECK(RuntimeConfig_OpenPathForGame(path,1) && RuntimeConfig_GetBinding(1,4,1).selector==4321);
    CHECK(RuntimeConfig_SetInt(CONFIG_DEADZONE,8800));
    CHECK(RuntimeFile_Read(skill_file,text,sizeof text,&size) && !strcmp(text,external_skills));
    /* 版本不匹配自动备份；同名备份存在时使用下一个编号，绝不覆盖原件。 */
    WCHAR backup[1200],backup_next[1200],migration[1300];
    swprintf(backup,1200,L"%ls.DaoJian.bak",skill_file);swprintf(backup_next,1200,L"%ls.DaoJian.1.bak",skill_file);
    CHECK(RuntimeFile_WriteAtomic(backup,"keep",4,1));
    CHECK(RuntimeConfig_OpenPathForGame(path,2));
    CHECK(GetFileAttributesW(skill_file)==INVALID_FILE_ATTRIBUTES);
    CHECK(RuntimeFile_Read(backup,text,sizeof text,&size) && !strcmp(text,"keep"));
    CHECK(RuntimeFile_Read(backup_next,text,sizeof text,&size) && !strcmp(text,external_skills));
    CHECK(!RuntimeConfig_GetBinding(2,4,1).custom && !RuntimeConfig_GetBinding(1,4,1).custom);
    CHECK(RuntimeConfig_SetInt(CONFIG_DEADZONE,8900) && GetFileAttributesW(skill_file)==INVALID_FILE_ATTRIBUTES);
    CHECK(RuntimeConfig_SetBinding(2,50,2,(ConfigBinding){1,555,1}));
    CHECK(RuntimeFile_Read(skill_file,text,sizeof text,&size) && strstr(text,"WaiZhuan") && strstr(text,"[character_50.skills]"));
    CHECK(RuntimeConfig_SetBinding(2,50,2,(ConfigBinding){0,0,0}));
    CHECK(RuntimeFile_Read(skill_file,text,sizeof text,&size) && !strstr(text,"character_50"));
    CHECK(DeleteFileW(skill_file));
    /* 旧主文件按当前游戏迁移，另一作旧记录留在原字节备份，左侧旧语义也保持。 */
    const char *legacy="[meta]\r\nschema=1\r\n[controller.input]\r\ndeadzone=9000\r\n[controller.bindings]\r\ndefault=\"none\"\r\n[controller.bindings.daojian.character_4.slot_1]\r\nmode=\"skill\"\r\nselector=345\r\nhand=\"left\"\r\n[controller.bindings.waizhuan.character_50.slot_1]\r\nmode=\"skill\"\r\nselector=987\r\nhand=\"right\"\r\n";
    CHECK(RuntimeFile_WriteAtomic(path,legacy,strlen(legacy),0) && RuntimeConfig_OpenPathForGame(path,1));
    CHECK(RuntimeConfig_GetBinding(1,4,1).selector==345 && !RuntimeConfig_GetBinding(1,4,1).right);
    CHECK(!RuntimeConfig_GetBinding(2,50,1).custom && RuntimeConfig_GetInt(CONFIG_DEADZONE)==9000);
    swprintf(migration,1300,L"%ls.skills-migration.bak",path);
    CHECK(RuntimeFile_Read(migration,text,sizeof text,&size) && !strcmp(text,legacy));
    CHECK(RuntimeFile_Read(path,text,sizeof text,&size) && !strstr(text,"controller.bindings"));
    CHECK(RuntimeFile_Read(skill_file,saved,sizeof saved,&size));size_t skill_before=size;
    /* 主配置替换失败时，已写入的技能文件回滚；锁定技能文件时主配置不能先变化。 */
    ConfigEdit main_edit={CONFIG_DEADZONE,11000,NULL};ConfigBindingEdit skill_edit={1,4,1,{1,678,1}};
    lock=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(lock!=INVALID_HANDLE_VALUE);
    CHECK(!RuntimeConfig_SaveBatch(&main_edit,1,&skill_edit,1));CloseHandle(lock);
    CHECK(RuntimeFile_Read(skill_file,text,sizeof text,&size) && size==skill_before && !memcmp(text,saved,size));
    CHECK(RuntimeConfig_GetSavedBinding(1,4,1).selector==345 && RuntimeConfig_Saved()->values[CONFIG_DEADZONE]==9000);
    lock=CreateFileW(skill_file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(lock!=INVALID_HANDLE_VALUE);
    CHECK(!RuntimeConfig_SaveBatch(&main_edit,1,&skill_edit,1));CloseHandle(lock);
    CHECK(RuntimeFile_Read(path,text,sizeof text,&size) && strstr(text,"deadzone=9000"));
    CHECK(RuntimeConfig_SaveBatch(&main_edit,1,&skill_edit,1));
    CHECK(RuntimeConfig_OpenPathForGame(path,1) && RuntimeConfig_GetBinding(1,4,1).selector==678 && RuntimeConfig_GetInt(CONFIG_DEADZONE)==11000);
    /* 当前游戏的坏文件不改名、不覆盖，也不会悄悄变成默认配置。 */
    const char *bad_skill="[version]\r\ngame_version=\"DaoJian\"\r\nschema=1\r\n[character_4]\r\nselector=5\r\n[character_4.skills]\r\nrt_a=777\r\n";
    CHECK(RuntimeFile_WriteAtomic(skill_file,bad_skill,strlen(bad_skill),0));
    CHECK(!RuntimeConfig_OpenPathForGame(path,1));
    CHECK(RuntimeFile_Read(skill_file,text,sizeof text,&size) && !strcmp(text,bad_skill));
    CHECK(DeleteFileW(skill_file));
    const char *defaults_legacy="[meta]\r\nschema=1\r\n[controller.bindings]\r\ndefault=\"none\"\r\n";
    CHECK(RuntimeFile_WriteAtomic(path,defaults_legacy,strlen(defaults_legacy),0) && RuntimeConfig_OpenPathForGame(path,1));
    CHECK(GetFileAttributesW(skill_file)==INVALID_FILE_ATTRIBUTES);
    CHECK(RuntimeFile_Read(migration,text,sizeof text,&size) && !strcmp(text,legacy));
    CHECK(DeleteFileW(backup) && DeleteFileW(backup_next) && DeleteFileW(migration));
    CHECK(DeleteFileW(path));CHECK(RemoveDirectoryW(dir));
    printf("TOML实际读写、快照、绑定隔离、中文路径与失败保存检查通过：%u项\n",checks);
    return 0;
}
