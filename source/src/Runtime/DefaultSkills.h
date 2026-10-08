#ifndef EDSLASH_DEFAULT_SKILLS_H
#define EDSLASH_DEFAULT_SKILLS_H
/* 原技能选择键的位置映射；固定B跳跃取原第一位置，RT未设置时不使用此映射。
 * 这里只共享位置关系，Controller只在明确需要原动作来源时使用，不能作为未设置RT的后备。 */
static inline int RuntimeSkill_DefaultKey(unsigned slot)
{
    static const int keys[]={'Q','W','E','R','T','Y','U','I','O','A','S','D',0,0};
    return slot>=1 && slot<=14 ? keys[slot-1]:0;
}
#endif
