#ifndef BLADESWORD_QOL_GAME_PROFILE_H
#define BLADESWORD_QOL_GAME_PROFILE_H

/*
 * GameProfile.h
 *
 * 这个文件只描述“当前进程到底是哪一款游戏”。
 * 以后所有功能模块都必须通过 GameProfile 了解游戏身份，不能各自再猜一次 EXE。
 * 这样本体/外传识别只发生一次，后面的 DisplayFix、手柄、QoL 都共享同一个结论。
 */

typedef enum GameId {
    GAME_ID_UNKNOWN = 0,
    GAME_ID_DAOJIAN = 1,
    GAME_ID_WAIZHUAN = 2
} GameId;

/*
 * RuntimeApiProfile 保存主程序导入表中两个基础入口的位置。
 * Runtime 通过这两个入口查询其它 Kernel32 API，因此主 ASI 自身仍可保持零导入表。
 */
typedef struct RuntimeApiProfile {
    unsigned long get_module_handle_a_iat_rva;
    unsigned long get_proc_address_iat_rva;
} RuntimeApiProfile;

/*
 * QolGameProfile 保存 QoL 模块需要的游戏内部入口。
 * 所有数值都是相对主 EXE ImageBase 的 RVA，避免在模块代码中散落绝对地址。
 */
typedef struct QolGameProfile {
    unsigned long ground_item_update_rva;
    unsigned long show_item_name_rva;
    unsigned long input_update_rva;
    unsigned long action_entry_rva;
    unsigned long pickup_entry_rva;

    unsigned long ground_manager_global_rva;
    unsigned long action_slot_table_global_rva;
    unsigned long pickup_action_vtable_rva;
    unsigned long ground_item_vtable_rva;
    unsigned long drop_flight_tick_limit_rva;
} QolGameProfile;

typedef struct GameProfile {
    GameId game_id;
    const char* display_name;
    unsigned long image_size;
    unsigned long entry_point_rva;
    RuntimeApiProfile runtime_api;
    QolGameProfile qol;
} GameProfile;

/* 读取主 EXE 的 PE32 头并返回已支持的 Profile；不支持时返回空指针。 */
const GameProfile* GameProfile_Detect(void);

#endif
