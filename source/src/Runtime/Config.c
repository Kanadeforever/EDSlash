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
#define P(id,key,label) {id,"gameplay.stamina",key,label,"按最大体力百分比计算，0表示没有单次变化，最多两位小数。",CONFIG_PERCENT,0,0,10000,25,NULL,NULL,CONFIG_APPLY_FRAME}
static const ConfigDescriptor fields[CONFIG_COUNT]={
    B(CONFIG_DISPLAY_ENABLED,"display","enabled","动态宽屏","保持原显示修复范围；修改后重启。",CONFIG_APPLY_RESTART),
    N(CONFIG_BASE_HEIGHT,"display","base_height","逻辑高度","默认480；更高数值扩大视野并增加游戏负担。",480,1,INT_MAX,16,CONFIG_APPLY_RESTART),
    {CONFIG_ASPECT_RATIO,"display","aspect_ratio","画面比例","auto自动，或正整数宽:高；最终宽度仍按8像素对齐。",CONFIG_TEXT,0,0,0,0,"auto",NULL,CONFIG_APPLY_RESTART},
    B(CONFIG_FONT_DPI,"font","fix_dpi","字体DPI修复","修改后重启。",CONFIG_APPLY_RESTART),
    B(CONFIG_CENTER_HUD,"gui","center_hud","HUD居中","只影响显示布局，修改后重启。",CONFIG_APPLY_RESTART),
    B(CONFIG_AUXILIARY_UI,"gui","auxiliary_above_hud","辅助界面层级","保持已验收的绘制和点击优先级。",CONFIG_APPLY_RESTART),
    B(CONFIG_GROUND_NAMES,"qol.ground_items","always_show_names","物品名称常显","关闭后恢复原版显示条件。",CONFIG_APPLY_FRAME),
    N(CONFIG_PICKUP_MODE,"qol.auto_pickup","mode","自动拾取范围","0关闭、1钱、2钱及回复、3再加宝石护身石、4全部。",2,0,4,1,CONFIG_APPLY_FRAME),
    N(CONFIG_PICKUP_INTERVAL,"qol.auto_pickup","interval_ms","拾取扫描间隔","毫秒；0为每输入帧扫描，默认100。",100,0,3000,25,CONFIG_APPLY_FRAME),
    N(CONFIG_DROP_DELAY,"qol.auto_pickup","drop_delay_ms","掉落等待","毫秒；仍需完成原版落地动画。",1000,0,3000,100,CONFIG_APPLY_FRAME),
    B(CONFIG_CONTROLLER_ENABLED,"controller","enabled","手柄功能","关闭仍可用键鼠；启动安装选择修改后重启。",CONFIG_APPLY_RESTART),
    N(CONFIG_DEADZONE,"controller.input","deadzone","摇杆死区","圆形左摇杆死区；连接时先松开全部输入。",8000,1000,24000,500,CONFIG_APPLY_FRAME),
    N(CONFIG_MOVE_LEAD,"controller.movement","lead_tiles","移动前探","地图格数；走跑共用。",12,6,32,1,CONFIG_APPLY_IDLE),
    N(CONFIG_MOUSE_SPEED,"controller.mouse","speed","鼠标模式速度","每秒桌面像素，右摇杆为三分之一。",900,100,3000,50,CONFIG_APPLY_FRAME),
    B(CONFIG_RUMBLE,"controller.feedback","rumble","手柄震动","保持已有模式切换震动。",CONFIG_APPLY_FRAME),
    B(CONFIG_CHARGE_GUARD,"gameplay.combat","charge_guard_on_hit","受击才扣防御体力","关闭恢复原版周期消耗。",CONFIG_APPLY_FRAME),
    B(CONFIG_FREE_RUN,"gameplay.combat","free_run","跑步不扣体力","不按是否战斗区分。",CONFIG_APPLY_FRAME),
    B(CONFIG_OMNI_GUARD,"gameplay.combat","omnidirectional_guard","全方位格挡","保留原5点体力门和特殊穿防。",CONFIG_APPLY_FRAME),
    B(CONFIG_DIRECTIONAL_DODGE,"gameplay.combat","directional_dodge","方向闪避","当前闪避结束后应用；关闭恢复原版目标闪避。",CONFIG_APPLY_IDLE),
    N(CONFIG_DODGE_DISTANCE,"gameplay.combat","dodge_distance","闪避距离","世界单位；64为1格，原碰撞限制保留。",128,16,512,16,CONFIG_APPLY_IDLE),
    {CONFIG_GUARD_MODE,"gameplay.stamina","guard_mode","防御消耗模式","original原角色值，percent自定百分比。",CONFIG_CHOICE,0,0,1,1,NULL,"original|percent",CONFIG_APPLY_FRAME},
    P(CONFIG_GUARD_PERCENT,"guard_percent","防御扣费百分比"),
    {CONFIG_RECOVERY_MODE,"gameplay.stamina","recovery_mode","攻击恢复模式","guard跟随防御消耗，percent自定百分比。",CONFIG_CHOICE,0,0,1,1,NULL,"guard|percent",CONFIG_APPLY_FRAME},
    P(CONFIG_RECOVERY_PERCENT,"recovery_percent","实伤恢复百分比"),
    N(CONFIG_INSPECT_DISTANCE,"controller.interaction","max_distance","正面调查最大距离",
      "世界单位，64=1格；范围16～480，默认160；360度近身搜索，优先近处及前方，不改变原互动资格。",160,16,480,16,CONFIG_APPLY_FRAME),
    {CONFIG_LEGACY_ULTIMATE,"controller.combat","single_trigger_ultimate","旧必杀输入模式",
     "false使用LT+RT+ABXY准备/再次新按释放；true恢复LT+ABXY，连招只能LT十字切换。动作菜单仍使用双扳机。",
     CONFIG_BOOL,0,0,1,1,NULL,NULL,CONFIG_APPLY_FRAME},
    {CONFIG_COMBO_SWITCH,"controller.combat","combo_switch_input","新模式连招切换输入",
     "仅single_trigger_ultimate=false时生效：face为LT+Y/B/A/X切1/2/3/4，dpad为LT+上/右/下/左；旧模式固定dpad。",
     CONFIG_CHOICE,0,0,1,1,NULL,"face|dpad",CONFIG_APPLY_FRAME},
    N(CONFIG_AIM_EXPAND_MS,"controller.skill_aim","expand_time_ms","技能落点扩散耗时",
      "从最短距离扩到原技能最远落点的毫秒数；越小越快，范围100～10000，默认1000。每次按B固定本次值，下次预览使用新设置。",
      1000,100,10000,50,CONFIG_APPLY_FRAME)
};
#undef B
#undef N
#undef P
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
int RuntimeConfig_DefaultText(char *output,size_t capacity,size_t *size)
{
    if (!output || !size) return 0;
    const char *last="";size_t position=0;
    int n=snprintf(output,capacity,"# EDSlash统一配置；UTF-8无BOM，CRLF。\r\n# 本版本只读取此TOML，不读取旧INI。\r\n[meta]\r\nschema = 1\r\n");
    if (n<0 || (size_t)n>=capacity) return 0;
    position=(size_t)n;
    for (unsigned i=0;i<CONFIG_COUNT;++i) {
        const ConfigDescriptor *f=&fields[i];char value[80];
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
        "\r\n[controller.bindings]\r\n# game跟随原游戏当前角色快捷绑定；自定义技能按游戏、角色、槽位分别保存。\r\ndefault = \"game\"\r\n"
        "# character_编号来自Player+0x348的创建角色selector；技能选择码须来自该角色实际技能。\r\n");
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
        snprintf(error_text,sizeof error_text,"第%u行：%s的类型或范围错误",entry->line,f->label);return 0;
    }
    /* 检查全部条目，拼错键不能被当作无效注释而悄悄忽略。 */
    for (unsigned i=0;i<doc->count;++i) {
        const TomlEntry *e=&doc->entries[i];int known=0;
        for (unsigned j=0;j<CONFIG_COUNT;++j)
            if (!strcmp(e->table,fields[j].table) && !strcmp(e->key,fields[j].key)) {known=1;break;}
        if (known) continue;
        int value;char text[40];
        if (!strcmp(e->table,"meta") && !strcmp(e->key,"schema")) {
            if (!Toml_Integer(doc,e,&value) || value!=1) return error("只支持最终配置schema=1");
            continue;
        }
        if (!strcmp(e->table,"controller.bindings") && !strcmp(e->key,"default")) {
            if (!Toml_String(doc,e,text,sizeof text) || strcmp(text,"game")) return error("默认技能绑定来源只能为game");
            continue;
        }
        unsigned game,role,slot;
        if (!binding_scope(e->table,&game,&role,&slot)) {
            snprintf(error_text,sizeof error_text,"第%u行：未知配置项%s.%s",e->line,e->table,e->key);return 0;
        }
        unsigned index;
        for (index=0;index<*count;++index)
            if (bindings[index].game==game && bindings[index].role==role && bindings[index].slot==slot) break;
        if (index==*count) {
            if (*count>=128) return error("角色技能绑定数量超过限制");
            memset(&bindings[index],0,sizeof bindings[index]);
            bindings[index].game=game;bindings[index].role=role;bindings[index].slot=slot;++*count;
        }
        ConfigBinding *binding=&bindings[index].binding;
        if (!strcmp(e->key,"mode")) {
            if (!Toml_String(doc,e,text,sizeof text) || (strcmp(text,"game") && strcmp(text,"skill")))
                return error("技能绑定mode只能为game或skill");
            binding->custom=!strcmp(text,"skill");
        } else if (!strcmp(e->key,"selector")) {
            if (!Toml_Integer(doc,e,&value) || value<0 || value>65535) return error("技能选择码范围为0..65535");
            binding->selector=value;
        } else if (!strcmp(e->key,"hand")) {
            if (!Toml_String(doc,e,text,sizeof text) || (strcmp(text,"left") && strcmp(text,"right")))
                return error("技能手侧只能为left或right");
            binding->right=!strcmp(text,"right");
        } else return error("未知角色技能绑定字段");
    }
    return 1;
}
int RuntimeConfig_OpenPath(const wchar_t *path)
{
    if (!path || wcslen(path)>=sizeof config_path/sizeof config_path[0]) return error("配置路径过长");
    wcscpy(config_path,path);size_t size;
    if (!RuntimeFile_Read(path,raw,sizeof raw,&size)) {
        /* 只有文件确实不存在才生成默认值，权限错误或超长文件都不能覆盖。 */
        unsigned long code=GetLastError();
        if (code!=ERROR_FILE_NOT_FOUND) return error("无法读取TOML，原文件未改");
        if (!RuntimeConfig_DefaultText(work,sizeof work,&size) ||
            !RuntimeFile_WriteAtomic(path,work,size,1) ||
            !RuntimeFile_Read(path,raw,sizeof raw,&size)) return error("无法生成默认TOML");
    }
    if (!Toml_Parse(&candidate,raw,size)) {
        snprintf(error_text,sizeof error_text,"第%u行：%s",candidate.error_line,candidate.error);return 0;
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
    if (!RuntimeFile_Sibling(module,L"EDSlash.toml",path,1100)) return error("无法定位ASI旁TOML");
    return RuntimeConfig_OpenPath(path);
}
static int commit_text(const char *bytes,size_t size)
{
    if (!ready) return error("配置服务未初始化");
    size_t current_size;
    /* 乐观并发检查：加载后被外部编辑过的文件不得由旧候选值覆盖。 */
    if (!RuntimeFile_Read(config_path,raw,sizeof raw,&current_size) ||
        current_size!=document.size || memcmp(raw,document.bytes,current_size))
        return error("配置文件已被外部修改，请重新载入后再保存");
    size_t n=0;
    for (size_t i=0;i<size;++i) {
        char ch=bytes[i];
        if (ch=='\r') {if (i+1<size && bytes[i+1]=='\n') ++i;ch='\n';}
        if (n+(ch=='\n' ? 2u:1u)>=sizeof normalized) return error("配置保存超过容量");
        if (ch=='\n') normalized[n++]='\r';
        normalized[n++]=ch;
    }
    if (!Toml_Parse(&candidate,normalized,n)) return error(candidate.error);
    ConfigSnapshot decoded;BindingRecord records[128];unsigned count;
    if (!decode(&candidate,&decoded,records,&count)) return 0;
    if (!RuntimeFile_WriteAtomic(config_path,normalized,n,0)) return error("TOML保存失败，原文件和有效设置保持");
    document=candidate;pending=decoded;pending.generation=active.generation+1;
    memcpy(pending_bindings,records,sizeof records);pending_count=count;++saved_serial;error_text[0]=0;return 1;
}
int RuntimeConfig_SetInt(ConfigId id,int value)
{
    const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);char value_text[80];size_t size;
    if (!f || f->type==CONFIG_TEXT || value<f->minimum || value>f->maximum ||
        !literal(f,value,NULL,value_text,sizeof value_text)) return error("设置值超出允许范围");
    if (!Toml_Update(&document,f->table,f->key,value_text,work,sizeof work,&size)) return error("无法生成配置候选");
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
        return error("选项名称不在允许列表内");
    }
    char value_text[80];size_t size;
    if (!f || id!=CONFIG_ASPECT_RATIO || !value || !valid_ratio(value) ||
        strlen(value)>=sizeof active.aspect_ratio || !literal(f,0,value,value_text,sizeof value_text))
        return error("画面比例必须为auto或有效宽:高");
    if (!Toml_Update(&document,f->table,f->key,value_text,work,sizeof work,&size)) return error("无法生成配置候选");
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
        (binding.right!=0 && binding.right!=1)) return error("技能绑定参数无效");
    char table[96],selector[32];size_t size;
    snprintf(table,sizeof table,"controller.bindings.%s.character_%u.slot_%u",game==1 ? "daojian":"waizhuan",role,slot);
    snprintf(selector,sizeof selector,"%d",binding.selector);
    /* 三个字段在内存里一起编辑，只进行一次原子保存，不留下半份技能绑定。 */
    candidate=document;
    const char *keys[]={"mode","selector","hand"};
    const char *values[]={binding.custom ? "\"skill\"":"\"game\"",selector,binding.right ? "\"right\"":"\"left\""};
    for (unsigned i=0;i<3;++i) {
        if (!Toml_Update(&candidate,table,keys[i],values[i],work,sizeof work,&size) ||
            !Toml_Parse(&candidate,work,size)) return error("无法生成完整技能绑定");
    }
    return commit_text(work,size);
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
    if (!action_idle && bindings_changed) pending_idle=1;
    if (action_idle && bindings_changed) {
        memcpy(active_bindings,pending_bindings,sizeof active_bindings);active_count=pending_count;changed=1;
    }
    applied_serial=saved_serial;
    if (changed) ++active.generation;
    return changed;
}
