/* 验证共用断言的单次求值、计数、常量条件及两种失败退出方式。
 * 所有数据只属于测试进程，不加载游戏或修改发行目录。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "TestAssert.h"

static int checks, evaluated, failed;

/* 返回失败的模拟检查仍由调用者决定返回值。 */
static int return_failure(void)
{
    TEST_CHECK(checks, 0, return 7);
    return 0;
}

int main(int argc, char **argv)
{
    /* 子进程模式验证exit路径，CTest检查指定返回值而非仅任意非零。 */
    if (argc > 1 && !strcmp(argv[1], "exit")) {
        TEST_FAIL(checks, fprintf(stderr,"断言失败退出回放\n"); exit(9));
    }
    _Static_assert(sizeof(char)==1,"字符应占一个字节");
    TEST_CHECK(checks, 6==6, return 1);
    TEST_CHECK(checks, ++evaluated==1, return 2);
    TEST_CHECK(checks, 0, ++failed);
    if (return_failure()!=7 || checks!=4 || evaluated!=1 || failed!=1) return 3;
    /* 条件失败时只执行一次失败动作，也必须计入检查数。 */
    TEST_FAIL(checks, ++failed);
    if (checks!=5 || failed!=2) return 4;
    puts("统一断言常量、求值、计数和失败返回回归通过。");
    return 0;
}
