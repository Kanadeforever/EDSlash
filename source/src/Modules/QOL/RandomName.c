#include "RandomName.h"
#include "RandomNameData.h"
#include <string.h>

/* 所有词条由编译期数组持有，无文件读取、堆分配或外部词库依赖。 */
#define COUNT(a) ((unsigned)(sizeof(a) / sizeof((a)[0])))
static uint32_t next_value(RandomNameSession *session)
{
    /* 非零状态的 xorshift32 适合姓名抽样；种子由接入方提供，不用于安全用途。 */
    uint32_t value = session->state;
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    session->state = value;
    return value;
}
static unsigned choose(RandomNameSession *session, unsigned count)
{
    /* 拒绝不能等分的末端区间，避免直接取余造成偏差。
     * 生成器输出范围为 1..UINT32_MAX，因此先减一再比较。 */
    uint32_t limit = UINT32_MAX - UINT32_MAX % count, value;
    do { value = next_value(session) - 1u; } while (value >= limit);
    return value % count;
}
static unsigned characters(const char *text)
{
    unsigned count = 0;
    /* 内置数据已经验证为 UTF-8；非续接字节代表一个字符的开始。 */
    for (; *text; ++text) if (((unsigned char)*text & 0xc0u) != 0x80u) ++count;
    return count;
}
void RandomName_Begin(RandomNameSession *session, uint32_t seed)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->state = seed ? seed : UINT32_C(0x9e3779b9);
    session->active = 1;
}
void RandomName_End(RandomNameSession *session)
{
    /* 清除整个会话，确保退出后不会继续生成，也不保留旧创建流程历史。 */
    if (session) memset(session, 0, sizeof(*session));
}
int RandomName_Generate(RandomNameSession *session, RandomNameGender gender,
    unsigned max_characters, const char *current, RandomNameAccept accept,
    void *user, char *out, unsigned capacity)
{
    if (!session || !session->active || !session->state || !accept || !out ||
        capacity < 1 || max_characters < 2 || gender < RANDOM_NAME_NEUTRAL ||
        gender > RANDOM_NAME_FEMALE) return 0;
    /* 拒绝回调可能屏蔽全部字库；有限次尝试保证界面不会被无限循环阻塞。 */
    for (unsigned attempt = 0; attempt < 256; ++attempt) {
        const char *surname, *name;
        char candidate[RANDOM_NAME_CAPACITY];
        unsigned compound = choose(session, 100) < 12;
        surname = compound ? random_compound[choose(session, COUNT(random_compound))] :
            random_surnames[choose(session, COUNT(random_surnames))];
        /* 中性词条始终参与；已知性别时约七成选择对应池。 */
        const char *const *pool = random_neutral;
        unsigned count = COUNT(random_neutral);
        if (gender != RANDOM_NAME_NEUTRAL && choose(session, 100) < 70) {
            if (gender == RANDOM_NAME_MALE) { pool = random_male; count = COUNT(random_male); }
            else { pool = random_female; count = COUNT(random_female); }
        }
        name = pool[choose(session, count)];
        /* 中文词条首字均为三字节：避免云云岫等姓与名首字重复的结果。 */
        if (memcmp(surname, name, 3) == 0 ||
            characters(surname) + characters(name) > max_characters) continue;
        size_t a = strlen(surname), b = strlen(name);
        if (a + b + 1 > sizeof(candidate) || a + b + 1 > capacity) continue;
        memcpy(candidate, surname, a); memcpy(candidate + a, name, b + 1);
        if (current && strcmp(current, candidate) == 0) continue;
        unsigned repeated = 0;
        for (unsigned i = 0; i < RANDOM_NAME_RECENT; ++i)
            if (strcmp(session->recent[i], candidate) == 0) repeated = 1;
        if (repeated || !accept(candidate, user)) continue;
        /* 只在候选通过全部约束后更新输出与历史；失败不会提交半个姓名。 */
        memcpy(out, candidate, a + b + 1);
        memcpy(session->recent[session->next_recent], candidate, a + b + 1);
        session->next_recent = (session->next_recent + 1) % RANDOM_NAME_RECENT;
        return 1;
    }
    return 0;
}
