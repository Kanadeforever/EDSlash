#ifndef EDSLASH_SETTINGS_MODEL_H
#define EDSLASH_SETTINGS_MODEL_H
#include "Config.h"
enum {SETTINGS_PAGE_COUNT=5,SETTINGS_PAGE_KEYMAP=3,SETTINGS_PAGE_ABOUT=4};
/* 纯界面模型不读取游戏地址；角色上下文由原游戏适配器提供。 */
typedef struct {
    unsigned page,focus[SETTINGS_PAGE_COUNT],scroll[SETTINGS_PAGE_COUNT],game,role;
    int help;
    ConfigSnapshot saved,draft;
    ConfigBinding saved_bindings[14],draft_bindings[14];
} SettingsModel;
int SettingsModel_Open(SettingsModel *model,unsigned game,unsigned role);
unsigned SettingsModel_Count(unsigned page);
ConfigId SettingsModel_Field(unsigned page,unsigned index);
unsigned SettingsModel_PageAtTab(unsigned tab);
void SettingsModel_Page(SettingsModel *model,int delta);
void SettingsModel_Move(SettingsModel *model,int direction,unsigned visible_rows);
int SettingsModel_SetInt(SettingsModel *model,ConfigId id,int value);
int SettingsModel_SetText(SettingsModel *model,const char *value);
int SettingsModel_SetBinding(SettingsModel *model,unsigned slot,ConfigBinding value);
void SettingsModel_ResetItem(SettingsModel *model,unsigned index);
void SettingsModel_ResetPage(SettingsModel *model);
int SettingsModel_Dirty(const SettingsModel *model);
int SettingsModel_Save(SettingsModel *model);
void SettingsModel_Discard(SettingsModel *model);
#endif
