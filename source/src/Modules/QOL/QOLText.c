/* QOL 模块集中式文本。
 * 变量名按功能和用途命名；修改字符串内容后重新编译。
 * Label是名称，Description/Help是说明，Log是日志，Error/Reason是错误或原因。
 * Button是按钮文字，Value是显示值，Format/Template是保留占位符的文本。
 * 保留格式占位符及其顺序，避免与调用参数不匹配。
 * 本文件编译进 ASI，发行时不需要外置文本文件。 */
#include "QOLText.h"

/* AutoPickup：对应源文件使用的显示文字与诊断信息。 */
const char QOLText_AutoPickup_WaitingForUniqueTargetLog[] = "[QoL][等待] 尚未定位唯一拾取对象，未调用动作；进入场景后继续检查。";
const char QOLText_AutoPickup_NativeScanStartedLog[] = "[QoL][运行] 已定位拾取对象，开始调用原版动作22扫描；是否入包由原版判断。";
/* PickupNotice：对应源文件使用的显示文字与诊断信息。 */
const char QOLText_PickupNotice_AddedQuantityLog[] = "[QoL][拾取提示] 实际新增数量=";
const char QOLText_PickupNotice_DrawingReadyLog[] = "[QoL] 成功拾取提示已接入原版字体和公共UI绘制事件。";
/* QOLModule：对应源文件使用的显示文字与诊断信息。 */
const char QOLText_Startup_SharedInputUnavailableLog[] = "[QoL][停止] 公共输入帧或Win32桥不可用，未安装拾取Hook。";
const char QOLText_Startup_ModuleInitializingLog[] = "[QoL] 统一模块：初始化地面名称、自动拾取与成功提示。";
const char QOLText_Startup_NativeSignatureMismatchLog[] = "[QoL][停止] 原生入口签名不匹配，未安装模块。";
const char QOLText_Startup_ConfigPathUnavailableLog[] = "[QoL][停止] 无法定位同目录配置文件。";
const char QOLText_Config_ItemNamesAlwaysVisibleLog[] = "[QoL] 地面名称常显=";
const char QOLText_Config_AutoPickupModeLog[] = "[QoL] 自动拾取模式=";
const char QOLText_Config_ScanIntervalLog[] = "[QoL] 扫描间隔毫秒=";
const char QOLText_Config_DropWaitLog[] = "[QoL] 掉落等待毫秒=";
const char QOLText_Startup_PickupNoticeValidationFailedLog[] = "[QoL][停止] 拾取提示接口或公共事件订阅验证失败。";
const char QOLText_Startup_GroundItemHookFailedLog[] = "[QoL][停止] 地面物品入口安装失败，开始回滚。";
const char QOLText_Startup_AutoPickupHookFailedLog[] = "[QoL][停止] 自动拾取入口安装失败，开始回滚。";
const char QOLText_Startup_RandomNameInterfaceUnavailableLog[] = "[QoL][随机名称] 原名称接口未通过校验，保留原手工输入。";
const char QOLText_Startup_HooksReadyLog[] = "[QoL][成功] 地面名称与自动拾取入口已就绪；实际运行仍需场景验证。";
/* RandomNameUI：对应源文件使用的显示文字与诊断信息。 */
const char QOLText_RandomName_PageValidationRejectedLog[] = "[随机名称][拒绝] 原因=%u 页面=%08lX；1未就绪/2页/3模组窗口/4模态/5名称类/6父页/7隐藏/8句柄/9线程。";
const char QOLText_RandomName_BirthdayControlsRejectedLog[] = "[随机名称][拒绝] 外传生日控件身份不符，保留原姓名和生日。";
const char QOLText_RandomName_NonGbkEncodingRejectedLog[] = "[随机名称] 当前名称编辑编码不是GBK，未覆盖原文本。";
const char QOLText_RandomName_EditCapacityRejectedLog[] = "[随机名称][拒绝] 编辑容量=%ld，不足两个汉字。";
const char QOLText_RandomName_CandidateEncodingRejectedLog[] = "[随机名称][拒绝] 候选未通过GBK无损编码或名称字节容量检查。";
const char QOLText_RandomName_NameAndBirthdayFilledLog[] = "[随机名称] 已填入名称和生日%u月%u日，等待玩家确认创建。";
const char QOLText_RandomName_NameFilledLog[] = "[随机名称] 已填入名称，等待玩家确认创建。";
