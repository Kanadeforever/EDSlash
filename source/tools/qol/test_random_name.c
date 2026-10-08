#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/Modules/QOL/RandomName.c"
/* 直接运行生产生成器，覆盖会话、过滤、长度和历史；不访问游戏进程。 */
static unsigned checks;
#define CHECK(e) do { ++checks; if (!(e)) { fprintf(stderr,"随机姓名检查失败：行%d %s\n",__LINE__,#e); exit(1); } } while (0)
static int allow(const char *name, void *user)
{
    /* 候选均是中文汉字；每字三个 UTF-8 字节，输出应保持完整终止。 */
    (void)user;
    CHECK(strlen(name) % 3 == 0);
    return 1;
}
static int reject(const char *name, void *user)
{
    (void)name;
    ++*(unsigned *)user;
    return 0;
}
static int restricted(const char *name, void *user)
{
    (void)user;
    /* 模拟接入层只容许短姓名的原生容量检查。 */
    return strlen(name) == 6;
}
static int member(const char *name, const char *const *pool, unsigned count)
{
    for (unsigned i=0;i<count;++i) if (!strcmp(name,pool[i])) return 1;
    return 0;
}
static void check_category(const char *full, RandomNameGender gender)
{
    /* 从实际姓氏词库解析输出，验证不会泄漏另一性别的专属名字。 */
    for (unsigned compound=0;compound<2;++compound) {
        const char *const *surnames=compound?random_compound:random_surnames;
        unsigned count=compound?COUNT(random_compound):COUNT(random_surnames);
        for(unsigned i=0;i<count;++i) {
            size_t length=strlen(surnames[i]);
            if(strncmp(full,surnames[i],length)) continue;
            const char *name=full+length;
            if(member(name,random_neutral,COUNT(random_neutral)) ||
               (gender==RANDOM_NAME_MALE && member(name,random_male,COUNT(random_male))) ||
               (gender==RANDOM_NAME_FEMALE && member(name,random_female,COUNT(random_female)))) return;
        }
    }
    CHECK(0);
}
int main(void)
{
    RandomNameSession session={0}, replay;
    char output[32]="保持原值", previous[32]; unsigned refused=0;
    CHECK(!RandomName_Generate(&session,0,4,NULL,allow,NULL,output,sizeof output));
    CHECK(!strcmp(output,"保持原值"));
    RandomName_Begin(&session,0);CHECK(session.active && session.state);
    CHECK(!RandomName_Generate(&session,0,1,NULL,allow,NULL,output,sizeof output));
    CHECK(!RandomName_Generate(&session,0,4,NULL,NULL,NULL,output,sizeof output));
    CHECK(!RandomName_Generate(&session,3,4,NULL,allow,NULL,output,sizeof output));
    CHECK(!RandomName_Generate(&session,0,4,NULL,allow,NULL,output,6));
    CHECK(!RandomName_Generate(&session,0,4,NULL,reject,&refused,output,sizeof output));
    CHECK(refused>0 && refused<=256 && !strcmp(output,"保持原值"));
    for(unsigned gender=0;gender<3;++gender) {
        RandomName_Begin(&session,1234+gender);RandomName_Begin(&replay,1234+gender);
        output[0]=0;
        for(unsigned i=0;i<5000;++i) {
            char expected[32];strcpy(previous,output);
            CHECK(RandomName_Generate(&session,(RandomNameGender)gender,4,previous,allow,NULL,output,sizeof output));
            CHECK(RandomName_Generate(&replay,(RandomNameGender)gender,4,previous,allow,NULL,expected,sizeof expected));
            CHECK(!strcmp(output,expected) && strcmp(output,previous));
            CHECK(strlen(output)>=6 && strlen(output)<=12);
            check_category(output,(RandomNameGender)gender);
            /* 当前结果只应在最新一格出现，不能与其余七格重复。 */
            unsigned latest=(session.next_recent+RANDOM_NAME_RECENT-1)%RANDOM_NAME_RECENT;
            for(unsigned j=0;j<RANDOM_NAME_RECENT;++j)
                if(j!=latest) CHECK(strcmp(output,session.recent[j]));
        }
        for(unsigned i=0;i<100;++i) {
            CHECK(RandomName_Generate(&session,(RandomNameGender)gender,2,NULL,restricted,NULL,output,7));
            CHECK(strlen(output)==6);
        }
    }
    RandomName_End(&session);CHECK(!session.active && !session.state && !session.recent[0][0]);
    CHECK(!RandomName_Generate(&session,0,4,NULL,allow,NULL,output,sizeof output));
    puts("随机姓名生产核心离线回归通过。");printf("检查数：%u\n",checks);
    return 0;
}
