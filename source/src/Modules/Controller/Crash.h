#ifndef EDSLASH_CONTROLLER_CRASH_H
#define EDSLASH_CONTROLLER_CRASH_H
/* 仅记录异常地址与寄存器，不吞异常、不恢复游戏、不附加调试器。 */
int Crash_Initialize(void);
void Crash_Shutdown(void);
void Crash_Stage(const char *stage);
#endif
