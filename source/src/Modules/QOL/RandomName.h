#ifndef EDSLASH_RANDOM_NAME_H
#define EDSLASH_RANDOM_NAME_H
#include <stdint.h>

/* 核心仅生成 UTF-8 姓名，不读取游戏、轮询按键或修改控件。
 * 接入方须在游戏线程调用；此结构不是跨线程请求队列。 */
#define RANDOM_NAME_CAPACITY 32u
#define RANDOM_NAME_RECENT 8u
typedef enum RandomNameGender {
    RANDOM_NAME_NEUTRAL = 0,
    RANDOM_NAME_MALE = 1,
    RANDOM_NAME_FEMALE = 2
} RandomNameGender;
typedef struct RandomNameSession {
    uint32_t state;
    unsigned active, next_recent;
    char recent[RANDOM_NAME_RECENT][RANDOM_NAME_CAPACITY];
} RandomNameSession;

/* 接入方检查原生编码、输入框字节容量及字库后返回非零。
 * 回调在游戏写入前筛选候选，不应在此保存窗口或修改文本。 */
typedef int (*RandomNameAccept)(const char *utf8, void *user);
/* 每次进入名称窗口重新开始；退出、创建成功或取消时结束并清空历史。 */
void RandomName_Begin(RandomNameSession *session, uint32_t seed);
void RandomName_End(RandomNameSession *session);
/* max_characters 按汉字计数；current 为当前 UTF-8 文本，可为空。
 * 必须提供 accept。成功才写 out，失败保持 out 原值；不自动确认创建。
 * 无法确认角色类别时使用 NEUTRAL，不猜测游戏的角色编号。 */
int RandomName_Generate(RandomNameSession *session, RandomNameGender gender,
    unsigned max_characters, const char *current, RandomNameAccept accept,
    void *user, char *out, unsigned capacity);
#endif
