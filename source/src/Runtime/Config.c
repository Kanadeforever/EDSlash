#include "RuntimeText.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include "Config.h"
#include "Toml.h"
#include "FileIO.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* 描述表是默认模板、范围校验和未来界面的共同来源，防止三处各写一套默认值。 */
#define B(id,table,key,label,description,apply) {id,table,key,label,description,CONFIG_BOOL,1,0,1,1,NULL,NULL,apply}
#define N(id,table,key,label,description,def,min,max,step,apply) {id,table,key,label,description,CONFIG_INT,def,min,max,step,NULL,NULL,apply}
#define P(id,key,label) {id,"gameplay.stamina",key,label,(id==CONFIG_GUARD_PERCENT ? RuntimeText_Config_GuardStaminaPercentDescription : RuntimeText_Config_HitRecoveryPercentDescription),CONFIG_PERCENT,0,0,10000,25,NULL,NULL,CONFIG_APPLY_FRAME}
/* 世界键说明先解释对应动作，再说明改键规则；名字仍由下面每个K项目指定。 */
#define K(id,key,label,def) {id,"controller.world_keys",key,label, \
    (id==CONFIG_WORLD_INTERACT ? RuntimeText_Config_InteractButtonDescription : \
     id==CONFIG_WORLD_AIM ? RuntimeText_Config_AimButtonDescription : \
     id==CONFIG_WORLD_LEFT ? RuntimeText_Config_LeftActionButtonDescription : \
     id==CONFIG_WORLD_RIGHT ? RuntimeText_Config_RightActionButtonDescription : \
     id==CONFIG_WORLD_RUN ? RuntimeText_Config_RunButtonDescription : \
     id==CONFIG_WORLD_MAP ? RuntimeText_Config_MapButtonDescription : \
     RuntimeText_Config_SystemMenuButtonDescription), \
    CONFIG_CHOICE,def,0,7,1,NULL,"a|b|x|y|back|start|l3|r3",CONFIG_APPLY_IDLE}

static const ConfigDescriptor fields[CONFIG_COUNT]={
    B(CONFIG_DISPLAY_ENABLED,"display","enabled",RuntimeText_Config_DisplayEnabledLabel,RuntimeText_Config_DisplayEnabledDescription,CONFIG_APPLY_RESTART),
    N(CONFIG_BASE_HEIGHT,"display","base_height",RuntimeText_Config_BaseHeightLabel,RuntimeText_Config_BaseHeightDescription,480,1,INT_MAX,16,CONFIG_APPLY_RESTART),
    {CONFIG_ASPECT_RATIO,"display","aspect_ratio",RuntimeText_Config_AspectRatioLabel,RuntimeText_Config_AspectRatioDescription,CONFIG_TEXT,0,0,0,0,"auto",NULL,CONFIG_APPLY_RESTART},
    B(CONFIG_FONT_DPI,"font","fix_dpi",RuntimeText_Config_FontDpiFixLabel,RuntimeText_Config_FontDpiFixDescription,CONFIG_APPLY_RESTART),
    B(CONFIG_CENTER_HUD,"gui","center_hud",RuntimeText_Config_CenterMainHudLabel,RuntimeText_Config_CenterMainHudDescription,CONFIG_APPLY_FRAME),
    B(CONFIG_AUXILIARY_UI,"gui","auxiliary_above_hud",RuntimeText_Config_AuxiliaryUiAboveHudLabel,RuntimeText_Config_AuxiliaryUiAboveHudDescription,CONFIG_APPLY_FRAME),
    B(CONFIG_GROUND_NAMES,"qol.ground_items","always_show_names",RuntimeText_Config_ItemNamesAlwaysVisibleLabel,RuntimeText_Config_ItemNamesAlwaysVisibleDescription,CONFIG_APPLY_FRAME),
    N(CONFIG_PICKUP_MODE,"qol.auto_pickup","mode",RuntimeText_Config_AutoPickupModeLabel,RuntimeText_Config_AutoPickupModeDescription,2,0,4,1,CONFIG_APPLY_FRAME),
    N(CONFIG_PICKUP_INTERVAL,"qol.auto_pickup","interval_ms",RuntimeText_Config_PickupScanIntervalLabel,RuntimeText_Config_PickupScanIntervalDescription,100,0,3000,25,CONFIG_APPLY_FRAME),
    N(CONFIG_DROP_DELAY,"qol.auto_pickup","drop_delay_ms",RuntimeText_Config_DropWaitLabel,RuntimeText_Config_DropWaitDescription,1000,0,3000,100,CONFIG_APPLY_FRAME),
    B(CONFIG_CONTROLLER_ENABLED,"controller","enabled",RuntimeText_Config_ControllerEnabledLabel,RuntimeText_Config_ControllerEnabledDescription,CONFIG_APPLY_IDLE),
    N(CONFIG_DEADZONE,"controller.input","deadzone",RuntimeText_Config_StickDeadzoneLabel,RuntimeText_Config_StickDeadzoneDescription,8000,1000,24000,500,CONFIG_APPLY_FRAME),
    N(CONFIG_MOVE_LEAD,"controller.movement","lead_tiles",RuntimeText_Config_MoveLeadLabel,RuntimeText_Config_MoveLeadDescription,12,6,32,1,CONFIG_APPLY_IDLE),
    N(CONFIG_MOUSE_SPEED,"controller.mouse","speed",RuntimeText_Config_MouseModeSpeedLabel,RuntimeText_Config_MouseModeSpeedDescription,900,100,3000,50,CONFIG_APPLY_FRAME),
    B(CONFIG_RUMBLE,"controller.feedback","rumble",RuntimeText_Config_RumbleLabel,RuntimeText_Config_RumbleDescription,CONFIG_APPLY_FRAME),
    B(CONFIG_CHARGE_GUARD,"gameplay.combat","charge_guard_on_hit",RuntimeText_Config_GuardChargeOnHitLabel,RuntimeText_Config_GuardChargeOnHitDescription,CONFIG_APPLY_FRAME),
    B(CONFIG_FREE_RUN,"gameplay.combat","free_run",RuntimeText_Config_RunWithoutStaminaCostLabel,RuntimeText_Config_RunWithoutStaminaCostDescription,CONFIG_APPLY_FRAME),
    B(CONFIG_OMNI_GUARD,"gameplay.combat","omnidirectional_guard",RuntimeText_Config_OmnidirectionalGuardLabel,RuntimeText_Config_OmnidirectionalGuardDescription,CONFIG_APPLY_FRAME),
    B(CONFIG_DIRECTIONAL_DODGE,"gameplay.combat","directional_dodge",RuntimeText_Config_DirectionalDodgeLabel,RuntimeText_Config_DirectionalDodgeDescription,CONFIG_APPLY_IDLE),
    N(CONFIG_DODGE_DISTANCE,"gameplay.combat","dodge_distance",RuntimeText_Config_DodgeDistanceLabel,RuntimeText_Config_DodgeDistanceDescription,128,16,512,16,CONFIG_APPLY_IDLE),
    {CONFIG_GUARD_MODE,"gameplay.stamina","guard_mode",RuntimeText_Config_GuardCostModeLabel,RuntimeText_Config_GuardCostModeDescription,CONFIG_CHOICE,0,0,1,1,NULL,"original|percent",CONFIG_APPLY_FRAME},
    P(CONFIG_GUARD_PERCENT,"guard_percent",RuntimeText_Config_GuardStaminaPercentLabel),
    {CONFIG_RECOVERY_MODE,"gameplay.stamina","recovery_mode",RuntimeText_Config_HitRecoveryModeLabel,RuntimeText_Config_HitRecoveryModeDescription,CONFIG_CHOICE,0,0,1,1,NULL,"guard|percent",CONFIG_APPLY_FRAME},
    P(CONFIG_RECOVERY_PERCENT,"recovery_percent",RuntimeText_Config_HitRecoveryPercentLabel),
    N(CONFIG_INSPECT_DISTANCE,"controller.interaction","max_distance",RuntimeText_Config_InteractDistanceLabel,
      RuntimeText_Config_InteractDistanceDescription,160,16,480,16,CONFIG_APPLY_FRAME),
    {CONFIG_LEGACY_ULTIMATE,"controller.combat","single_trigger_ultimate",RuntimeText_Config_LegacyFinisherInputLabel,
     RuntimeText_Config_LegacyFinisherInputDescription,
     CONFIG_BOOL,0,0,1,1,NULL,NULL,CONFIG_APPLY_FRAME},
    {CONFIG_COMBO_SWITCH,"controller.combat","combo_switch_input",RuntimeText_Config_ComboSwitchInputLabel,
     RuntimeText_Config_ComboSwitchInputDescription,
     CONFIG_CHOICE,0,0,1,1,NULL,"face|dpad",CONFIG_APPLY_FRAME},
    N(CONFIG_AIM_EXPAND_MS,"controller.skill_aim","expand_time_ms",RuntimeText_Config_AimSpreadTimeLabel,
      RuntimeText_Config_AimSpreadTimeDescription,
      1000,100,10000,50,CONFIG_APPLY_FRAME),
    {CONFIG_MENU_SWAP_AB,"controller.menu","swap_confirm_cancel",RuntimeText_Config_SwapMenuConfirmCancelLabel,
     RuntimeText_Config_SwapMenuConfirmCancelDescription,
     CONFIG_BOOL,0,0,1,1,NULL,NULL,CONFIG_APPLY_IDLE},
    {CONFIG_ACTION_MENU_NAV,"controller.menu","skill_menu_navigation",RuntimeText_Config_SkillMenuNavigationLabel,
     RuntimeText_Config_SkillMenuNavigationDescription,CONFIG_CHOICE,2,0,2,1,NULL,"left|right|independent",CONFIG_APPLY_IDLE},
    K(CONFIG_WORLD_INTERACT,"interact",RuntimeText_Config_InteractButtonLabel,0),K(CONFIG_WORLD_AIM,"skill_aim",RuntimeText_Config_AimButtonLabel,1),
    K(CONFIG_WORLD_LEFT,"left_action",RuntimeText_Config_LeftActionButtonLabel,2),K(CONFIG_WORLD_RIGHT,"right_action",RuntimeText_Config_RightActionButtonLabel,3),
    K(CONFIG_WORLD_RUN,"run",RuntimeText_Config_RunButtonLabel,6),K(CONFIG_WORLD_MAP,"minimap",RuntimeText_Config_MapButtonLabel,7),
    K(CONFIG_WORLD_SYSTEM,"system_menu",RuntimeText_Config_SystemMenuButtonLabel,5),
    B(CONFIG_LOG_ENABLED,"logging","enabled",RuntimeText_Config_LoggingEnabledLabel,RuntimeText_Config_LoggingEnabledDescription,CONFIG_APPLY_FRAME)
};
#undef B
#undef N
#undef P
#undef K
typedef struct {unsigned game,role,slot;ConfigBinding binding;} BindingRecord;
/* 文档和候选缓冲放静态区，不把64KiB文档压到游戏调用栈，也不逐帧分配。 */
static TomlDocument document,candidate;
static char work[TOML_CAPACITY],raw[TOML_CAPACITY],normalized[TOML_CAPACITY];
static wchar_t config_path[1100];
static ConfigSnapshot active,pending;
static BindingRecord active_bindings[128],pending_bindings[128];
static unsigned active_count,pending_count;
static int ready,needs_restart,pending_idle;
static unsigned saved_serial,applied_serial;
static char error_text[240];

const ConfigDescriptor *RuntimeConfig_Descriptor(ConfigId id)
{return (unsigned)id<CONFIG_COUNT ? &fields[id]:NULL;}
const ConfigSnapshot *RuntimeConfig_Current(void) {return &active;}
int RuntimeConfig_GetInt(ConfigId id) {return (unsigned)id<CONFIG_COUNT ? active.values[id]:0;}
const char *RuntimeConfig_Error(void) {return error_text;}
int RuntimeConfig_NeedsRestart(void) {return needs_restart;}
static int error(const char *text)
{snprintf(error_text,sizeof error_text,"%s",text);return 0;}
static int choice_text(const ConfigDescriptor *field,int value,char *output,size_t capacity)
{
    const char *begin=field->choices;
    if (!begin) return 0;
    for (int i=0;i<value;++i) {begin=strchr(begin,'|');if (!begin) return 0;++begin;}
    const char *end=strchr(begin,'|');size_t length=end ? (size_t)(end-begin):strlen(begin);
    if (length+1>capacity) return 0;
    memcpy(output,begin,length);output[length]=0;return 1;
}
static int literal(const ConfigDescriptor *field,int value,const char *text,char *output,size_t capacity)
{
    char choice[40];int length;
    if (field->type==CONFIG_BOOL) length=snprintf(output,capacity,"%s",value ? "true":"false");
    else if (field->type==CONFIG_PERCENT) length=snprintf(output,capacity,"%d.%02d",value/100,value%100);
    else if (field->type==CONFIG_CHOICE) {
        if (!choice_text(field,value,choice,sizeof choice)) return 0;
        length=snprintf(output,capacity,"\"%s\"",choice);
    } else if (field->type==CONFIG_TEXT) length=snprintf(output,capacity,"\"%s\"",text);
    else length=snprintf(output,capacity,"%d",value);
    return length>=0 && (size_t)length<capacity;
}
/* 日志开关是总入口：旧配置首次补入此表时放在第一个表之前，原根键和注释保留。 */
static int config_update(const TomlDocument *doc,const char *table,const char *key,const char *value,char *output,size_t capacity,size_t *size)
{
    if(strcmp(table,"logging") || Toml_Find(doc,table,key))return Toml_Update(doc,table,key,value,output,capacity,size);
    for(unsigned i=0;i<doc->count;++i)if(!strcmp(doc->entries[i].table,"logging"))return Toml_Update(doc,table,key,value,output,capacity,size);
    size_t at=0;
    while(at<doc->size){size_t begin=at;while(at<doc->size && doc->bytes[at]!='\n')++at;if(at<doc->size)++at;
        while(begin<at && (doc->bytes[begin]==' ' || doc->bytes[begin]=='\t'))++begin;
        if(begin<at && doc->bytes[begin]=='['){at=begin;break;}}
    char prefix[256];int n=snprintf(prefix,sizeof prefix,RuntimeText_Config_LoggingSectionTemplate,key,value);
    if(n<0 || (size_t)n>=sizeof prefix || doc->size+(size_t)n+1>capacity)return 0;
    memcpy(output,doc->bytes,at);memcpy(output+at,prefix,(size_t)n);memcpy(output+at+n,doc->bytes+at,doc->size-at);
    *size=doc->size+(size_t)n;output[*size]=0;return 1;
}
int RuntimeConfig_DefaultText(char *output,size_t capacity,size_t *size)
{
    if (!output || !size) return 0;
    const char *last="";size_t position=0;
    int n=snprintf(output,capacity,RuntimeText_Config_DefaultHeaderTemplate);
    if (n<0 || (size_t)n>=capacity) return 0;
    position=(size_t)n;
    for (unsigned i=0;i<CONFIG_COUNT;++i) {
        const ConfigDescriptor *f=&fields[i];char value[80];
        if(f->id==CONFIG_LOG_ENABLED)continue;
        if (!literal(f,f->default_value,f->default_text,value,sizeof value)) return 0;
        if (strcmp(last,f->table)) {
            n=snprintf(output+position,capacity-position,"\r\n[%s]\r\n",f->table);
            if (n<0 || (size_t)n>=capacity-position) return 0;
            position+=(size_t)n;last=f->table;
        }
        n=snprintf(output+position,capacity-position,"# %s：%s\r\n%s = %s\r\n",f->label,f->description,f->key,value);
        if (n<0 || (size_t)n>=capacity-position) return 0;
        position+=(size_t)n;
    }
    n=snprintf(output+position,capacity-position,
        RuntimeText_Config_DefaultBindingsTemplate);
    if (n<0 || (size_t)n>=capacity-position) return 0;
    *size=position+(size_t)n;return 1;
}
static int valid_ratio(const char *text)
{
    if (!strcmp(text,"auto") || !strcmp(text,"Auto")) return 1;
    unsigned long a=0,b=0;const char *p=text;
    for (;*p>='0' && *p<='9';++p) {if(a>(unsigned long)INT_MAX/10) return 0;a=a*10+(unsigned)(*p-'0');}
    if (*p++!=':' || !a || a>INT_MAX) return 0;
    for (;*p>='0' && *p<='9';++p) {if(b>(unsigned long)INT_MAX/10) return 0;b=b*10+(unsigned)(*p-'0');}
    return !*p && b && b<=INT_MAX;
}
static int binding_scope(const char *table,unsigned *game,unsigned *role,unsigned *slot)
{
    const char *prefix="controller.bindings.";size_t length=strlen(prefix);
    if (strncmp(table,prefix,length)) return 0;
    const char *p=table+length;
    if (!strncmp(p,"daojian.",8)) {*game=1;p+=8;}
    else if (!strncmp(p,"waizhuan.",9)) {*game=2;p+=9;}
    else return 0;
    int used=0;
    return sscanf(p,"character_%u.slot_%u%n",role,slot,&used)==2 && !p[used] &&
        *role>=1 && *role<=65535 && *slot>=1 && *slot<=14;
}
static int decode(TomlDocument *doc,ConfigSnapshot *snapshot,BindingRecord *bindings,unsigned *count)
{
    memset(snapshot,0,sizeof *snapshot);memset(bindings,0,128*sizeof *bindings);*count=0;
    for (unsigned i=0;i<CONFIG_COUNT;++i) {
        const ConfigDescriptor *f=&fields[i];snapshot->values[i]=f->default_value;
        if (f->type==CONFIG_TEXT) strcpy(snapshot->aspect_ratio,f->default_text);
        const TomlEntry *entry=Toml_Find(doc,f->table,f->key);
        if (!entry) continue;
        int value=0,ok;char text[80];
        if (f->type==CONFIG_TEXT) {
            if (!Toml_String(doc,entry,text,sizeof text) || !valid_ratio(text)) goto invalid;
            strcpy(snapshot->aspect_ratio,text);continue;
        }
        if (f->type==CONFIG_CHOICE) {
            if (!Toml_String(doc,entry,text,sizeof text)) goto invalid;
            ok=0;
            for (value=f->minimum;value<=f->maximum;++value) {
                char item[40];if (!choice_text(f,value,item,sizeof item)) goto invalid;
                if (!strcmp(text,item)) {ok=1;break;}
            }
        } else if (f->type==CONFIG_BOOL) ok=Toml_Bool(doc,entry,&value);
        else if (f->type==CONFIG_PERCENT) ok=Toml_Percent(doc,entry,&value);
        else ok=Toml_Integer(doc,entry,&value);
        if (!ok || value<f->minimum || value>f->maximum) goto invalid;
        snapshot->values[i]=value;continue;
invalid:
        snprintf(error_text,sizeof error_text,RuntimeText_Config_TypeOrRangeError,entry->line,f->label);return 0;
    }
    /* 检查全部条目，拼错键不能被当作无效注释而悄悄忽略。 */
    /* 普通世界动作必须有独立物理键；批量保存允许两项同时交换，不留下中间冲突。 */
    for(unsigned i=CONFIG_WORLD_INTERACT;i<=CONFIG_WORLD_SYSTEM;++i)
        for(unsigned j=CONFIG_WORLD_INTERACT;j<i;++j)
            if(snapshot->values[i]==snapshot->values[j]) {
                char repeated[240];snprintf(repeated,sizeof repeated,RuntimeText_Config_DuplicateWorldButtonError,fields[i].label,fields[j].label);
                return error(repeated);
            }
    for (unsigned i=0;i<doc->count;++i) {
        const TomlEntry *e=&doc->entries[i];int known=0;
        for (unsigned j=0;j<CONFIG_COUNT;++j)
            if (!strcmp(e->table,fields[j].table) && !strcmp(e->key,fields[j].key)) {known=1;break;}
        if (known) continue;
        int value;char text[40];
        if (!strcmp(e->table,"meta") && !strcmp(e->key,"schema")) {
            if (!Toml_Integer(doc,e,&value) || value!=1) return error(RuntimeText_Config_UnsupportedSchemaError);
            continue;
        }
        if (!strcmp(e->table,"controller.bindings") && !strcmp(e->key,"default")) {
            if (!Toml_String(doc,e,text,sizeof text) || (strcmp(text,"none") && strcmp(text,"game"))) return error(RuntimeText_Config_InvalidDefaultBindingError);
            continue;
        }
        unsigned game,role,slot;
        if (!binding_scope(e->table,&game,&role,&slot)) {
            snprintf(error_text,sizeof error_text,RuntimeText_Config_UnknownFieldError,e->line,e->table,e->key);return 0;
        }
        unsigned index;
        for (index=0;index<*count;++index)
            if (bindings[index].game==game && bindings[index].role==role && bindings[index].slot==slot) break;
        if (index==*count) {
            if (*count>=128) return error(RuntimeText_Config_BindingCapacityError);
            memset(&bindings[index],0,sizeof bindings[index]);
            bindings[index].game=game;bindings[index].role=role;bindings[index].slot=slot;++*count;
        }
        ConfigBinding *binding=&bindings[index].binding;
        if (!strcmp(e->key,"mode")) {
            if (!Toml_String(doc,e,text,sizeof text) || (strcmp(text,"none") && strcmp(text,"game") && strcmp(text,"skill")))
                return error(RuntimeText_Config_InvalidBindingModeError);
            binding->custom=!strcmp(text,"skill");
        } else if (!strcmp(e->key,"selector")) {
            if (!Toml_Integer(doc,e,&value) || value<0 || value>65535) return error(RuntimeText_Config_InvalidSkillSelectorError);
            binding->selector=value;
        } else if (!strcmp(e->key,"hand")) {
            if (!Toml_String(doc,e,text,sizeof text) || (strcmp(text,"left") && strcmp(text,"right")))
                return error(RuntimeText_Config_InvalidHandError);
            binding->right=!strcmp(text,"right");
        } else return error(RuntimeText_Config_UnknownBindingFieldError);
    }
    return 1;
}
int RuntimeConfig_OpenPath(const wchar_t *path)
{
    if (!path || wcslen(path)>=sizeof config_path/sizeof config_path[0]) return error(RuntimeText_Config_PathTooLongError);
    wcscpy(config_path,path);size_t size;
    if (!RuntimeFile_Read(path,raw,sizeof raw,&size)) {
        /* 只有文件确实不存在才生成默认值，权限错误或超长文件都不能覆盖。 */
        unsigned long code=GetLastError();
        if (code!=ERROR_FILE_NOT_FOUND) return error(RuntimeText_Config_ReadFileError);
        if (!RuntimeConfig_DefaultText(work,sizeof work,&size) ||
            !RuntimeFile_WriteAtomic(path,work,size,1) ||
            !RuntimeFile_Read(path,raw,sizeof raw,&size)) return error(RuntimeText_Config_DefaultTemplateError);
    }
    if (!Toml_Parse(&candidate,raw,size)) {
        snprintf(error_text,sizeof error_text,RuntimeText_Config_ParseLineError,candidate.error_line,candidate.error);return 0;
    }
    ConfigSnapshot decoded;BindingRecord records[128];unsigned count;
    if (!decode(&candidate,&decoded,records,&count)) return 0;
    document=candidate;active=decoded;memcpy(active_bindings,records,sizeof records);active_count=count;
    pending=active;memcpy(pending_bindings,active_bindings,sizeof active_bindings);pending_count=active_count;
    active.generation=pending.generation=1;needs_restart=pending_idle=0;saved_serial=applied_serial=0;ready=1;error_text[0]=0;return 1;
}
int RuntimeConfig_Initialize(void *module)
{
    wchar_t path[1100];
    if (!RuntimeFile_Sibling(module,L"EDSlash.toml",path,1100)) return error(RuntimeText_Config_LocateSiblingFileError);
    return RuntimeConfig_OpenPath(path);
}
static int commit_text(const char *bytes,size_t size)
{
    if (!ready) return error(RuntimeText_Config_NotInitializedError);
    size_t current_size;
    /* 乐观并发检查：加载后被外部编辑过的文件不得由旧候选值覆盖。 */
    if (!RuntimeFile_Read(config_path,raw,sizeof raw,&current_size) ||
        current_size!=document.size || memcmp(raw,document.bytes,current_size))
        return error(RuntimeText_Config_ExternallyModifiedError);
    size_t n=0;
    for (size_t i=0;i<size;++i) {
        char ch=bytes[i];
        if (ch=='\r') {if (i+1<size && bytes[i+1]=='\n') ++i;ch='\n';}
        if (n+(ch=='\n' ? 2u:1u)>=sizeof normalized) return error(RuntimeText_Config_SaveCapacityError);
        if (ch=='\n') normalized[n++]='\r';
        normalized[n++]=ch;
    }
    if (!Toml_Parse(&candidate,normalized,n)) return error(candidate.error);
    ConfigSnapshot decoded;BindingRecord records[128];unsigned count;
    if (!decode(&candidate,&decoded,records,&count)) return 0;
    if (!RuntimeFile_WriteAtomic(config_path,normalized,n,0)) return error(RuntimeText_Config_SaveFileError);
    document=candidate;pending=decoded;pending.generation=active.generation+1;
    memcpy(pending_bindings,records,sizeof records);pending_count=count;++saved_serial;error_text[0]=0;return 1;
}
int RuntimeConfig_SetInt(ConfigId id,int value)
{
    const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);char value_text[80];size_t size;
    if (!f || f->type==CONFIG_TEXT || value<f->minimum || value>f->maximum ||
        !literal(f,value,NULL,value_text,sizeof value_text)) return error(RuntimeText_Config_ValueOutOfRangeError);
    if (!config_update(&document,f->table,f->key,value_text,work,sizeof work,&size)) return error(RuntimeText_Config_CandidateGenerationError);
    return commit_text(work,size);
}
int RuntimeConfig_SetText(ConfigId id,const char *value)
{
    const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);
    if (f && f->type==CONFIG_CHOICE && value) {
        /* 枚举在TOML里是名字，在内存里是编号。先比对描述表允许的名字，
         * 再复用整数保存链；未知名字不写磁盘，也不改变待应用快照。 */
        const ConfigDescriptor *field=f;
        for (int index=field->minimum;index<=field->maximum;++index) {
            char item[40];
            if (choice_text(field,index,item,sizeof item) && !strcmp(item,value))
                return RuntimeConfig_SetInt(id,index);
        }
        return error(RuntimeText_Config_InvalidChoiceError);
    }
    char value_text[80];size_t size;
    if (!f || id!=CONFIG_ASPECT_RATIO || !value || !valid_ratio(value) ||
        strlen(value)>=sizeof active.aspect_ratio || !literal(f,0,value,value_text,sizeof value_text))
        return error(RuntimeText_Config_InvalidAspectRatioError);
    if (!config_update(&document,f->table,f->key,value_text,work,sizeof work,&size)) return error(RuntimeText_Config_CandidateGenerationError);
    return commit_text(work,size);
}
ConfigBinding RuntimeConfig_GetBinding(unsigned game,unsigned role,unsigned slot)
{
    for (unsigned i=0;i<active_count;++i)
        if (active_bindings[i].game==game && active_bindings[i].role==role && active_bindings[i].slot==slot)
            return active_bindings[i].binding;
    ConfigBinding automatic={0,0,0};return automatic;
}
int RuntimeConfig_SetBinding(unsigned game,unsigned role,unsigned slot,ConfigBinding binding)
{
    if ((game!=1 && game!=2) || role<1 || role>65535 || slot<1 || slot>14 ||
        binding.selector<0 || binding.selector>65535 || (binding.custom!=0 && binding.custom!=1) ||
        (binding.right!=0 && binding.right!=1)) return error(RuntimeText_Config_InvalidBindingArgumentsError);
    char table[96],selector[32];size_t size;
    snprintf(table,sizeof table,"controller.bindings.%s.character_%u.slot_%u",game==1 ? "daojian":"waizhuan",role,slot);
    snprintf(selector,sizeof selector,"%d",binding.selector);
    /* 三个字段在内存里一起编辑，只进行一次原子保存，不留下半份技能绑定。 */
    candidate=document;
    const char *keys[]={"mode","selector","hand"};
    const char *values[]={binding.custom ? "\"skill\"":"\"none\"",selector,binding.right ? "\"right\"":"\"left\""};
    for (unsigned i=0;i<3;++i) {
        if (!config_update(&candidate,table,keys[i],values[i],work,sizeof work,&size) ||
            !Toml_Parse(&candidate,work,size)) return error(RuntimeText_Config_BindingGenerationError);
    }
    return commit_text(work,size);
}
const ConfigSnapshot *RuntimeConfig_Saved(void) {return ready ? &pending:NULL;}
ConfigBinding RuntimeConfig_GetSavedBinding(unsigned game,unsigned role,unsigned slot)
{
    for(unsigned i=0;i<pending_count;++i)
        if(pending_bindings[i].game==game && pending_bindings[i].role==role && pending_bindings[i].slot==slot)
            return pending_bindings[i].binding;
    ConfigBinding automatic={0,0,0};return automatic;
}
int RuntimeConfig_SaveBatch(const ConfigEdit *edits,size_t count,
                            const ConfigBindingEdit *bindings,size_t binding_count)
{
    if(!ready || count>CONFIG_COUNT || binding_count>128 || (count && !edits) || (binding_count && !bindings))
        return error(RuntimeText_Config_InvalidBatchArgumentsError);
    if(!count && !binding_count)return 1;
    /* candidate仅是工作副本；所有编辑完成并通过decode前不能更新document或pending。 */
    candidate=document;size_t size=document.size;
    for(size_t i=0;i<count;++i) {
        const ConfigEdit *edit=&edits[i];const ConfigDescriptor *f=RuntimeConfig_Descriptor(edit->id);
        char value[80];
        for(size_t j=0;j<i;++j)if(edits[j].id==edit->id)return error(RuntimeText_Config_DuplicateBatchFieldError);
        if(!f)return error(RuntimeText_Config_InvalidBatchFieldError);
        if(f->type==CONFIG_TEXT) {
            if(edit->id!=CONFIG_ASPECT_RATIO || !edit->text || !valid_ratio(edit->text) ||
                strlen(edit->text)>=sizeof active.aspect_ratio || !literal(f,0,edit->text,value,sizeof value))
                return error(RuntimeText_Config_InvalidBatchTextError);
        } else if(edit->value<f->minimum || edit->value>f->maximum ||
                  !literal(f,edit->value,NULL,value,sizeof value))return error(RuntimeText_Config_BatchValueOutOfRangeError);
        if(!config_update(&candidate,f->table,f->key,value,work,sizeof work,&size) || !Toml_Parse(&candidate,work,size))
            return error(RuntimeText_Config_BatchCandidateGenerationError);
    }
    for(size_t i=0;i<binding_count;++i) {
        const ConfigBindingEdit *edit=&bindings[i];ConfigBinding b=edit->value;
        if((edit->game!=1 && edit->game!=2) || edit->role<1 || edit->role>65535 || edit->slot<1 || edit->slot>14 ||
            b.selector<0 || b.selector>65535 || (b.custom!=0 && b.custom!=1) || (b.right!=0 && b.right!=1))
            return error(RuntimeText_Config_InvalidBatchBindingArgumentsError);
        for(size_t j=0;j<i;++j)if(bindings[j].game==edit->game && bindings[j].role==edit->role && bindings[j].slot==edit->slot)
            return error(RuntimeText_Config_DuplicateBatchBindingSlotError);
        char table[96],selector[32];
        snprintf(table,sizeof table,"controller.bindings.%s.character_%u.slot_%u",edit->game==1 ? "daojian":"waizhuan",edit->role,edit->slot);
        snprintf(selector,sizeof selector,"%d",b.selector);
        const char *keys[]={"mode","selector","hand"};
        const char *values[]={b.custom ? "\"skill\"":"\"none\"",selector,b.right ? "\"right\"":"\"left\""};
        for(unsigned j=0;j<3;++j)
            if(!config_update(&candidate,table,keys[j],values[j],work,sizeof work,&size) || !Toml_Parse(&candidate,work,size))
                return error(RuntimeText_Config_BatchBindingGenerationError);
    }
    /* commit_text复用外部改动检测、完整decode、CRLF规范化与一次替换；生效仍在原安全边界。 */
    return commit_text(candidate.bytes,candidate.size);
}
int RuntimeConfig_HasPending(void) {return ready && (saved_serial!=applied_serial || pending_idle);}
int RuntimeConfig_ApplyFrame(int action_idle)
{
    /* 已处理的重启项只保留提示，不让它们导致每帧重新遍历设置。 */
    if (!ready || (saved_serial==applied_serial && (!pending_idle || !action_idle))) return 0;
    int changed=0;needs_restart=0;pending_idle=0;
    for (unsigned i=0;i<CONFIG_COUNT;++i) {
        const ConfigDescriptor *f=&fields[i];
        int different=f->type==CONFIG_TEXT ? strcmp(active.aspect_ratio,pending.aspect_ratio)!=0:
            active.values[i]!=pending.values[i];
        if (!different) continue;
        if (f->apply==CONFIG_APPLY_RESTART) {needs_restart=1;continue;}
        if (f->apply==CONFIG_APPLY_IDLE && !action_idle) {pending_idle=1;continue;}
        active.values[i]=pending.values[i];changed=1;
    }
    int bindings_changed=active_count!=pending_count ||
        memcmp(active_bindings,pending_bindings,pending_count*sizeof pending_bindings[0]);
    /* 技能请求在新输入时复制选择码；新绑定可在下一输入帧接收，不重写在途Runtime。 */
    if (bindings_changed) {
        memcpy(active_bindings,pending_bindings,sizeof active_bindings);active_count=pending_count;changed=1;
    }
    applied_serial=saved_serial;
    if (changed) ++active.generation;
    return changed;
}
