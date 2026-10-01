#ifndef EDSLASH_TOML_H
#define EDSLASH_TOML_H
#include <stddef.h>
#include <stdint.h>

/* 配置只有一份，限制总长度和条目数，坏文件不能让插件无限分配内存。 */
#define TOML_CAPACITY 65536u
#define TOML_MAX_ENTRIES 384u
typedef struct {
    char table[96], key[48];
    size_t value_begin, value_end;
    unsigned line;
} TomlEntry;
typedef struct {
    char bytes[TOML_CAPACITY];
    size_t size;
    TomlEntry entries[TOML_MAX_ENTRIES];
    unsigned count, error_line;
    char error[160];
} TomlDocument;

/* 沿用 Castle Reforge 的 UTF-8、注释和标量读取核心，扩展重复定义检查与值编辑。
 * 这里只接受项目实际使用的表、布尔、十进制数和双引号字符串，不冒充完整 TOML。
 */
int Toml_Parse(TomlDocument *doc, const char *bytes, size_t size);
const TomlEntry *Toml_Find(const TomlDocument *doc, const char *table, const char *key);
int Toml_Bool(const TomlDocument *doc, const TomlEntry *entry, int *value);
int Toml_Integer(const TomlDocument *doc, const TomlEntry *entry, int *value);
int Toml_Percent(const TomlDocument *doc, const TomlEntry *entry, int *value);
int Toml_String(const TomlDocument *doc, const TomlEntry *entry, char *output, size_t capacity);
/* 编辑只替换值所在区间，中文注释、其它键和行内说明都继续保留。 */
int Toml_Update(const TomlDocument *doc, const char *table, const char *key,
                const char *literal, char *output, size_t capacity, size_t *size);
#endif
