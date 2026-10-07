#include "SettingsModel.h"
#include <string.h>

static int controls(ConfigId id)
{
    return id==CONFIG_DEADZONE || id==CONFIG_MOVE_LEAD || id==CONFIG_MOUSE_SPEED || id==CONFIG_RUMBLE ||
        id==CONFIG_MENU_SWAP_AB || (id>=CONFIG_WORLD_INTERACT && id<=CONFIG_WORLD_SYSTEM);
}
ConfigId SettingsModel_Field(unsigned page,unsigned index)
{
    if(page>1)return CONFIG_COUNT;
    for(unsigned i=0;i<CONFIG_COUNT;++i)if((unsigned)controls((ConfigId)i)==page) {
        if(!index)return (ConfigId)i;
        --index;
    }
    return CONFIG_COUNT;
}
unsigned SettingsModel_Count(unsigned page)
{
    if(page==2)return 14;
    unsigned count=0;
    if(page<2)for(unsigned i=0;i<CONFIG_COUNT;++i)if((unsigned)controls((ConfigId)i)==page)++count;
    return count;
}
int SettingsModel_Open(SettingsModel *m,unsigned game,unsigned role)
{
    const ConfigSnapshot *saved=RuntimeConfig_Saved();
    if(!m || !saved || (game!=1 && game!=2) || role<1 || role>65535)return 0;
    memset(m,0,sizeof *m);m->game=game;m->role=role;m->saved=m->draft=*saved;
    for(unsigned i=0;i<14;++i)m->saved_bindings[i]=m->draft_bindings[i]=RuntimeConfig_GetSavedBinding(game,role,i+1);
    return 1;
}
void SettingsModel_Page(SettingsModel *m,int delta)
{
    if(!m)return;
    m->page=(m->page+(delta<0 ? 2u:1u))%3; /* 大类循环，每页独立保留焦点与滚动 */
}
void SettingsModel_Move(SettingsModel *m,int direction,unsigned visible)
{
    if(!m || m->page>2 || !visible)return;
    unsigned count=SettingsModel_Count(m->page),index=m->focus[m->page];
    if(index>=count)index=count ? count-1:0;
    /* 双列按视觉行列走，不在上下边界斜跳；左/右只切同一行。 */
    if(direction==1 && index>=2)index-=2;
    else if(direction==2 && index+2<count)index+=2;
    else if(direction==3 && (index&1))--index;
    else if(direction==4 && !(index&1) && index+1<count)++index;
    m->focus[m->page]=index;unsigned row=index/2,*scroll=&m->scroll[m->page];
    if(row<*scroll)*scroll=row;
    else if(row-*scroll>=visible)*scroll=row-visible+1;
}
int SettingsModel_SetInt(SettingsModel *m,ConfigId id,int value)
{
    const ConfigDescriptor *f=RuntimeConfig_Descriptor(id);
    if(!m || !f || f->type==CONFIG_TEXT || value<f->minimum || value>f->maximum)return 0;
    m->draft.values[id]=value;return 1;
}
int SettingsModel_SetText(SettingsModel *m,const char *value)
{
    if(!m || !value || strlen(value)>=sizeof m->draft.aspect_ratio)return 0;
    strcpy(m->draft.aspect_ratio,value);return 1; /* 完整比例资格由同一原子保存服务校验 */
}
int SettingsModel_SetBinding(SettingsModel *m,unsigned slot,ConfigBinding b)
{
    if(!m || slot<1 || slot>14 || b.selector<0 || b.selector>65535 ||
        (b.custom!=0 && b.custom!=1) || (b.right!=0 && b.right!=1))return 0;
    m->draft_bindings[slot-1]=b;return 1;
}
int SettingsModel_Dirty(const SettingsModel *m)
{
    if(!m)return 0;
    return memcmp(m->saved.values,m->draft.values,sizeof m->saved.values) ||
        strcmp(m->saved.aspect_ratio,m->draft.aspect_ratio) ||
        memcmp(m->saved_bindings,m->draft_bindings,sizeof m->saved_bindings);
}
void SettingsModel_Discard(SettingsModel *m)
{
    if(!m)return;
    m->draft=m->saved;memcpy(m->draft_bindings,m->saved_bindings,sizeof m->saved_bindings);
}
int SettingsModel_Save(SettingsModel *m)
{
    if(!m)return 0;
    ConfigEdit edits[CONFIG_COUNT];ConfigBindingEdit bindings[14];size_t n=0,k=0;
    for(unsigned i=0;i<CONFIG_COUNT;++i) {
        const ConfigDescriptor *f=RuntimeConfig_Descriptor((ConfigId)i);
        int changed=f->type==CONFIG_TEXT ? strcmp(m->saved.aspect_ratio,m->draft.aspect_ratio)!=0:
            m->saved.values[i]!=m->draft.values[i];
        if(changed)edits[n++]=(ConfigEdit){(ConfigId)i,m->draft.values[i],f->type==CONFIG_TEXT ? m->draft.aspect_ratio:NULL};
    }
    for(unsigned i=0;i<14;++i)if(memcmp(&m->saved_bindings[i],&m->draft_bindings[i],sizeof(ConfigBinding)))
        bindings[k++]=(ConfigBindingEdit){m->game,m->role,i+1,m->draft_bindings[i]};
    if(!RuntimeConfig_SaveBatch(edits,n,bindings,k))return 0;
    /* 只有整批成功才把候选标成保存；失败时保留全部草稿供继续修改。 */
    m->saved=m->draft;memcpy(m->saved_bindings,m->draft_bindings,sizeof m->saved_bindings);return 1;
}
