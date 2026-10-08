#ifndef EDSLASH_LOG_H
#define EDSLASH_LOG_H
#include <stdarg.h>
#include <stddef.h>
/* A版系统接口返回系统代码页字符串；必须在拼接中文UTF-8日志前转换。
 * 成功追加完整片段，容量不足/转换失败不写半个字符。 */
int RuntimeLog_AppendAnsi(char *output,size_t capacity,const char *text);
typedef struct {unsigned long queued_bytes,written_bytes,dropped_messages,file_opens,file_writes;} RuntimeLogStats;
/* 总开关在保存后的帧边界生效；关闭不删除旧文件，关闭启动不创建日志文件。 */
void RuntimeLog_SetEnabled(int enabled);
int RuntimeLog_Enabled(void);
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
