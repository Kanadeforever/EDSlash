#ifndef BLADESWORD_PICKUP_NOTICE_H
#define BLADESWORD_PICKUP_NOTICE_H
#include "../../Runtime/Runtime.h"
/* 每次同步拾取独立保存前后数据；原版可能销毁地面对象，因此名字必须先复制。 */
typedef struct PickupNoticeSnapshot {
    unsigned long inventory,world,collector,item_id,before;
    int money,valid;
    char name[128];
} PickupNoticeSnapshot;
int PickupNotice_Initialize(const RuntimeContext *runtime);
void PickupNotice_Before(unsigned long ground,unsigned long collector,PickupNoticeSnapshot *snapshot);
void PickupNotice_After(const PickupNoticeSnapshot *snapshot);
void PickupNotice_Disable(void);
#endif
