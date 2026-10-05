#ifndef EDSLASH_PERF_H
#define EDSLASH_PERF_H
#include <stdint.h>
typedef enum { PERF_INPUT,PERF_SDL,PERF_TARGET,PERF_GAME,PERF_HIT,PERF_NATIVE_HIT,PERF_PICKUP,PERF_INSPECT,PERF_COUNT } RuntimePerfId;
/* 只在游戏线程计时；固定计数器每5秒输出一次，不逐对象写性能日志。 */
void RuntimePerf_Initialize(void);
int64_t RuntimePerf_Begin(void);
void RuntimePerf_End(RuntimePerfId id,int64_t begin);
void RuntimePerf_FrameEnd(void);
#endif
