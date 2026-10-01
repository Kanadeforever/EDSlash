#include "Toml.h"
#include <stdio.h>
#include <string.h>
/* 标量核心改编自Castle Reforge Runtime TOML v1，版权与MIT许可见docs/配置与SDL第三方许可.txt。
 * 本项目增加索引、严格重复检查、定点百分比和保留注释的写回。 */
#define RUNTIME_TOML_NAME_CAP 256u
typedef struct { const char *data; unsigned length; } RuntimeTomlSlice;
static int toml_space_(char value) {
    return value == ' ' || value == '\t' || value == '\r';
}

static RuntimeTomlSlice toml_trim_(RuntimeTomlSlice value) {
    while (value.length && toml_space_(value.data[0])) {
        ++value.data;
        --value.length;
    }
    while (value.length && toml_space_(value.data[value.length - 1u])) {
        --value.length;
    }
    return value;
}

static unsigned toml_content_length_(const char* line, unsigned length) {
    unsigned index;
    int in_string = 0;
    int escaped = 0;
    for (index = 0u; index < length; ++index) {
        char value = line[index];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (value == '\\') escaped = 1;
            else if (value == '"') in_string = 0;
        } else if (value == '"') {
            in_string = 1;
        } else if (value == '#') {
            return index;
        }
    }
    return length;
}

static int toml_bare_name_valid_(RuntimeTomlSlice name) {
    unsigned index;
    if (!name.data || name.length == 0u || name.length >= RUNTIME_TOML_NAME_CAP) return 0;
    for (index = 0u; index < name.length; ++index) {
        char value = name.data[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '-' || value == '.')) return 0;
    }
    return 1;
}

static int toml_utf8_valid_(const unsigned char* bytes, unsigned size) {
    unsigned index = 0u;
    while (index < size) {
        unsigned char first = bytes[index++];
        unsigned needed;
        unsigned code;
        unsigned minimum;
        if (first < 0x80u) continue;
        if ((first & 0xE0u) == 0xC0u) {
            needed = 1u; code = first & 0x1Fu; minimum = 0x80u;
        } else if ((first & 0xF0u) == 0xE0u) {
            needed = 2u; code = first & 0x0Fu; minimum = 0x800u;
        } else if ((first & 0xF8u) == 0xF0u) {
            needed = 3u; code = first & 0x07u; minimum = 0x10000u;
        } else return 0;
        if (needed > size - index) return 0;
        while (needed--) {
            unsigned char next = bytes[index++];
            if ((next & 0xC0u) != 0x80u) return 0;
            code = (code << 6u) | (next & 0x3Fu);
        }
        if (code < minimum || code > 0x10FFFFu ||
            (code >= 0xD800u && code <= 0xDFFFu)) return 0;
    }
    return 1;
}

static int toml_parse_s32_(RuntimeTomlSlice value, int* out_value) {
    unsigned index = 0u;
    unsigned magnitude = 0u;
    int negative = 0;
    int digit_seen = 0;
    if (!out_value || !value.length) return 0;
    if (value.data[index] == '-' || value.data[index] == '+') {
        negative = value.data[index] == '-';
        if (++index == value.length) return 0;
    }
    if (value.length-index>1u && value.data[index]=='0') return 0;
    for (; index < value.length; ++index) {
        unsigned digit;
        char ch = value.data[index];
        if (ch == '_') { if (index == 0 || index + 1u == value.length || value.data[index-1u] < '0' || value.data[index-1u] > '9' || value.data[index+1u] < '0' || value.data[index+1u] > '9') return 0; continue; }
        if (ch < '0' || ch > '9') return 0;
        digit = (unsigned)(ch - '0');
        if (magnitude > 214748364u ||
            (magnitude == 214748364u && digit > (negative ? 8u : 7u))) return 0;
        magnitude = magnitude * 10u + digit;
        digit_seen = 1;
    }
    if (!digit_seen) return 0;
    *out_value = negative ? (int)(0u - magnitude) : (int)magnitude;
    return 1;
}

/* 文档解析先整体校验，再提供索引；错误不能留下一半可用的配置。 */
static int fail(TomlDocument *doc, unsigned line, const char *message)
{
    doc->error_line=line;
    snprintf(doc->error,sizeof doc->error,"%s",message);
    return 0;
}
const TomlEntry *Toml_Find(const TomlDocument *doc,const char *table,const char *key)
{
    if (!doc || !table || !key) return NULL;
    for (unsigned i=0;i<doc->count;++i)
        if (!strcmp(doc->entries[i].table,table) && !strcmp(doc->entries[i].key,key))
            return &doc->entries[i];
    return NULL;
}
static RuntimeTomlSlice value_slice(const TomlDocument *doc,const TomlEntry *entry)
{
    RuntimeTomlSlice value={doc->bytes+entry->value_begin,(unsigned)(entry->value_end-entry->value_begin)};
    return value;
}
int Toml_Bool(const TomlDocument *doc,const TomlEntry *entry,int *value)
{
    if (!doc || !entry || !value) return 0;
    RuntimeTomlSlice v=value_slice(doc,entry);
    if (v.length==4 && !memcmp(v.data,"true",4)) {*value=1;return 1;}
    if (v.length==5 && !memcmp(v.data,"false",5)) {*value=0;return 1;}
    return 0;
}
int Toml_Integer(const TomlDocument *doc,const TomlEntry *entry,int *value)
{
    if (!doc || !entry || !value) return 0;
    return toml_parse_s32_(value_slice(doc,entry),value);
}
int Toml_Percent(const TomlDocument *doc,const TomlEntry *entry,int *value)
{
    if (!doc || !entry || !value) return 0;
    RuntimeTomlSlice v=value_slice(doc,entry);
    unsigned whole=0,fraction=0,digits=0,places=0,index=0;
    /* 百分比按十进制逐位计算：12.50直接得到1250，不经过浮点舍入。 */
    for (;index<v.length && v.data[index]>='0' && v.data[index]<='9';++index) {
        if (++digits>3) return 0;
        whole=whole*10u+(unsigned)(v.data[index]-'0');
    }
    if (!digits || (digits>1 && v.data[0]=='0')) return 0;
    if (index<v.length && v.data[index]=='.') {
        ++index;
        for (;index<v.length && v.data[index]>='0' && v.data[index]<='9';++index) {
            if (++places>2) return 0;
            fraction=fraction*10u+(unsigned)(v.data[index]-'0');
        }
        if (!places) return 0;
    }
    if (index!=v.length || whole>100) return 0;
    if (places==1) fraction*=10;
    if (whole*100u+fraction>10000u) return 0;
    *value=(int)(whole*100u+fraction);
    return 1;
}
int Toml_String(const TomlDocument *doc,const TomlEntry *entry,char *output,size_t capacity)
{
    if (!doc || !entry || !output || !capacity) return 0;
    RuntimeTomlSlice v=value_slice(doc,entry);
    if (v.length<2 || v.data[0]!='"' || v.data[v.length-1]!='"') return 0;
    size_t written=0;
    /* 与Castle的基本字符串读取一致，检查转义和容量后才写入输出。 */
    for (unsigned i=1;i+1<v.length;++i) {
        char ch=v.data[i];
        if (ch=='\\') {
            if (++i+1>=v.length) return 0;
            ch=v.data[i];
            if (ch=='n') ch='\n';
            else if (ch=='r') ch='\r';
            else if (ch=='t') ch='\t';
            else if (ch!='\\' && ch!='"') return 0;
        } else if (ch=='"' || (unsigned char)ch<32) return 0;
        if (written+1>=capacity) return 0;
        output[written++]=ch;
    }
    output[written]=0;
    return 1;
}
int Toml_Parse(TomlDocument *doc,const char *bytes,size_t size)
{
    if (!doc) return 0;
    memset(doc,0,sizeof *doc);
    if (!bytes || !size || size>=TOML_CAPACITY) return fail(doc,0,"配置为空或超过64KiB限制");
    if (memchr(bytes,0,size) || !toml_utf8_valid_((const unsigned char *)bytes,(unsigned)size))
        return fail(doc,0,"配置不是有效UTF-8文本");
    if (size>=3 && !memcmp(bytes,"\xEF\xBB\xBF",3)) return fail(doc,0,"配置必须使用UTF-8无BOM");
    memcpy(doc->bytes,bytes,size);doc->size=size;
    char table[96]="",tables[128][96];unsigned table_count=0,line_number=0;
    size_t position=0;
    while (position<size) {
        size_t begin=position; ++line_number;
        while (position<size && bytes[position]!='\n') ++position;
        RuntimeTomlSlice line={doc->bytes+begin,(unsigned)(position-begin)};
        if (position<size) ++position;
        line.length=toml_content_length_(line.data,line.length);line=toml_trim_(line);
        if (!line.length) continue;
        if (line.data[0]=='[') {
            if (line.length<3 || line.data[line.length-1]!=']' || line.data[1]=='[')
                return fail(doc,line_number,"只支持普通配置表，不支持数组表");
            RuntimeTomlSlice name={line.data+1,line.length-2};name=toml_trim_(name);
            if (!toml_bare_name_valid_(name) || name.length>=sizeof table ||
                name.data[0]=='.' || name.data[name.length-1]=='.')
                return fail(doc,line_number,"配置表名无效");
            memcpy(table,name.data,name.length);table[name.length]=0;
            if (strstr(table,"..")) return fail(doc,line_number,"配置表名含空分组");
            for (unsigned i=0;i<table_count;++i)
                if (!strcmp(tables[i],table)) return fail(doc,line_number,"配置表重复定义");
            if (table_count>=128) return fail(doc,line_number,"配置表数量过多");
            strcpy(tables[table_count++],table);continue;
        }
        const char *equal=memchr(line.data,'=',line.length);
        if (!equal) return fail(doc,line_number,"配置项缺少等号");
        RuntimeTomlSlice key={line.data,(unsigned)(equal-line.data)};
        RuntimeTomlSlice value={equal+1,line.length-(unsigned)(equal+1-line.data)};
        key=toml_trim_(key);value=toml_trim_(value);
        if (!toml_bare_name_valid_(key) || key.length>=sizeof doc->entries[0].key ||
            memchr(key.data,'.',key.length) || !value.length)
            return fail(doc,line_number,"配置键名或值无效");
        char key_text[48];memcpy(key_text,key.data,key.length);key_text[key.length]=0;
        if (Toml_Find(doc,table,key_text)) return fail(doc,line_number,"配置键重复定义");
        if (doc->count>=TOML_MAX_ENTRIES) return fail(doc,line_number,"配置项数量过多");
        TomlEntry *entry=&doc->entries[doc->count++];
        strcpy(entry->table,table);strcpy(entry->key,key_text);entry->line=line_number;
        entry->value_begin=(size_t)(value.data-doc->bytes);entry->value_end=entry->value_begin+value.length;
        int scalar;char string[TOML_CAPACITY];
        if (!Toml_Bool(doc,entry,&scalar) && !Toml_Integer(doc,entry,&scalar) &&
            !Toml_Percent(doc,entry,&scalar) && !Toml_String(doc,entry,string,sizeof string))
            return fail(doc,line_number,"值必须是布尔、十进制整数、百分比或双引号字符串");
    }
    return 1;
}
int Toml_Update(const TomlDocument *doc,const char *table,const char *key,
                const char *literal,char *output,size_t capacity,size_t *size)
{
    if (!doc || !table || !key || !literal || !output || !size) return 0;
    const TomlEntry *entry=Toml_Find(doc,table,key);
    size_t written=0,length=strlen(literal);
    if (entry) {
        size_t total=doc->size-(entry->value_end-entry->value_begin)+length;
        if (total+1>capacity) return 0;
        memcpy(output,doc->bytes,entry->value_begin);written=entry->value_begin;
        memcpy(output+written,literal,length);written+=length;
        memcpy(output+written,doc->bytes+entry->value_end,doc->size-entry->value_end);
        written+=doc->size-entry->value_end;
    } else {
        /* 缺失项插入它所属表的末尾；不存在的表才追加，不能制造重复表。 */
        size_t insert=doc->size;int found=0;
        size_t p=0;
        while (p<doc->size) {
            size_t begin=p;
            while (p<doc->size && doc->bytes[p]!='\n') ++p;
            RuntimeTomlSlice line={doc->bytes+begin,(unsigned)(p-begin)};
            if (p<doc->size) ++p;
            line.length=toml_content_length_(line.data,line.length);line=toml_trim_(line);
            if (line.length>=3 && line.data[0]=='[') {
                if (found) {insert=begin;break;}
                RuntimeTomlSlice name={line.data+1,line.length-2};name=toml_trim_(name);
                found=name.length==strlen(table) && !memcmp(name.data,table,name.length);
            }
        }
        char addition[512];int n;
        n=found ? snprintf(addition,sizeof addition,"%s%s = %s\r\n",
            insert && doc->bytes[insert-1]!='\n' ? "\r\n":"",key,literal) :
            snprintf(addition,sizeof addition,"%s[%s]\r\n%s = %s\r\n",
            insert && doc->bytes[insert-1]!='\n' ? "\r\n":"",table,key,literal);
        if (n<0 || (size_t)n>=sizeof addition || doc->size+(size_t)n+1>capacity) return 0;
        memcpy(output,doc->bytes,insert);written=insert;
        memcpy(output+written,addition,(size_t)n);written+=(size_t)n;
        memcpy(output+written,doc->bytes+insert,doc->size-insert);written+=doc->size-insert;
    }
    output[written]=0;*size=written;return 1;
}
