#ifndef EDSLASH_LOG_H
#define EDSLASH_LOG_H
#include <stdarg.h>
typedef struct {unsigned long queued_bytes,written_bytes,dropped_messages,file_opens,file_writes;} RuntimeLogStats;
int RuntimeLog_Initialize(void *module);
/* Start只能在Loader锁外调用；初始日志先入队，首个游戏输入帧启动后台写盘。 */
int RuntimeLog_Start(void);
void RuntimeLog_Text(const char *text);
void RuntimeLog_Line(const char *text);
void RuntimeLog_Write(const char *format,...);
void RuntimeLog_VWrite(const char *format,va_list args);
void RuntimeLog_GetStats(RuntimeLogStats *stats);
/* 用于切图收尾或测试，普通帧不等待磁盘；Shutdown也只能在Loader锁外调用。 */
int RuntimeLog_Flush(unsigned long timeout_ms);
void RuntimeLog_Shutdown(void);
#endif
