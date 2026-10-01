#ifndef EDSLASH_FILE_IO_H
#define EDSLASH_FILE_IO_H
#include <stddef.h>
/* 路径用Windows宽字符，文件内容保持UTF-8；两种编码不能混用。 */
int RuntimeFile_Sibling(void *module,const wchar_t *name,wchar_t *path,size_t capacity);
int RuntimeFile_Read(const wchar_t *path,char *bytes,size_t capacity,size_t *size);
int RuntimeFile_WriteAtomic(const wchar_t *path,const char *bytes,size_t size,int create_only);
#endif
