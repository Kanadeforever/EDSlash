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
#define P(id,key,label) {id,"gameplay.stamina",key,label,(id==CONFIG_GUARD_PERCENT ? "设置防御每次消耗的体力。100%代表角色整条最大体力；1%代表其中百分之一。例如最大体力100时，1%就是1点。0表示不扣。需要将防御消耗模式设为最大体力百分比。" : "设置每次攻击造成实际伤害后恢复的体力。100%代表整条最大体力；1%代表其中百分之一。0表示不恢复。需要将攻击恢复模式设为最大体力百分比。没有伤害敌人时不恢复，恢复也不能超过体力上限。"),CONFIG_PERCENT,0,0,10000,25,NULL,NULL,CONFIG_APPLY_FRAME}
/* 世界键说明先解释对应动作，再说明改键规则；名字仍由下面每个K项目指定。 */
#define WORLD_KEYS_HELP "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。"
#define K(id,key,label,def) {id,"controller.world_keys",key,label, \
    (id==CONFIG_WORLD_INTERACT ? "按下后与附近的人物、物品、机关或换区点互动。会优先选择较近和角色前方的目标。" WORLD_KEYS_HELP : \
     id==CONFIG_WORLD_AIM ? "按住时预览动作落点，用左摇杆调整方向，松开后执行。默认用于跳跃，外传长老默认是瞬移。" WORLD_KEYS_HELP : \
     id==CONFIG_WORLD_LEFT ? "执行左手动作，通常是普通攻击。左右手动作可在同时按住LT和RT、拨动右摇杆打开的菜单中选择。" WORLD_KEYS_HELP : \
     id==CONFIG_WORLD_RIGHT ? "执行右手当前选择的技能或连招套组。可以连续按下，按游戏原来的规则衔接动作。" WORLD_KEYS_HELP : \
     id==CONFIG_WORLD_RUN ? "移动中按下后开始跑步；松开左摇杆停止移动，再移动时恢复走路。不改变跑步速度。" WORLD_KEYS_HELP : \
     id==CONFIG_WORLD_MAP ? "按下显示或隐藏小地图，方便查看周围道路和位置。" WORLD_KEYS_HELP : \
     "按下打开或关闭游戏原本的系统菜单，用于存档、读档和返回等操作。插件设置仍用LT加RT加Back打开。" WORLD_KEYS_HELP), \
    CONFIG_CHOICE,def,0,7,1,NULL,"a|b|x|y|back|start|l3|r3",CONFIG_APPLY_IDLE}

static const ConfigDescriptor fields[CONFIG_COUNT]={
    B(CONFIG_DISPLAY_ENABLED,"display","enabled","动态宽屏","让游戏画面适应较宽的屏幕，减少两侧空白。关闭时使用原游戏的画面方式。保存后需要退出游戏并重新打开才会改变。",CONFIG_APPLY_RESTART),
    N(CONFIG_BASE_HEIGHT,"display","base_height","逻辑高度","决定画面能显示多大范围的场景。第一次使用建议保持480。调大后通常能看见更多场景，但人物和物品会变小，也会增加电脑负担。保存后重新打开游戏生效。",480,1,INT_MAX,16,CONFIG_APPLY_RESTART),
    {CONFIG_ASPECT_RATIO,"display","aspect_ratio","画面比例","选择画面的宽和高之间的比例。自动会跟随游戏窗口；16:9适合常见宽屏，4:3接近原游戏。保存后需要重新打开游戏。",CONFIG_TEXT,0,0,0,0,"auto",NULL,CONFIG_APPLY_RESTART},
    B(CONFIG_FONT_DPI,"font","fix_dpi","字体DPI修复","用于修正Windows显示缩放造成的字体大小异常。如果文字大小正常，可以保持当前选择。保存后需要退出并重新打开游戏。",CONFIG_APPLY_RESTART),
    B(CONFIG_CENTER_HUD,"gui","center_hud","HUD居中","把画面底部的生命、体力、技能和物品快捷栏放在屏幕中间。关闭后使用原游戏位置。保存后直接改变位置，不需要重新打开游戏。",CONFIG_APPLY_FRAME),
    B(CONFIG_AUXILIARY_UI,"gui","auxiliary_above_hud","辅助界面层级","开启后，背包等打开的窗口会显示在底部生命条和快捷栏的前面，避免被它们遮住。关闭后使用原游戏的叠放顺序。只改变谁在前面，不移动按钮。保存后直接生效。",CONFIG_APPLY_FRAME),
    B(CONFIG_GROUND_NAMES,"qol.ground_items","always_show_names","物品名称常显","不用按住查看键，就能看到地上物品的名字，更容易判断要不要拾取。关闭后按照原游戏的方式显示物品名字。",CONFIG_APPLY_FRAME),
    N(CONFIG_PICKUP_MODE,"qol.auto_pickup","mode","自动拾取范围","角色靠近地上物品时，插件会自动拾取选定类别。可选择关闭、只捡钱、钱和恢复道具、再加入宝石护身石，或者全部物品。仍然需要有足够的背包空间；不在范围内的物品可以手动拾取。",2,0,4,1,CONFIG_APPLY_FRAME),
    N(CONFIG_PICKUP_INTERVAL,"qol.auto_pickup","interval_ms","拾取扫描间隔","自动拾取每隔多久检查一次附近物品。1000毫秒等于1秒，100毫秒等于0.1秒。调小会更快尝试拾取，但更频繁检查会增加电脑负担；0表示每次游戏刷新输入都检查。",100,0,3000,25,CONFIG_APPLY_FRAME),
    N(CONFIG_DROP_DELAY,"qol.auto_pickup","drop_delay_ms","掉落等待","怪物掉落物品后，自动拾取至少等待多久。1000毫秒等于1秒。调小可以更早拾取，但物品仍必须先完成原游戏的落地动作；0也不能拾取还在空中的物品。",1000,0,3000,100,CONFIG_APPLY_FRAME),
    B(CONFIG_CONTROLLER_ENABLED,"controller","enabled","手柄功能","开启后可以用手柄移动、战斗和操作菜单。关闭后使用原键盘鼠标；插件设置入口仍保留，可以再开启。保存后先松开手柄输入，等当前动作结束，才会切换。",CONFIG_APPLY_IDLE),
    N(CONFIG_DEADZONE,"controller.input","deadzone","摇杆死区","摇杆推动超过这段幅度才开始控制角色。调大可以减少摇杆轻微偏移造成的自行移动，但需要推得更远。调小会更灵敏。连接手柄后先松开摇杆和按键，再开始使用。",8000,1000,24000,500,CONFIG_APPLY_FRAME),
    N(CONFIG_MOVE_LEAD,"controller.movement","lead_tiles","移动前探","控制角色朝摇杆方向移动时，向前查找移动目标的距离。数值越大，目标放得越远；数值越小，目标更靠近角色。走路和跑步共用这项。修改后松开输入，等当前动作结束生效。",12,6,32,1,CONFIG_APPLY_IDLE),
    N(CONFIG_MOUSE_SPEED,"controller.mouse","speed","鼠标模式速度","在手柄的鼠标救援模式里，控制指针移动速度。调大移动更快，调小更方便对准小按钮。右摇杆使用较慢的速度。正常角色移动不受这项影响。",900,100,3000,50,CONFIG_APPLY_FRAME),
    B(CONFIG_RUMBLE,"controller.feedback","rumble","手柄震动","切换手柄和鼠标救援模式时，让手柄轻微震动，提醒切换成功。关闭后不震动。手柄本身不支持震动时，这项不会产生效果。",CONFIG_APPLY_FRAME),
    B(CONFIG_CHARGE_GUARD,"gameplay.combat","charge_guard_on_hit","受击才扣防御体力","按住LT防御时，只有成功格挡攻击才扣体力；没有受到攻击时不扣。关闭后恢复原游戏规则，持续防御也会不断扣体力。体力不足仍会按原游戏规则停止防御。",CONFIG_APPLY_FRAME),
    B(CONFIG_FREE_RUN,"gameplay.combat","free_run","跑步不扣体力","开启后跑步不消耗体力，战斗中也一样。关闭后恢复原游戏跑步消耗体力的规则。不改变跑步速度。",CONFIG_APPLY_FRAME),
    B(CONFIG_OMNI_GUARD,"gameplay.combat","omnidirectional_guard","全方位格挡","按住LT防御时，可以格挡来自前后左右的普通攻击。关闭后只按原游戏允许的方向格挡。体力太少或遇到原本就无法格挡的攻击，仍可能受伤。",CONFIG_APPLY_FRAME),
    B(CONFIG_DIRECTIONAL_DODGE,"gameplay.combat","directional_dodge","方向闪避","闪避时，左摇杆推向哪里，就朝哪里直线移动。关闭后使用原游戏的闪避方向和路径。当前闪避不会被半途改变；下一次闪避使用新选择。",CONFIG_APPLY_IDLE),
    N(CONFIG_DODGE_DISTANCE,"gameplay.combat","dodge_distance","闪避距离","方向闪避一次移动多远。调大距离更长，调小更容易停在近处。64大约相当于一个地图格。墙壁和障碍仍会限制移动。只在方向闪避开启时使用；当前闪避结束后生效。",128,16,512,16,CONFIG_APPLY_IDLE),
    {CONFIG_GUARD_MODE,"gameplay.stamina","guard_mode","防御消耗模式","选择防御扣掉多少体力：游戏原有消耗沿用这个角色的原值；最大体力百分比使用下方的防御扣费百分比。百分比以整条体力的最大值计算，不是当前剩余体力。",CONFIG_CHOICE,0,0,1,1,NULL,"original|percent",CONFIG_APPLY_FRAME},
    P(CONFIG_GUARD_PERCENT,"guard_percent","防御扣费百分比"),
    {CONFIG_RECOVERY_MODE,"gameplay.stamina","recovery_mode","攻击恢复模式","攻击确实伤害敌人时，可以恢复体力。选择与格挡消耗相同，恢复量就和防御扣费相同；选择最大体力百分比，则使用下方的实伤恢复百分比。挥空或完全被格挡不会恢复。",CONFIG_CHOICE,0,0,1,1,NULL,"guard|percent",CONFIG_APPLY_FRAME},
    P(CONFIG_RECOVERY_PERCENT,"recovery_percent","实伤恢复百分比"),
    N(CONFIG_INSPECT_DISTANCE,"controller.interaction","max_distance","正面调查最大距离",
      "按调查键时，在角色周围多远的范围内寻找物品、人物、机关和换区点。调大可以更远选中，调小更容易区分靠近的目标。会优先选择较近和角色前方的目标；仍需满足原游戏的互动条件。默认160，最多480。",160,16,480,16,CONFIG_APPLY_FRAME),
    {CONFIG_LEGACY_ULTIMATE,"controller.combat","single_trigger_ultimate","旧必杀输入模式",
     "关闭时使用较安全的新方式：同时按住LT和RT，再按A/B/X/Y准备必杀；松开面键后再次按同一个键才释放。开启后只需LT加面键，较容易误触，连招切换也固定使用LT加方向键。",
     CONFIG_BOOL,0,0,1,1,NULL,NULL,CONFIG_APPLY_FRAME},
    {CONFIG_COMBO_SWITCH,"controller.combat","combo_switch_input","新模式连招切换输入",
     "选择四套连招的切换方法。面键方式为按住LT，再按Y/B/A/X选择第1/2/3/4套；方向键方式为LT加上/右/下/左。只有旧必杀输入模式关闭时可以选择；旧模式固定用方向键。",
     CONFIG_CHOICE,0,0,1,1,NULL,"face|dpad",CONFIG_APPLY_FRAME},
    N(CONFIG_AIM_EXPAND_MS,"controller.skill_aim","expand_time_ms","技能落点扩散耗时",
      "按住跳跃预览键时，落点从近处扩到最远处需要多久。1000毫秒等于1秒。调小扩得更快，调大更方便慢慢选距离。松开按键后朝预览位置跳跃，长老使用瞬移。正在预览的这一次不受修改影响。",
      1000,100,10000,50,CONFIG_APPLY_FRAME),
    {CONFIG_MENU_SWAP_AB,"controller.menu","swap_confirm_cancel","菜单确认取消交换",
     "交换菜单里的确认和取消按钮。关闭为A确认、B取消；开启为B确认、A取消。不会交换战斗动作和组合键。当前插件设置窗口保持打开时的按法，下次打开才使用新按法。",
     CONFIG_BOOL,0,0,1,1,NULL,NULL,CONFIG_APPLY_IDLE},
    K(CONFIG_WORLD_INTERACT,"interact","调查键",0),K(CONFIG_WORLD_AIM,"skill_aim","技能落点预览键",1),
    K(CONFIG_WORLD_LEFT,"left_action","左手动作键",2),K(CONFIG_WORLD_RIGHT,"right_action","右手动作键",3),
    K(CONFIG_WORLD_RUN,"run","奔跑切换键",6),K(CONFIG_WORLD_MAP,"minimap","小地图键",7),
    K(CONFIG_WORLD_SYSTEM,"system_menu","系统菜单键",5)
};
#undef B
#undef N
#undef P
#undef K
#undef WORLD_KEYS_HELP
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
    /* 普通世界动作必须有独立物理键；批量保存允许两项同时交换，不留下中间冲突。 */
    for(unsigned i=CONFIG_WORLD_INTERACT;i<=CONFIG_WORLD_SYSTEM;++i)
        for(unsigned j=CONFIG_WORLD_INTERACT;j<i;++j)
            if(snapshot->values[i]==snapshot->values[j]) {
                char repeated[240];snprintf(repeated,sizeof repeated,"%s 和 %s 的按钮重复。请选不同按钮；交换两键要一起改好再保存。",fields[i].label,fields[j].label);
                return error(repeated);
            }
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
        return error("批量配置参数无效");
    if(!count && !binding_count)return 1;
    /* candidate仅是工作副本；所有编辑完成并通过decode前不能更新document或pending。 */
    candidate=document;size_t size=document.size;
    for(size_t i=0;i<count;++i) {
        const ConfigEdit *edit=&edits[i];const ConfigDescriptor *f=RuntimeConfig_Descriptor(edit->id);
        char value[80];
        for(size_t j=0;j<i;++j)if(edits[j].id==edit->id)return error("批量配置包含重复字段");
        if(!f)return error("批量配置字段无效");
        if(f->type==CONFIG_TEXT) {
            if(edit->id!=CONFIG_ASPECT_RATIO || !edit->text || !valid_ratio(edit->text) ||
                strlen(edit->text)>=sizeof active.aspect_ratio || !literal(f,0,edit->text,value,sizeof value))
                return error("批量文本值无效");
        } else if(edit->value<f->minimum || edit->value>f->maximum ||
                  !literal(f,edit->value,NULL,value,sizeof value))return error("批量设置值超出范围");
        if(!Toml_Update(&candidate,f->table,f->key,value,work,sizeof work,&size) || !Toml_Parse(&candidate,work,size))
            return error("无法生成批量配置候选");
    }
    for(size_t i=0;i<binding_count;++i) {
        const ConfigBindingEdit *edit=&bindings[i];ConfigBinding b=edit->value;
        if((edit->game!=1 && edit->game!=2) || edit->role<1 || edit->role>65535 || edit->slot<1 || edit->slot>14 ||
            b.selector<0 || b.selector>65535 || (b.custom!=0 && b.custom!=1) || (b.right!=0 && b.right!=1))
            return error("批量技能绑定参数无效");
        for(size_t j=0;j<i;++j)if(bindings[j].game==edit->game && bindings[j].role==edit->role && bindings[j].slot==edit->slot)
            return error("批量技能绑定包含重复槽位");
        char table[96],selector[32];
        snprintf(table,sizeof table,"controller.bindings.%s.character_%u.slot_%u",edit->game==1 ? "daojian":"waizhuan",edit->role,edit->slot);
        snprintf(selector,sizeof selector,"%d",b.selector);
        const char *keys[]={"mode","selector","hand"};
        const char *values[]={b.custom ? "\"skill\"":"\"game\"",selector,b.right ? "\"right\"":"\"left\""};
        for(unsigned j=0;j<3;++j)
            if(!Toml_Update(&candidate,table,keys[j],values[j],work,sizeof work,&size) || !Toml_Parse(&candidate,work,size))
                return error("无法生成完整批量技能绑定");
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
