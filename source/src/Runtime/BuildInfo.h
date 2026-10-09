#ifndef EDSLASH_BUILD_INFO_H
#define EDSLASH_BUILD_INFO_H
#include "RuntimeText.h"
/* 构建系统提供版本和源码摘要；直接编译单个文件时明确显示身份未指定。
 * 日志和关于页共用作者名称，避免两处显示不同的信息。 */
#ifndef EDSLASH_VERSION
#define EDSLASH_VERSION RuntimeText_DevelopmentVersion
#endif
#ifndef EDSLASH_BUILD_ID
#define EDSLASH_BUILD_ID RuntimeText_UnspecifiedBuild
#endif
#define EDSLASH_AUTHOR RuntimeText_Author
#endif
