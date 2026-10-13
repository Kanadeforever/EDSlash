/* 测试共用断言：只计算一次条件，保留调用者的计数、诊断和失败退出方式。
 * 编译期结构约束使用_Static_assert；确定失败的路径使用TEST_FAIL。
 * 不关闭C4127，生产代码中的常量条件仍由编译器检查。 */
#ifndef EDSLASH_TEST_ASSERT_H
#define EDSLASH_TEST_ASSERT_H

/* 把布尔结果作为函数参数接收，断言宏的if不直接比较编译期常量。
 * 内联只影响生成代码，不影响MSVC对源码条件表达式的诊断。 */
static inline int TestCondition(int value)
{
    return value != 0;
}

/* counter是调用者的检查数；失败语句可打印专用信息并return或exit。
 * 条件只出现在一次函数调用中，因此自增、函数调用等副作用不会重复。 */
#define TEST_CHECK(counter, expression, ...) do { \
    ++(counter); \
    if (!TestCondition(!!(expression))) { __VA_ARGS__; } \
} while (0)

/* 不制造恒假条件，直接执行调用者提供的失败报告和退出语句。 */
#define TEST_FAIL(counter, ...) do { ++(counter); __VA_ARGS__; } while (0)

#endif
