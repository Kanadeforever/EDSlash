/* QOL 模块文本声明；具体文字统一在配套 .c 文件编辑。
 * 名称直接说明功能与用途；Label/Description区分设置名称与说明，Log/Error区分日志与错误。
 * 使用常量数组，使静态配置表可以直接保存字符串地址。 */
#ifndef EDSLASH_QOL_TEXT_H
#define EDSLASH_QOL_TEXT_H
#include <wchar.h>

extern const char QOLText_AutoPickup_WaitingForUniqueTargetLog[];
extern const char QOLText_AutoPickup_NativeScanStartedLog[];
extern const char QOLText_PickupNotice_AddedQuantityLog[];
extern const char QOLText_PickupNotice_DrawingReadyLog[];
extern const char QOLText_Startup_SharedInputUnavailableLog[];
extern const char QOLText_Startup_ModuleInitializingLog[];
extern const char QOLText_Startup_NativeSignatureMismatchLog[];
extern const char QOLText_Startup_ConfigPathUnavailableLog[];
extern const char QOLText_Config_ItemNamesAlwaysVisibleLog[];
extern const char QOLText_Config_AutoPickupModeLog[];
extern const char QOLText_Config_ScanIntervalLog[];
extern const char QOLText_Config_DropWaitLog[];
extern const char QOLText_Startup_PickupNoticeValidationFailedLog[];
extern const char QOLText_Startup_GroundItemHookFailedLog[];
extern const char QOLText_Startup_AutoPickupHookFailedLog[];
extern const char QOLText_Startup_RandomNameInterfaceUnavailableLog[];
extern const char QOLText_Startup_HooksReadyLog[];
extern const char QOLText_RandomName_PageValidationRejectedLog[];
extern const char QOLText_RandomName_BirthdayControlsRejectedLog[];
extern const char QOLText_RandomName_NonGbkEncodingRejectedLog[];
extern const char QOLText_RandomName_EditCapacityRejectedLog[];
extern const char QOLText_RandomName_CandidateEncodingRejectedLog[];
extern const char QOLText_RandomName_NameAndBirthdayFilledLog[];
extern const char QOLText_RandomName_NameFilledLog[];

#endif
