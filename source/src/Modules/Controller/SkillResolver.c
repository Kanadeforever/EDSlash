#include "Combat.h"
#include <limits.h>

static uint16_t word(const void *p,unsigned offset)
{
    /* 招式表是紧凑布局，编号是 16 位；不能把它和旁边字段拼成 32 位编号。 */
    return (uint16_t)(Read32(p,offset)&0xFFFFu);
}

static void *lookup(uintptr_t table,int id)
{
    return (void *)(uintptr_t)((This1)g_profile->lookup)((void *)table, NULL,id);
}

static uint32_t elapsed(uint32_t now,uint32_t then)
{
    /* 与原版时间差绝对值语义一致，避免有符号 INT_MIN 求负的未定义行为。 */
    uint32_t delta=now-then;
    return (int32_t)delta<0 ? 0u-delta:delta;
}

static SkillStatus condition(void *role,void *method,const WorldPoint *point,const ActionHistory *history,bool protected_phase)
{
    uint32_t now=Read32((void *)g_profile->game_tick,0);
    int low=(int)Read32(method,0x29);
    if (low>=1000) {
        int upper=(int)Read32(method,0x2D);
        upper=upper==0xFFFF ? 0:upper-1000;
        uint32_t delta=elapsed(now,history->start_tick);
        if ((int32_t)delta>=upper && !(protected_phase && history->grace)) return SKILL_EXPIRED;
        if ((int32_t)delta<=low-1000) return SKILL_WAITING;
    } else {
        if (!history->ended) return SKILL_WAITING;
        if ((int32_t)elapsed(now,history->end_tick)>=low && !(protected_phase && history->grace)) return SKILL_EXPIRED;
    }
    /* 原版方向条件 2 要求和历史方向相差超过两个八向单位；其余类型不加这一门。
       这是招式选择规则，不能误用角色渲染的 16 向模式来比较。 */
    if (*((const BYTE *)method+0x31)==2) {
        WorldPoint origin={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
        typedef int (__cdecl *Direction)(const WorldPoint *,const WorldPoint *);
        int direction=((Direction)g_profile->direction8)(point,&origin);
        int difference=history->direction-direction;
        if (difference<0) difference=-difference;
        if (difference>4) difference=8-difference;
        if (difference<=2) return SKILL_DIRECTION;
    }
    return SKILL_READY;
}

static SkillStatus query(void *role,int selection,const WorldPoint *point,const ActionHistory *history,ResolvedSkill *out)
{
    if (selection<-1 || !role || !point || !history || !out) return SKILL_MISSING;
    *out=(ResolvedSkill){0};
    out->sequence=history->count!=0;
    void *choices=ReadPtr(role,0x193);
    if (!Memory_Readable(choices,12)) return SKILL_MISSING;
    /* 原入口将 >=10000 的选择直接作为特殊动作编号；保留值域规则，不猜角色名称。 */
    if (selection>=10000) {
        out->method=selection;out->restart=true;
        void *record=lookup(g_profile->methods,selection);
        out->selector=Memory_Readable(record,0x29) ? word(record,0x27):-1;
        return SKILL_READY;
    }
    int count=((This1)g_profile->ui_property)(choices, NULL,1);
    if (count<0 || count>1024) return SKILL_MISSING;
    void *winner=NULL;
    int best=-1,blocked_best=-1;
    SkillStatus blocked=SKILL_MISSING;
    void *blocked_method=NULL;
    bool winner_continuation=false;
    unsigned winner_index=0;
    for (int i=0;i<count;++i) {
        int group_id=((This1)g_profile->ui_property)(choices, NULL,i+2);
        if (((This1)g_profile->skill_eligibility)(role, NULL,group_id)==-1) continue;
        void *group=lookup(g_profile->skill_groups,group_id);
        if (!Memory_Readable(group,0x2E)) continue;
        unsigned members=Read32(group,0x26);
        void *array=ReadPtr(group,0x2A);
        if (!members || members>1024 || !Memory_Readable(array,members*4)) continue;
        unsigned index=0;
        /* 原序列前缀算法：历史长度不少于组长度时从首项重新开始；否则逐项匹配组引用。 */
        if (history->count && history->count<members) {
            while (index<history->count) {
                void *prior=lookup(g_profile->methods,(int)Read32(array,index*4));
                if (!Memory_Readable(prior,0x32) || word(prior,0x27)!=history->selectors[index]) break;
                ++index;
            }
        }
        unsigned matched=index;
        void *method=NULL;
        for (;index<members;++index) {
            method=lookup(g_profile->methods,(int)Read32(array,index*4));
            if (!Memory_Readable(method,0x32)) { method=NULL;break; }
            /* 续接成员的时间条件为非正时原版继续找下一成员；首成员不跳过。 */
            if (!index || (int)Read32(method,0x29)>0) break;
            method=NULL;
        }
        if (!method || (selection>=0 && word(method,0x27)!=(unsigned)selection)) continue;
        /* 只保护已经完整走过的同一条前缀；同名、同选择码的其它实际动作不能冒充本阶段。 */
        bool continuation=history->count && matched==history->count && matched>0 && index>0 &&
            (!history->last_method || (int)Read32(array,(matched-1)*4)==history->last_method);
        if(selection==-1 && !continuation)continue;
        if (!history->count) {winner=method;winner_index=index;}
        else {
            SkillStatus state=condition(role,method,point,history,continuation);
            if(state==SKILL_READY && (int)matched>best) {
                best=(int)matched;winner=method;winner_continuation=continuation;winner_index=index;
            } else if(continuation && state!=SKILL_READY && (int)matched>blocked_best) {
                blocked_best=(int)matched;blocked=state;blocked_method=method;
            }
        }
    }
    if (!winner) {
        if(blocked_method) {
            out->method=word(blocked_method,0x24);out->selector=word(blocked_method,0x27);
            out->continuation=true;out->from_start=(int)Read32(blocked_method,0x29)>=1000;
        }
        return blocked;
    }
    out->method=word(winner,0x24);
    out->selector=word(winner,0x27);
    out->restart=winner_index==0;
    out->continuation=winner_continuation;
    out->from_start=(int)Read32(winner,0x29)>=1000;
    return SKILL_READY;
}

SkillStatus Skill_Query(void *role,int selection,const WorldPoint *point,const ActionHistory *history,ResolvedSkill *out)
{
    if(selection<0)return SKILL_MISSING;
    return query(role,selection,point,history,out);
}
SkillStatus Skill_Continuation(void *role,const ActionHistory *history,ResolvedSkill *out)
{
    if(!role || !history || !history->count)return SKILL_MISSING;
    WorldPoint point={(int)Read32(role,0x2C),(int)Read32(role,0x30)};
    return query(role,-1,&point,history,out);
}
bool Skill_Resolve(void *role,int selection,const WorldPoint *point,const ActionHistory *history,ResolvedSkill *out)
{
    return Skill_Query(role,selection,point,history,out)==SKILL_READY;
}
