/* Runtime 模块集中式文本。
 * 变量名按功能和用途命名；修改字符串内容后重新编译。
 * Label是名称，Description/Help是说明，Log是日志，Error/Reason是错误或原因。
 * Button是按钮文字，Value是显示值，Format/Template是保留占位符的文本。
 * 保留格式占位符及其顺序，避免与调用参数不匹配。
 * 本文件编译进 ASI，发行时不需要外置文本文件。 */
#include "RuntimeText.h"

/* Config：对应源文件使用的显示文字与诊断信息。 */
const char RuntimeText_Config_GuardStaminaPercentDescription[] = "设置防御每次消耗的体力。100%代表角色整条最大体力；1%代表其中百分之一。例如最大体力100时，1%就是1点。0表示不扣。需要将防御体力消耗方式设为按最大体力比例。";
const char RuntimeText_Config_HitRecoveryPercentDescription[] = "设置每次攻击造成实际伤害后恢复的体力。100%代表整条最大体力；1%代表其中百分之一。0表示不恢复。需要将攻击命中体力恢复方式设为按最大体力比例。没有伤害敌人时不恢复，恢复也不能超过体力上限。";
const char RuntimeText_Config_InteractButtonDescription[] = "按下后与附近的人物、物品、机关或换区点互动。会优先选择较近和角色前方的目标。" "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。";
const char RuntimeText_Config_AimButtonDescription[] = "按住时预览动作落点，用左摇杆调整方向，松开后执行。默认用于跳跃，外传长老默认是瞬移。" "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。";
const char RuntimeText_Config_LeftActionButtonDescription[] = "执行左手动作，通常是普通攻击。左右手动作可在同时按住LT和RT、拨动右摇杆打开的菜单中选择。" "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。";
const char RuntimeText_Config_RightActionButtonDescription[] = "执行右手当前选择的技能或连招套组。可以连续按下，按游戏原来的规则衔接动作。" "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。";
const char RuntimeText_Config_RunButtonDescription[] = "移动中按下后开始跑步；松开左摇杆停止移动，再移动时恢复走路。不改变跑步速度。" "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。";
const char RuntimeText_Config_MapButtonDescription[] = "按下显示或隐藏小地图，方便查看周围道路和位置。" "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。";
const char RuntimeText_Config_SystemMenuButtonDescription[] = "按下打开或关闭游戏原本的系统菜单，用于存档、读档和返回等操作。插件设置仍用LT加RT加Back打开。" "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。";
const char RuntimeText_Config_DisplayEnabledLabel[] = "动态宽屏";
const char RuntimeText_Config_DisplayEnabledDescription[] = "让游戏画面适应较宽的屏幕，减少两侧空白。关闭时使用原游戏的画面方式。保存后需要退出游戏并重新打开才会改变。";
const char RuntimeText_Config_BaseHeightLabel[] = "逻辑高度";
const char RuntimeText_Config_BaseHeightDescription[] = "决定画面能显示多大范围的场景。第一次使用建议保持480。调大后通常能看见更多场景，但人物和物品会变小，也会增加电脑负担。保存后重新打开游戏生效。";
const char RuntimeText_Config_AspectRatioLabel[] = "画面比例";
const char RuntimeText_Config_AspectRatioDescription[] = "选择画面的宽和高之间的比例。自动会跟随游戏窗口；16:9适合常见宽屏，4:3接近原游戏。保存后需要重新打开游戏。";
const char RuntimeText_Config_FontDpiFixLabel[] = "字体DPI修复";
const char RuntimeText_Config_FontDpiFixDescription[] = "用于修正Windows显示缩放造成的字体大小异常。如果文字大小正常，可以保持当前选择。保存后需要退出并重新打开游戏。";
const char RuntimeText_Config_CenterMainHudLabel[] = "HUD居中";
const char RuntimeText_Config_CenterMainHudDescription[] = "把画面底部的生命、体力、技能和物品快捷栏放在屏幕中间。关闭后使用原游戏位置。保存后直接改变位置，不需要重新打开游戏。";
const char RuntimeText_Config_AuxiliaryUiAboveHudLabel[] = "辅助界面层级";
const char RuntimeText_Config_AuxiliaryUiAboveHudDescription[] = "开启后，背包等打开的窗口会显示在底部生命条和快捷栏的前面，避免被它们遮住。关闭后使用原游戏的叠放顺序。只改变谁在前面，不移动按钮。保存后直接生效。";
const char RuntimeText_Config_ItemNamesAlwaysVisibleLabel[] = "物品名称常显";
const char RuntimeText_Config_ItemNamesAlwaysVisibleDescription[] = "不用按住查看键，就能看到地上物品的名字，更容易判断要不要拾取。关闭后按照原游戏的方式显示物品名字。";
const char RuntimeText_Config_AutoPickupModeLabel[] = "自动拾取范围";
const char RuntimeText_Config_AutoPickupModeDescription[] = "角色靠近地上物品时，插件会自动拾取选定类别。可选择关闭、只捡钱、钱和恢复道具、再加入宝石护身石，或者全部物品。仍然需要有足够的背包空间；不在范围内的物品可以手动拾取。";
const char RuntimeText_Config_PickupScanIntervalLabel[] = "拾取扫描间隔";
const char RuntimeText_Config_PickupScanIntervalDescription[] = "自动拾取每隔多久检查一次附近物品。1000毫秒等于1秒，100毫秒等于0.1秒。调小会更快尝试拾取，但更频繁检查会增加电脑负担；0表示每次游戏刷新输入都检查。";
const char RuntimeText_Config_DropWaitLabel[] = "掉落等待";
const char RuntimeText_Config_DropWaitDescription[] = "怪物掉落物品后，自动拾取至少等待多久。1000毫秒等于1秒。调小可以更早拾取，但物品仍必须先完成原游戏的落地动作；0也不能拾取还在空中的物品。";
const char RuntimeText_Config_ControllerEnabledLabel[] = "手柄功能";
const char RuntimeText_Config_ControllerEnabledDescription[] = "开启后可以用手柄移动、战斗和操作菜单。关闭后使用原键盘鼠标；插件设置入口仍保留，可以再开启。保存后先松开手柄输入，等当前动作结束，才会切换。";
const char RuntimeText_Config_StickDeadzoneLabel[] = "摇杆死区";
const char RuntimeText_Config_StickDeadzoneDescription[] = "摇杆推动超过这段幅度才开始控制角色。调大可以减少摇杆轻微偏移造成的自行移动，但需要推得更远。调小会更灵敏。连接手柄后先松开摇杆和按键，再开始使用。";
const char RuntimeText_Config_MoveLeadLabel[] = "移动前探";
const char RuntimeText_Config_MoveLeadDescription[] = "控制角色朝摇杆方向移动时，向前查找移动目标的距离。数值越大，目标放得越远；数值越小，目标更靠近角色。走路和跑步共用这项。修改后松开输入，等当前动作结束生效。";
const char RuntimeText_Config_MouseModeSpeedLabel[] = "鼠标模式速度";
const char RuntimeText_Config_MouseModeSpeedDescription[] = "在手柄的鼠标救援模式里，控制指针移动速度。调大移动更快，调小更方便对准小按钮。右摇杆使用较慢的速度。正常角色移动不受这项影响。";
const char RuntimeText_Config_RumbleLabel[] = "手柄震动";
const char RuntimeText_Config_RumbleDescription[] = "切换手柄和鼠标救援模式时，让手柄轻微震动，提醒切换成功。关闭后不震动。手柄本身不支持震动时，这项不会产生效果。";
const char RuntimeText_Config_GuardChargeOnHitLabel[] = "受击才扣防御体力";
const char RuntimeText_Config_GuardChargeOnHitDescription[] = "按住LT防御时，只有成功格挡攻击才扣体力；没有受到攻击时不扣。关闭后恢复原游戏规则，持续防御也会不断扣体力。体力不足仍会按原游戏规则停止防御。";
const char RuntimeText_Config_RunWithoutStaminaCostLabel[] = "跑步不扣体力";
const char RuntimeText_Config_RunWithoutStaminaCostDescription[] = "开启后跑步不消耗体力，战斗中也一样。关闭后恢复原游戏跑步消耗体力的规则。不改变跑步速度。";
const char RuntimeText_Config_OmnidirectionalGuardLabel[] = "全方位格挡";
const char RuntimeText_Config_OmnidirectionalGuardDescription[] = "按住LT防御时，可以格挡来自前后左右的普通攻击。关闭后只按原游戏允许的方向格挡。体力太少或遇到原本就无法格挡的攻击，仍可能受伤。";
const char RuntimeText_Config_DirectionalDodgeLabel[] = "方向闪避";
const char RuntimeText_Config_DirectionalDodgeDescription[] = "闪避时，左摇杆推向哪里，就朝哪里直线移动。关闭后使用原游戏的闪避方向和路径。当前闪避不会被半途改变；下一次闪避使用新选择。";
const char RuntimeText_Config_DodgeDistanceLabel[] = "闪避距离";
const char RuntimeText_Config_DodgeDistanceDescription[] = "方向闪避一次移动多远。调大距离更长，调小更容易停在近处。64大约相当于一个地图格。墙壁和障碍仍会限制移动。只在方向闪避开启时使用；当前闪避结束后生效。";
const char RuntimeText_Config_GuardCostModeLabel[] = "防御体力消耗方式";
const char RuntimeText_Config_GuardCostModeDescription[] = "选择防御扣掉多少体力：原版数值沿用这个角色的原值；按最大体力比例使用下方的防御体力消耗比例。百分比以整条体力的最大值计算，不是当前剩余体力。";
const char RuntimeText_Config_GuardStaminaPercentLabel[] = "防御体力消耗比例";
const char RuntimeText_Config_HitRecoveryModeLabel[] = "攻击命中体力恢复方式";
const char RuntimeText_Config_HitRecoveryModeDescription[] = "攻击确实伤害敌人时，可以恢复体力。选择与防御消耗一致，恢复量就和防御扣费相同；选择按最大体力比例，则使用下方的攻击命中体力恢复比例。挥空或完全被格挡不会恢复。";
const char RuntimeText_Config_HitRecoveryPercentLabel[] = "攻击命中体力恢复比例";
const char RuntimeText_Config_InteractDistanceLabel[] = "正面调查最大距离";
const char RuntimeText_Config_InteractDistanceDescription[] = "按调查键时，在角色周围多远的范围内寻找物品、人物、机关和换区点。调大可以更远选中，调小更容易区分靠近的目标。会优先选择较近和角色前方的目标；仍需满足原游戏的互动条件。默认160，最多480。";
const char RuntimeText_Config_LegacyFinisherInputLabel[] = "旧必杀输入模式";
const char RuntimeText_Config_LegacyFinisherInputDescription[] = "关闭时使用较安全的新方式：同时按住LT和RT，再按A/B/X/Y准备必杀；松开面键后再次按同一个键才释放。开启后只需LT加面键，较容易误触，连招切换也固定使用LT加方向键。";
const char RuntimeText_Config_ComboSwitchInputLabel[] = "连招切换输入";
const char RuntimeText_Config_ComboSwitchInputDescription[] = "选择四套连招的切换方法。面键方式为按住LT，再按Y/B/A/X选择第1/2/3/4套；方向键方式为LT加上/右/下/左。只有旧必杀输入模式关闭时可以选择；旧模式固定用方向键。";
const char RuntimeText_Config_AimSpreadTimeLabel[] = "技能落点扩散耗时";
const char RuntimeText_Config_AimSpreadTimeDescription[] = "按住跳跃预览键时，落点从近处扩到最远处需要多久。1000毫秒等于1秒。调小扩得更快，调大更方便慢慢选距离。松开按键后朝预览位置跳跃，长老使用瞬移。正在预览的这一次不受修改影响。";
const char RuntimeText_Config_SwapMenuConfirmCancelLabel[] = "菜单确认取消交换";
const char RuntimeText_Config_SwapMenuConfirmCancelDescription[] = "交换菜单里的确认和取消按钮。关闭为A确认、B取消；开启为B确认、A取消。不会交换战斗动作和组合键。当前插件设置窗口保持打开时的按法，下次打开才使用新按法。";
const char RuntimeText_Config_InteractButtonLabel[] = "调查键";
const char RuntimeText_Config_AimButtonLabel[] = "技能落点预览键";
const char RuntimeText_Config_LeftActionButtonLabel[] = "左手动作键";
const char RuntimeText_Config_RightActionButtonLabel[] = "右手动作键";
const char RuntimeText_Config_RunButtonLabel[] = "奔跑切换键";
const char RuntimeText_Config_MapButtonLabel[] = "小地图键";
const char RuntimeText_Config_SystemMenuButtonLabel[] = "系统菜单键";
const char RuntimeText_Config_LoggingEnabledLabel[] = "插件日志";
const char RuntimeText_Config_LoggingEnabledDescription[] = "记录插件运行和故障信息，方便排查问题。关闭后不再记录普通日志、性能摘要和插件崩溃记录；已有文件保留。保存后生效，再开启即可继续记录。";
const char RuntimeText_Config_WorldButtonsHelp[] = "这是没有按住LT、RT等组合键时的按钮。菜单确认取消不随它改变。七项不能用同一个按钮；交换两键时，先把两项改好再保存。";
const char RuntimeText_Config_LoggingSectionTemplate[] = "[logging]\r\n# 插件日志总开关；保存后生效。\r\n%s = %s\r\n\r\n";
const char RuntimeText_Config_DefaultHeaderTemplate[] = "# EDSlash统一配置；UTF-8无BOM，CRLF。\r\n# 本版本只读取此TOML，不读取旧INI。\r\n[logging]\r\n# 插件日志：关闭后不记录运行、性能和插件崩溃信息；已有文件保留。\r\nenabled = true\r\n\r\n[meta]\r\nschema = 1\r\n";
const char RuntimeText_Config_DefaultBindingsTemplate[] = "\r\n[controller.bindings]\r\n# 未设置是所有RT快捷位置的默认状态；明确设置的技能按游戏、角色、槽位分别保存。\r\ndefault = \"none\"\r\n" "# character_编号来自Player+0x348的创建角色selector；技能选择码须来自该角色实际技能。\r\n";
const char RuntimeText_Config_TypeOrRangeError[] = "第%u行：%s的类型或范围错误";
const char RuntimeText_Config_DuplicateWorldButtonError[] = "%s 和 %s 的按钮重复。请选不同按钮；交换两键要一起改好再保存。";
const char RuntimeText_Config_UnsupportedSchemaError[] = "只支持最终配置schema=1";
const char RuntimeText_Config_InvalidDefaultBindingError[] = "默认技能绑定只能为none";
const char RuntimeText_Config_UnknownFieldError[] = "第%u行：未知配置项%s.%s";
const char RuntimeText_Config_BindingCapacityError[] = "角色技能绑定数量超过限制";
const char RuntimeText_Config_InvalidBindingModeError[] = "技能绑定mode只能为none或skill";
const char RuntimeText_Config_InvalidSkillSelectorError[] = "技能选择码范围为0..65535";
const char RuntimeText_Config_InvalidHandError[] = "技能手侧只能为left或right";
const char RuntimeText_Config_UnknownBindingFieldError[] = "未知角色技能绑定字段";
const char RuntimeText_Config_PathTooLongError[] = "配置路径过长";
const char RuntimeText_Config_ReadFileError[] = "无法读取TOML，原文件未改";
const char RuntimeText_Config_DefaultTemplateError[] = "无法生成默认TOML";
const char RuntimeText_Config_ParseLineError[] = "第%u行：%s";
const char RuntimeText_Config_LocateSiblingFileError[] = "无法定位ASI旁TOML";
const char RuntimeText_Config_NotInitializedError[] = "配置服务未初始化";
const char RuntimeText_Config_ExternallyModifiedError[] = "配置文件已被外部修改，请重新载入后再保存";
const char RuntimeText_Config_SaveCapacityError[] = "配置保存超过容量";
const char RuntimeText_Config_SaveFileError[] = "TOML保存失败，原文件和有效设置保持";
const char RuntimeText_Config_ValueOutOfRangeError[] = "设置值超出允许范围";
const char RuntimeText_Config_CandidateGenerationError[] = "无法生成配置候选";
const char RuntimeText_Config_InvalidChoiceError[] = "选项名称不在允许列表内";
const char RuntimeText_Config_InvalidAspectRatioError[] = "画面比例必须为auto或有效宽:高";

const char RuntimeText_Config_InvalidBindingArgumentsError[] = "技能绑定参数无效";
const char RuntimeText_Config_BindingGenerationError[] = "无法生成完整技能绑定";
const char RuntimeText_Config_InvalidBatchArgumentsError[] = "批量配置参数无效";
const char RuntimeText_Config_DuplicateBatchFieldError[] = "批量配置包含重复字段";
const char RuntimeText_Config_InvalidBatchFieldError[] = "批量配置字段无效";
const char RuntimeText_Config_InvalidBatchTextError[] = "批量文本值无效";
const char RuntimeText_Config_BatchValueOutOfRangeError[] = "批量设置值超出范围";
const char RuntimeText_Config_BatchCandidateGenerationError[] = "无法生成批量配置候选";
const char RuntimeText_Config_InvalidBatchBindingArgumentsError[] = "批量技能绑定参数无效";
const char RuntimeText_Config_DuplicateBatchBindingSlotError[] = "批量技能绑定包含重复槽位";
const char RuntimeText_Config_BatchBindingGenerationError[] = "无法生成完整批量技能绑定";
/* Focus：对应源文件使用的显示文字与诊断信息。 */
const char RuntimeText_Focus_SourceRectReadStage[] = "焦点框：原源图矩形裁取";
const char RuntimeText_Focus_SpriteReadStage[] = "焦点框：原常规动态精灵";
const char RuntimeText_Focus_AnimationFrameReadStage[] = "焦点框：原动画帧读取";
const char RuntimeText_Focus_ImageReadStage[] = "焦点框：原图像读取";
const char RuntimeText_Focus_DrawGeometryLog[] = "[焦点绘制] %s 原图=%d,%d 目标=%ld,%ld；同尺寸原Draw，异尺寸原图边缘拼接，禁止越界裁取。";
const char RuntimeText_Focus_SharedFrameLabel[] = "共享焦点";
/* GameProfile：对应源文件使用的显示文字与诊断信息。 */
const char RuntimeText_Game_DaoJianTitle[] = "刀剑封魔录";
const char RuntimeText_Game_WaiZhuanTitle[] = "刀剑封魔录外传：上古传说";
/* Log：对应源文件使用的显示文字与诊断信息。 */
const char RuntimeText_Log_BuildIdentityAndAuthor[] = "EDSlash：构建身份=%s；作者：%s";
/* Perf：对应源文件使用的显示文字与诊断信息。 */
const char RuntimeText_Perf_NativeInputStage[] = "原版输入刷新";
const char RuntimeText_Perf_SdlSamplingStage[] = "SDL采样";
const char RuntimeText_Perf_TargetTraversalStage[] = "目标遍历";
const char RuntimeText_Perf_ControllerStage[] = "手柄业务";
const char RuntimeText_Perf_HitWrapperStage[] = "受击包装";
const char RuntimeText_Perf_NativeHitStage[] = "原生受击";
const char RuntimeText_Perf_AutoPickupStage[] = "自动拾取";
const char RuntimeText_Perf_InteractionProbeStage[] = "调查探测";
const char RuntimeText_Perf_InputIntervalSummaryLog[] = "[性能] 窗口=%.2f秒 输入次数=%u 输入间隔平均=%.3f毫秒 最大=%.3f毫秒";
const char RuntimeText_Perf_StageTimingSummaryLog[] = "[性能][%s] 次数=%u 平均=%.3f毫秒 最大=%.3f毫秒 合计=%.3f毫秒";
const char RuntimeText_Perf_LogQueueSummaryLog[] = "[性能][日志] 文件打开=%lu 写入次数=%lu 队列字节=%lu 丢失消息=%lu";
/* Runtime：对应源文件使用的显示文字与诊断信息。 */
const char RuntimeText_Startup_InputFrameUnavailableLog[] = "[输入][警告] 公共帧入口不可用；自动拾取模块将自行停止，显示与手柄继续初始化。";
const char RuntimeText_Startup_FocusDrawingUnavailableLog[] = "[焦点][警告] 原绘制接口未通过校验，保留各界面原反馈。";
/* SettingsWindow：对应源文件使用的显示文字与诊断信息。 */
const char RuntimeText_Settings_DrawEntryRestoreError[] = "原绘制入口恢复失败，请再次关闭。";
const char RuntimeText_Settings_InputEntryRestoreError[] = "原输入入口恢复失败，请再次关闭。";
const char RuntimeText_Settings_ClosedLog[] = "[模组设置] 关闭并释放原菜单捕获，原游戏恢复。";
const char RuntimeText_Settings_OpenedLog[] = "[模组设置] 打开并取得原系统菜单暂停/捕获，角色selector=%u。";
const char RuntimeText_AboutFaq[] =
    "这玩意收费？\n"
    "   本模组使用MIT协议开源，被偷已经是预料之中的事情了。不懂自己问豆包这啥意思。\n   还不懂自己想办法，没人天生该伺候你；\n   如果你是买的资源里看到这个消息，那容我嘲笑一下你。\n   如果看到这个不高兴了可以删了换别人做的或者你自己上。\n   厚脸皮那我也没法了。\n\n"
    "既然知道会被偷那还放这个？\n"
    "   放点信息当最后的挣扎了，还能让看到的人一起嘲笑\n   被人骗钱或者主动送钱给“不劳而获的狗”的傻子了。\n\n"
    "话说的太难听了，不适合放进来\n"
    "   我做的东西我怎么处理都行，我都开源了你让我diss下\n   某种懒狗和偷东西的畜生不行啊？还受不了自己想办法\n\n"
    "如果要整合这个MOD或者要用这个MOD的代码？\n"
    "   在我能看得到的地方打个招呼，外带发布的时候声明一下，\n   都MIT协议了，署名是应该的好吧，就算代码是AI写的插件那也全部是我设计的。\n\n\n"
    "========== 下面是正式的Q&A ==========\n\n\n"
    "怎样操作设置菜单？\n"
    "   LB/RB切页，方向键浏览。\n默认A确认、B取消；\n可在按键设置中交换确认和取消。\n\n"
    "修改设置后怎样生效？\n"
    "   按START保存修改。\n标有重启提示的项目，要退出并重新启动游戏才会生效。\n\n"
    "技能快捷是每个存档单独保存的吗？\n"
    "   本体与外传分别保存。\n每个职业共用一组14个快捷位置，\n同职业的不同存档共用这一组。\n\n"
    "遇到问题怎样反馈？\n"
    "   1、先开启插件日志；\n2、重新进游戏触发BUG；\n3、把触发BUG后的EDSlash.log和本页的构建编号提交到Github的issues板块。\n4、等我修，或者你自己修；\n5、修复期间别嚎，嚎破嗓子没用，再叫老子不干了！\n\n"
    "Github以外不接受任何BUG反馈！！！！！！\n\n"
    "Github以外不接受任何BUG反馈！！！！！！\n\n"
    "Github以外不接受任何BUG反馈！！！！！！\n\n"
    "Github以外不接受任何BUG反馈！！！！！！\n\n"
    "上面的信息非常重要，说四遍。\n\n\n"
    "========== 下面是必要信息 ==========\n\n\n"
    "项目地址：\n\n"
    "   https://github.com/Kanadeforever/EDSlash\n\n"
    "个人主页地址：\n\n"
    "   https://space.bilibili.com/419051\n\n";
const char RuntimeText_AboutIntroduction[] = "插件说明";
const char RuntimeText_AboutBuild[] = "版本与构建";
const char RuntimeText_AboutAuthor[] = "作者";
const char RuntimeText_AboutFaqTitle[] = "常见问题解答";
const char RuntimeText_AboutDependencies[] = "第三方组件";
const char RuntimeText_Settings_AboutIntroductionBody[] = "为《刀剑封魔录》、《刀剑封魔录外传：上古传说》提供手柄操作、现代化画面适配和便利功能。";
const char RuntimeText_Settings_AboutVersionBuildFormat[] = "版本：%s\n构建：%s";
const char RuntimeText_Settings_AboutDependenciesBody[] = "使用 SDL 3.4.16 官方库。随发行提供第三方许可说明；本插件不包含游戏文件与ASI加载器。";
const wchar_t RuntimeText_Settings_FontFace[] = L"宋体";
const char RuntimeText_Settings_SavedRestartRequiredMessage[] = "设置已保存。待重启的项目在下次启动时生效。";
const char RuntimeText_Settings_SavedAwaitingSafeApplyMessage[] = "设置已保存。修改会在对应操作安全结束后生效。";
const char RuntimeText_Settings_CurrentItemResetMessage[] = "当前项目已恢复默认值，保存后生效。";
const char RuntimeText_Settings_CurrentPageResetMessage[] = "本页已恢复默认设置，保存后生效。";
const char RuntimeText_Settings_UnavailableValue[] = "当前不可用";
const char RuntimeText_Settings_GuardCostMaximumStaminaPercentValue[] = "按最大体力比例";
const char RuntimeText_Settings_GuardCostOriginalValue[] = "游戏原始数值";
const char RuntimeText_Settings_HitRecoveryMaximumStaminaPercentValue[] = "按最大体力比例";
const char RuntimeText_Settings_HitRecoveryMatchesGuardValue[] = "与防御消耗相同";
const char RuntimeText_Settings_ComboSwitchDpadValue[] = "LT＋方向键";
const char RuntimeText_PickupDisabled[] = "关闭自动拾取";
const char RuntimeText_Settings_PickupMoneyValue[] = "金钱";
const char RuntimeText_Settings_PickupMoneyAndRecoveryValue[] = "金钱+恢复道具";
const char RuntimeText_Settings_PickupMoneyRecoveryAndStonesValue[] = "金钱+恢复道具+石头";
const char RuntimeText_Settings_PickupAllDropsValue[] = "全部掉落物";
const char RuntimeText_Enabled[] = "开启";
const char RuntimeText_Settings_DisabledValue[] = "关闭";
const wchar_t RuntimeText_Settings_NativeEntryCaption[] = L"EDSlash设置";

const char RuntimeText_Settings_Title[] = "EDSlash 模组设置";
const char RuntimeText_TabSettings[] = "模组设置";
const char RuntimeText_TabControls[] = "按键设置";
const char RuntimeText_TabSkills[] = "技能快捷";
const char RuntimeText_TabAbout[] = "关于";
const char RuntimeText_Settings_UnsetSkillValue[] = "未设置";

const char RuntimeText_Settings_PendingRestartSuffix[] = " [待重启]";
const char RuntimeText_Settings_PendingInputReleaseSuffix[] = " [待释放]";
const char RuntimeText_Settings_PendingApplySuffix[] = " [待应用]";
const char RuntimeText_Settings_RestartRequiredSuffix[] = " [重启]";
const char RuntimeText_Settings_SaveButton[] = "保存";
const char RuntimeText_Settings_CloseButton[] = "关闭";

const char RuntimeText_ResetCurrent[] = "当前默认";
const char RuntimeText_ResetPage[] = "本页默认";
const char RuntimeText_Settings_HideHelpButton[] = "隐藏说明";
const char RuntimeText_Settings_ShowHelpButton[] = "显示说明";
const char RuntimeText_Settings_AboutNavigationHint[] = "LB/RB 换页 | ↑/↓ 浏览 | START 保存 | 取消键 关闭";
const char RuntimeText_Settings_ValueEditorNavigationHint[] = "左右×1 | 上下×10 | 长按加速 | BACK 默认 | START 保存";
const char RuntimeText_Settings_SettingsNavigationHint[] = "LB/RB 分类 | BACK 本页默认 | START 保存 | Y 说明";
const char RuntimeText_Settings_SkillPickerTitle[] = "选择已学会的技能";
const char RuntimeText_Settings_SkillDescriptionTitle[] = "技能说明";

const char RuntimeText_Settings_CurrentValueCaption[] = "当前选择／数值：";
const char RuntimeText_Settings_PreviousChoiceButton[] = "上一项";
const char RuntimeText_Settings_DecreaseValueButton[] = "减小数值";
const char RuntimeText_Settings_NextChoiceButton[] = "下一项";
const char RuntimeText_Settings_IncreaseValueButton[] = "增大数值";
const char RuntimeText_Settings_FinishEditingButton[] = "完成";
const char RuntimeText_Settings_CancelEditingButton[] = "取消";
const char RuntimeText_Settings_ValueEditorHelp[] = "左右调整最小单位，上下调整十倍。按住方向两秒后加快，百分比加速更快。\n\n完成只保留这次修改；返回主列表后，选择“保存并应用”才会保存。取消会恢复打开这个调整窗口前的值。\n\n按Y可查看本项的详细说明。";
const char RuntimeText_Settings_UnsetSkillHelp[] = "未设置任何技能。选择并保存后，按这个RT组合键不会发动技能。";
const char RuntimeText_Settings_SkillBindingHelp[] = "先选择一个RT组合键位置并确认，再从已学会的技能中选择。保存后，按住RT并按这个组合键就会直接发动技能，不需要再按鼠标右键。每个角色分别保存。";

const char RuntimeText_Settings_ResetPageConfirmation[] = "将本页恢复默认？其它页面不变，保存后生效。";
const char RuntimeText_Settings_DiscardChangesConfirmation[] = "有未保存的修改。丢弃并关闭，或继续编辑。";
const char RuntimeText_Settings_ConfirmResetButton[] = "恢复默认";
const char RuntimeText_Settings_DiscardAndCloseButton[] = "丢弃并关闭";
const char RuntimeText_Settings_ContinueEditingButton[] = "继续编辑";
const char RuntimeText_Settings_SaveErrorTitle[] = "无法保存设置";
const char RuntimeText_Settings_ReturnToEditingButton[] = "返回修改";
const char RuntimeText_Settings_InterfaceSignatureMismatchLog[] = "[模组设置] 原接口%u地址%08lX未通过签名校验，设置窗口停用。";
const char RuntimeText_Settings_InterfacesReadyLog[] = "[模组设置] 原接口已校验；场景中LT+RT+Back或主键盘重音键（1左边的）打开并使用原暂停。";
/* Toml：对应源文件使用的显示文字与诊断信息。 */
const char RuntimeText_Toml_EmptyOrOversizedError[] = "配置为空或超过64KiB限制";
const char RuntimeText_Toml_InvalidUtf8Error[] = "配置不是有效UTF-8文本";
const char RuntimeText_Toml_BomNotAllowedError[] = "配置必须使用UTF-8无BOM";
const char RuntimeText_Toml_ArrayTableUnsupportedError[] = "只支持普通配置表，不支持数组表";
const char RuntimeText_Toml_InvalidTableNameError[] = "配置表名无效";
const char RuntimeText_Toml_EmptyTableGroupError[] = "配置表名含空分组";
const char RuntimeText_Toml_DuplicateTableError[] = "配置表重复定义";
const char RuntimeText_Toml_TableCapacityError[] = "配置表数量过多";
const char RuntimeText_Toml_MissingEqualsError[] = "配置项缺少等号";
const char RuntimeText_Toml_InvalidKeyOrValueError[] = "配置键名或值无效";
const char RuntimeText_Toml_DuplicateKeyError[] = "配置键重复定义";
const char RuntimeText_Toml_EntryCapacityError[] = "配置项数量过多";
const char RuntimeText_Toml_UnsupportedValueTypeError[] = "值必须是布尔、十进制整数、百分比或双引号字符串";

/* 英文界面标记与诊断片段同样集中编辑。 */
const char RuntimeText_Key_A[] = "A";
const char RuntimeText_Key_B[] = "B";
const char RuntimeText_Key_X[] = "X";
const char RuntimeText_Key_Y[] = "Y";
const char RuntimeText_Key_Up[] = "↑";
const char RuntimeText_Key_Down[] = "↓";
const char RuntimeText_Key_Left[] = "←";
const char RuntimeText_Key_Right[] = "→";
const char RuntimeText_Key_LeftShoulder[] = "LB";
const char RuntimeText_Key_RightShoulder[] = "RB";
const char RuntimeText_Key_Back[] = "Back";
const char RuntimeText_Key_Start[] = "Start";
const char RuntimeText_Key_LeftStick[] = "L3";
const char RuntimeText_Key_RightStick[] = "R3";
const char RuntimeText_Key_StartUppercase[] = "START";

const char RuntimeText_Key_BackUppercase[] = "BACK";

const char RuntimeText_DevelopmentVersion[] = "开发版";
const char RuntimeText_UnspecifiedBuild[] = "未指定源码摘要";
const char RuntimeText_Author[] = "Luminous / のの不想想名字";
const char RuntimeText_ComboFaceKeys[] = "LT＋Y/B/A/X";
const char RuntimeText_SkillSlotFormat[] = "RT + %s";
const char RuntimeText_ConfirmCancelKeys[] = "A/B";
