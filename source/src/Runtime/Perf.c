#include "RuntimeText.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "Perf.h"
#include "Log.h"
static int64_t frequency,report_at,last_frame,interval_total,interval_max;
static unsigned owner,frames,interval_count;
static struct {int64_t total,maximum;unsigned count;} samples[PERF_COUNT];
static const char *labels[PERF_COUNT]={RuntimeText_Perf_NativeInputStage,RuntimeText_Perf_SdlSamplingStage,RuntimeText_Perf_TargetTraversalStage,RuntimeText_Perf_ControllerStage,RuntimeText_Perf_HitWrapperStage,RuntimeText_Perf_NativeHitStage,RuntimeText_Perf_AutoPickupStage,RuntimeText_Perf_InteractionProbeStage};
void RuntimePerf_Initialize(void)
{
    if (frequency) return;
    LARGE_INTEGER f,now;
    if (!QueryPerformanceFrequency(&f) || !QueryPerformanceCounter(&now)) return;
    frequency=f.QuadPart;report_at=now.QuadPart;owner=GetCurrentThreadId();
}
int64_t RuntimePerf_Begin(void)
{
    if (!frequency || GetCurrentThreadId()!=owner) return 0;
    LARGE_INTEGER now;if(!QueryPerformanceCounter(&now))return 0;return now.QuadPart;
}
void RuntimePerf_End(RuntimePerfId id,int64_t begin)
{
    if (!begin || (unsigned)id>=PERF_COUNT || GetCurrentThreadId()!=owner) return;
    int64_t end=RuntimePerf_Begin(),elapsed=end-begin;
    if (elapsed<0) return;
    samples[id].total+=elapsed;++samples[id].count;
    if (elapsed>samples[id].maximum) samples[id].maximum=elapsed;
}
void RuntimePerf_FrameEnd(void)
{
    int64_t now=RuntimePerf_Begin();if(!now)return;
    ++frames;
    /* 输入间隔包含原游戏逻辑、绘制和等待，不把它误标成纯插件耗时或精确FPS。 */
    if (last_frame && now-last_frame<=frequency) {
        int64_t interval=now-last_frame;interval_total+=interval;++interval_count;
        if(interval>interval_max)interval_max=interval;
    }
    last_frame=now;
    if (now-report_at<frequency*5) return;
    double elapsed=(double)(now-report_at)/(double)frequency;
    RuntimeLog_Write(RuntimeText_Perf_InputIntervalSummaryLog,
        elapsed,frames,interval_count ? (double)interval_total*1000.0/(double)frequency/interval_count:0.0,
        (double)interval_max*1000.0/(double)frequency);
    for (unsigned i=0;i<PERF_COUNT;++i) if(samples[i].count)
        RuntimeLog_Write(RuntimeText_Perf_StageTimingSummaryLog,
            labels[i],samples[i].count,(double)samples[i].total*1000.0/frequency/samples[i].count,
            (double)samples[i].maximum*1000.0/frequency,(double)samples[i].total*1000.0/frequency);
    RuntimeLogStats log;RuntimeLog_GetStats(&log);
    RuntimeLog_Write(RuntimeText_Perf_LogQueueSummaryLog,
        log.file_opens,log.file_writes,log.queued_bytes,log.dropped_messages);
    memset(samples,0,sizeof samples);frames=interval_count=0;interval_total=interval_max=0;report_at=now;
}
