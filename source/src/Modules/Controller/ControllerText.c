/* Controller 模块集中式文本。
 * 变量名按功能和用途命名；修改字符串内容后重新编译。
 * Label是名称，Description/Help是说明，Log是日志，Error/Reason是错误或原因。
 * Button是按钮文字，Value是显示值，Format/Template是保留占位符的文本。
 * 保留格式占位符及其顺序，避免与调用参数不匹配。
 * 本文件编译进 ASI，发行时不需要外置文本文件。 */
#include "ControllerText.h"

/* ActionMenu：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_ActionMenu_OpenedLog[] = "[动作菜单] 原右手菜单展开；长期槽位保留，松任一扳机确认当前侧。";
const char ControllerText_ActionMenu_SelectionConfirmedLog[] = "[动作菜单] 原%s手选择=%d已确认。";
const char ControllerText_LeftSideLabel[] = "左";
const char ControllerText_RightSideLabel[] = "右";
const char ControllerText_ActionMenu_FocusSideChangedLog[] = "[动作菜单] 聚焦改为%s手，另侧长期选择未改。";

/* Combat：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Combat_ComboGroupChangedLog[] = "[连招] 切换独立套组=%u，下一次 Y 从首项开始。";
const char ControllerText_Combat_UnmatchedNotificationLog[] = "[战斗通知] 未匹配到本次手柄提交，组=%d；不推进手柄历史或连招游标。";
const char ControllerText_Combat_NativeActionCreatedLog[] = "[战斗执行] 原生动作已建立，组=%d 方向=%d 来源=%s 历史=%u。";
const char ControllerText_RightHandLabel[] = "右手";
const char ControllerText_LeftHandLabel[] = "左手";
const char ControllerText_Combat_HandoffToMouseLog[] = "[输入交接] 手柄到物理鼠标，成功历史=%u；保留套组进度。";
const char ControllerText_Combat_MouseHistoryOverflowLog[] = "[输入交接] 鼠标历史超出上限，按新序列接管。";
const char ControllerText_Combat_HandoffToControllerLog[] = "[输入交接] 物理鼠标到手柄，成功历史=%u；保留当前目标。";
const char ControllerText_Combat_NewPressLog[] = "[战斗输入] 来源=%s 选择=%d 新按=1 忙碌=%d 历史=%u 已结束=%d。";

const char ControllerText_Combat_BufferExpiredWhileBusyLog[] = "[战斗丢弃] 缓冲期间持续忙碌，选择=%d。";
const char ControllerText_Combat_ClearEndedHistoryAndRetryLog[] = "[战斗恢复] 旧序列不匹配选择=%d，清理已结束历史 %u 项后重试。";
const char ControllerText_Combat_UnresolvedActionDiscardedLog[] = "[战斗丢弃] 招式仍不可解析，选择=%d。";
const char ControllerText_Combat_ActionSubmittedLog[] = "[战斗提交] 来源=%s 选择=%d 招式=%d 事件=%d 参数=%d,%d 通知=%d。";

/* ControllerModule：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_InputSource_MouseTakeoverLog[] = "[输入来源] 物理鼠标接管；恢复原版鼠标解析、重试和动作历史。";
const char ControllerText_Patch_WriteFinalizationFailedLog[] = "[补丁][警告] 字节已写入，但缓存／保护处理失败，保留相关资源。";
const char ControllerText_Window_FocusLossHookFailedLog[] = "[窗口] 未安装失焦清理入口，错误码=%lu。";
const char ControllerText_InputSource_ControllerTakeoverLog[] = "[输入来源] 新的手柄操作接管；恢复独立手柄动作解析。";
const char ControllerText_InputMode_ChangedLog[] = "[模式] 已切换为%s，震动 %u 毫秒。";
const char ControllerText_InputMode_MouseLabel[] = "鼠标模式";
const char ControllerText_InputMode_ControllerLabel[] = "手柄模式";
const char ControllerText_Startup_ModuleInitializingLog[] = "[Controller] 统一模块启动，战斗复用原生动作协议，菜单使用独立焦点与原生入口。";
const char ControllerText_Startup_ProfileMismatchLog[] = "[停止] Controller基线或机器码不匹配，撤回采样入口，其他模块继续。";
const char ControllerText_Startup_SdlInitializationFailedLog[] = "[停止] SDL初始化失败，撤回采样入口，其他模块继续。";
const char ControllerText_Startup_ActionStageInstallFailedLog[] = "[停止] 无法安装动作阶段，已撤回采样入口。";
const char ControllerText_Startup_GameplayHooksInstallFailedLog[] = "[停止] 防御/闪避/菜单入口未通过安装，已撤回手柄动作阶段。";
const char ControllerText_Startup_BusinessReadyLog[] = "[启动] %s；SDL、输入采样和原版目标解析后阶段已就绪。";
const char ControllerText_Startup_LegacyAsiDetectedLog[] = "[Controller][停止] 检测到旧独立ASI，请停用它后使用统一插件。";
const char ControllerText_Startup_SamplingBridgeInstalledLog[] = "[Controller] 采样桥已安装，完整初始化延后到游戏输入线程。";
/* Crash：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Crash_NormalInputStage[] = "手柄普通运行";
const char ControllerText_Crash_UnknownStage[] = "未知阶段";
const char ControllerText_Crash_ReportFormat[] = "异常记录 %04u-%02u-%02u %02u:%02u:%02u\r\n最近手柄阶段=%s\r\n异常=%08lX 地址=%08lX 模块=%s 偏移=%08lX\r\n" "访问类型=%lu 故障地址=%08lX 线程=%lu 输入层=%d 按键=%08lX\r\n" "EIP=%08lX ESP=%08lX EBP=%08lX EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX\r\n" "这是首次异常记录，不吞异常；最终退出由游戏/系统原处理链决定。\r\n\r\n";
const wchar_t ControllerText_Crash_ReportFileName[] = L"EDSlash崩溃记录.txt";
/* Cursor：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Cursor_CrashRecorderUnavailableLog[] = "[诊断] 未能注册异常地址记录，普通输入仍可用。";
const char ControllerText_Cursor_FocusFeedbackReadyLog[] = "[光标] 格子/物品确认/动作菜单由Runtime动态框提示，持有图标居中；其它页保留原反馈。";
/* Feedback：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Finisher_SlotIneligibleLog[] = "[必杀技] 槽%u目前不符合原版资格。";
const char ControllerText_Finisher_ReleaseRequestedLog[] = "[必杀技] 槽%u第二次确认，请求释放组=%d。";
const char ControllerText_Finisher_PreparedLog[] = "[必杀技] 槽%u准备组=%d，沿用原版有效窗口。";
const char ControllerText_Finisher_SlotUnconfiguredLog[] = "[必杀技] 角色未配置第%u个必杀组。";
/* Game：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Game_WorldAndPlayerReadyLog[] = "[控制就绪] 世界与玩家恢复有效；观察到世界/角色未就绪=%lu毫秒（可能含前端停留，不等同读档耗时），当前界面门=%u。";
const char ControllerText_Game_InputChainStateLog[] = "[输入链] 层=%d 门=%u 界面=%02X 对象=%08lx 玩家=%08lx 鼠标玩家=%08lx 句柄=%08lx 世界58=%08lx 原生帧=%u 按键=%04lx 有效持键=%04lx 新按=%04lx 摇杆=%.2f,%.2f 状态=%lu 活动动作=%08lx。";
const char ControllerText_Game_RecoveryItemShortcutLog[] = "[药品快捷] 原版槽位 %d。";
const char ControllerText_Game_ThrowSlotInvalidLog[] = "[投掷快捷] 槽 %d 没有有效物品。";
const char ControllerText_Game_ThrowSlotEmptyLog[] = "[投掷快捷] 槽 %d 已空或物品用尽。";
const char ControllerText_Game_ThrowActionRequestedLog[] = "[投掷快捷] 槽 %d 直接请求选择=%d。";
const char ControllerText_Game_SkillSlotRequestedLog[] = "[技能] 槽%u请求选择=%d。";
const char ControllerText_Game_SkillSlotUnboundLog[] = "[技能] 槽%u尚无有效绑定。";
const char ControllerText_Aim_ActiveActionRejectedLog[] = "[技能预览拒绝] 当前有原活动动作，等待新的B输入。";
const char ControllerText_Aim_FirstNativeSkillUnavailableLog[] = "[技能预览拒绝] 原第一技能快捷绑定未就绪。";
const char ControllerText_Aim_BaseSkillIneligibleLog[] = "[技能预览拒绝] 原基础选择=%d，不存在或快捷技能解析/资格拒绝。";
const char ControllerText_Aim_MethodUnreadableLog[] = "[技能预览拒绝] 实际Method=%d记录不可读。";
const char ControllerText_Aim_InvalidJumpDistanceLog[] = "[跳跃拒绝] 技能组=%d Method=%d 原距离档=%d无效。";
const char ControllerText_Aim_JumpPreviewStartedLog[] = "[跳跃] 开始预览技能组=%d Method=%d 原距离档=%d 最大距离=%d 扩散耗时=%lu毫秒；不改左右手槽位。";
const char ControllerText_Aim_LandingRequestedLog[] = "[技能落点] 松B请求选择=%d 落点=%d,%d 按住=%u毫秒；复用快捷技能路径。";
const char ControllerText_Movement_DeadzoneAndLeadLog[] = "[移动配置] 左摇杆圆形死区，走跑共用前探=%d 格。";
const char ControllerText_Movement_TerrainBlockedLog[] = "[移动受阻] 世界=%ld,%ld 请求=%d,%d；前方原地形不通行，本次不提交绕路目标。";
const char ControllerText_Movement_TargetSubmittedLog[] = "[移动] 世界=%ld,%ld 请求=%d,%d 地图目标=%d,%d 走跑=%d 提交后状态=%lu。";
/* Guard：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Guard_HitStaminaChargedLog[] = "[防御受击] 扣减体力=%.2f，体力=%.2f；原版防御状态=%u。";
const char ControllerText_Guard_AttackHitStaminaRecoveredLog[] = "[攻击命中] 敌人生命 %.2f→%.2f，恢复体力=%.2f。";
const char ControllerText_Guard_HitStateDiagnosticsLog[] = "[防御诊断] 生命=%.2f→%.2f 体力=%.2f→%.2f 防御=%u→%u 状态=%u→%u 朝向=%u 受击反应=%u 攻击位置=%d,%d 玩家位置=%d,%d。";
const char ControllerText_Dodge_FinishedLog[] = "[方向闪避] 结束，实际位置=%d,%d，%s。";
const char ControllerText_Dodge_StoppedByCollisionReason[] = "碰撞提前停止";
const char ControllerText_Dodge_ConfiguredDistanceReachedReason[] = "达到配置距离";
const char ControllerText_Dodge_NativeStartSucceededLog[] = "[方向闪避] 原版启动成功，参考点=%d,%d，方向=%d；摇杆回中后重新触发。";
const char ControllerText_Guard_ChargeOnHitConfigLog[] = "[战斗配置] 防御受击扣费=%u；0 时恢复原版周期空耗。";
const char ControllerText_Guard_RunWithoutCostConfigLog[] = "[战斗配置] 奔跑免扣费=%u；不再按战斗状态区分。";
const char ControllerText_Guard_OmnidirectionalConfigLog[] = "[战斗配置] 全方位格挡=%u；只放宽已有防御角度，原5点体力门和特殊穿防保留。";
const char ControllerText_Dodge_DirectionAndDistanceConfigLog[] = "[战斗配置] 方向闪避=%u；距离=%d世界单位（64单位=1格）。";
const char ControllerText_Guard_PercentCostConfigLog[] = "[战斗配置] 防御受击扣减最大体力的%d.%02d%%。";
const char ControllerText_Guard_OriginalCostConfigLog[] = "[战斗配置] 防御受击使用角色原周期扣费。";
const char ControllerText_Guard_PercentRecoveryConfigLog[] = "[战斗配置] 实伤恢复最大体力的%d.%02d%%。";
const char ControllerText_Guard_RecoveryMatchesCostConfigLog[] = "[战斗配置] 实伤恢复跟随防御扣费幅度。";
const char ControllerText_Guard_HooksInstallFailedLog[] = "[防御][停止] 入口安装失败，撤回本模块修改。";
const char ControllerText_Guard_HooksInstalledLog[] = "[防御] 原生 ON/OFF、可配置防御/奔跑扣费及方向闪避入口已安装；原版耗尽规则保留。";
/* Input：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Sdl_InitializationFailedLog[] = "[SDL][停止] 静态SDL输入初始化失败：%s";
const char ControllerText_Sdl_ExternalMappingsMissingLog[] = "[SDL][映射] 未提供外部数据库，继续使用内置映射。";
const char ControllerText_Sdl_ExternalMappingsLoadFailedLog[] = "[SDL][映射] 外部文件加载失败，继续使用内置映射：%s";
const char ControllerText_Sdl_ExternalMappingsLoadedLog[] = "[SDL][映射] 外部文件载入%d条映射。";
const char ControllerText_Sdl_MappingPathConversionFailedLog[] = "[SDL][映射] 路径转换失败，继续使用内置映射。";
const char ControllerText_Sdl_MappingPathIsDirectoryLog[] = "[SDL][映射] 同名路径是目录，继续使用内置映射。";
const char ControllerText_Sdl_InputSubsystemReadyLog[] = "[SDL] 静态SDL %d.%d.%d输入子系统已就绪。";
const char ControllerText_Device_DisconnectedLog[] = "[手柄] 已断开，释放插件拥有的输入。";
const char ControllerText_Device_ConnectedReleaseInputsLog[] = "[手柄] 已连接；请松开摇杆、扳机和按键后开始操作。";
const char ControllerText_Device_RumbleRejectedLog[] = "[震动] 当前设备未接受震动：%s";
/* Inspect：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Inspect_FocusTargetLog[] = "[调查焦点] 句柄=%08lx 原事件=%lu 动态候选=%u 静态格=%u 360度近身，前方优先。";
const char ControllerText_Inspect_NoNearbyTargetLog[] = "[调查] 近身没有有效目标，动态=%u 静态格=%u 地图=%08lx 格=%d,%d。";
const char ControllerText_Inspect_BaseActionUnavailableLog[] = "[调查][拒绝] 原基础动作当前不可用。";
const char ControllerText_Inspect_StaticActionRequestedLog[] = "[调查操作] 静态原基础技能组=%d，目标点=%d,%d。";
const char ControllerText_Inspect_ExitRegionNotEnteredLog[] = "[调查][拒绝] 尚未进入原换区区域，A不提交换区。";
const char ControllerText_Inspect_NativeEventRequestedLog[] = "[调查操作] 原事件=%lu 句柄=%08lx。";
const char ControllerText_Inspect_InterfacesReadyLog[] = "[调查] 动态交互/静态87、88、89、原TransGo出口及360度近身焦点接通，原WorldHover事件已隔离。";
/* Menu：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Menu_HeldItemReturnSlotMissingLog[] = "[物品菜单] 没有可放回的空位，保留持有物品和当前页面。";
const char ControllerText_Menu_ItemPanelFocusChangedLog[] = "[物品菜单] 焦点切换到原面板%02X。";
const char ControllerText_Menu_JournalCategoryChangedLog[] = "[日志菜单] 切换分类=%02X。";
const char ControllerText_Menu_JournalCurrentEntryLog[] = "[日志菜单] 分类=%02lX 当前项=%lu。";
const char ControllerText_Menu_LoadSlotFocusLog[] = "[读档焦点] 页起点=%lu 页内槽=%lu 有效槽=%u。";
const char ControllerText_Menu_DeleteConfirmationOpenedLog[] = "[读档操作] 打开原删除确认，页=%lu 槽=%lu。";
const char ControllerText_Menu_DeleteConfirmationUnavailableLog[] = "[读档][拒绝] 原删除确认对象/文本/焦点未就绪。";
const char ControllerText_Menu_LoadSlotRequestedLog[] = "[读档操作] 请求载入页起点=%lu 槽=%lu。";
const char ControllerText_Menu_RoutingChangedLog[] = "[菜单路由] 页面=%02lX 类型=%d 来源=手柄；等待旧输入释放。";
const char ControllerText_Menu_SkillPageChangedLog[] = "[技能菜单] 切换到%s。";
const char ControllerText_Menu_SkillLearningLabel[] = "技能学习";
const char ControllerText_Menu_ComboEditorLabel[] = "连招编辑";
const char ControllerText_Menu_SkillFocusRegionChangedLog[] = "[技能菜单] 焦点区域=%s。";
const char ControllerText_Menu_ComboListRegionLabel[] = "上方连招清单";
const char ControllerText_Menu_AvailableSkillsRegionLabel[] = "下方可选技能";
const char ControllerText_Menu_EditingComboGroupLog[] = "[技能菜单] 当前编辑套组=%lu。";
const char ControllerText_Menu_ControlFocusedLog[] = "[菜单焦点] 页面=%02lX 控件=%02X。";
const char ControllerText_Menu_ControlConfirmedLog[] = "[菜单操作] 页面=%02lX 控件=%02X 确认。";
const char ControllerText_Menu_DialogueCooldownRejectedLog[] = "[对话] 原显示保护期或外传输入冷却内，本次不提交。";
const char ControllerText_Menu_FocusHooksInstallFailedLog[] = "[菜单][停止] 焦点入口安装失败，已撤回本批菜单槽。";
const char ControllerText_Menu_HooksInstalledLog[] = "[菜单] 标题/读档/系统/对话/日志/技能/背包/储物箱已安装；A原操作，B分级取消，X切已显示格子页，Y切格子/按钮。";
/* Profile：对应源文件使用的显示文字与诊断信息。 */
const char ControllerText_Profile_ExecutableHashLog[] = "[基线] 当前 EXE SHA-256=%s";
const char ControllerText_Profile_SignatureMismatchLog[] = "[签名失败] 地址=0x%08lx，保持原游戏输入。";
const char ControllerText_Profile_StaticSelectorMapMismatchLog[] = "[签名失败] 静态选择器地图地址=%08lx，档案地址=%08lx，保持原游戏输入。";
const char ControllerText_Profile_WaiZhuanSteamName[] = "外传Steam 2.01";
const char ControllerText_Profile_DaoJianSteamName[] = "本体Steam 1.05";
const char ControllerText_Profile_WaiZhuanNonSteamName[] = "外传非 Steam 2.01";
const char ControllerText_Profile_DaoJianNonSteamName[] = "本体非 Steam 1.05";
