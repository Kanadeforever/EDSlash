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

typedef struct GameProfile {
    GameId game_id;
    const char* display_name;
    unsigned long image_size;
    unsigned long entry_point_rva;
} GameProfile;

/* 读取主 EXE 的 PE32 头并返回已支持的 Profile；不支持时返回空指针。 */
const GameProfile* GameProfile_Detect(void);

#endif
