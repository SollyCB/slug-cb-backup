#ifndef SOL_STRING_H_INCLUDE_GUARD_
#define SOL_STRING_H_INCLUDE_GUARD_

#include "defs.h"
#include "allocator.h"
#include "ascii.h"

typedef struct {
    const char *cstr;
    uint64 len;
} string;

static inline string cstr_to_string(const char *cstr) {
    return (string){cstr, strlen(cstr)};
}

// **THIS SAME COMMENT APPLIES TO THE ABOVE 'string'**
// Debating removing typedef. I used to more easily justify typedefs
// as being structs which are used in a sort of dynamic manner (a good
// symbol begin a verb in the name i.e. '_builder') but the more I use
// C, the I like the struct keyword to differentiate them from basic types.
typedef struct {
    uint32 used;
    uint32 cap;
    char *data;
} string_builder;

static inline string_builder sb_new(uint32 size, char *data)
{
    return (string_builder){0, size, data};
}

static inline void sb_null_term(string_builder *sb)
{
    assert(sb->used+1 <= sb->cap);
    sb->data[sb->used] = 0;
    sb->used++;
}

static inline void sb_add(string_builder *sb, uint32 size, const char *data)
{
    assert(sb->used + size <= sb->cap);
    memcpy(sb->data + sb->used, data, size);
    sb->used += size;
}

static inline void sb_addc(string_builder *sb, const char c)
{
    assert(sb->used + 1 <= sb->cap);
    sb->data[sb->used] = c;
    sb->used++;
}

static inline void sb_adduint(string_builder *sb, uint i)
{
    assert(i < 1000000);
    char nb[8];
    uint nl = uint_to_ascii(i,nb);
    assert(sb->used + nl <= sb->cap);
    for(uint j=0; j < nl; ++j)
        sb->data[sb->used+j] = nb[j];
    // memcpy(sb->data+sb->used,nb,nl);
    sb->used += nl;
}

static inline void sb_addnl(string_builder *sb)
{
    assert(sb->used + 1 <= sb->cap);
    sb->data[sb->used] = '\n';
    sb->used++;
}

static inline void sb_add_if(string_builder *sb, uint32 size, const char *data, bool b)
{
    assert(sb->used + size <= sb->cap);
    if (b) {
        memcpy(sb->data + sb->used, data, size);
        sb->used += size;
    }
}

static inline void sb_addc_if(string_builder *sb, const char c, bool b)
{
    assert(sb->used + 1 <= sb->cap);
    sb->data[sb->used] = c;
    sb->used += b;
}

static inline void sb_addnl_if(string_builder *sb, bool b)
{
    assert(sb->used + 1 <= sb->cap);
    sb->data[sb->used] = '\n';
    sb->used += b;
}

static inline void sb_endl_if(string_builder *sb, bool b) {
    assert(sb->used + 2 <= sb->cap);
    sb->data[sb->used] = ';';
    sb->data[sb->used+1] = '\n';
    sb->used += b + b;
}

static inline void sb_addarr(string_builder *sb, uint32 before_size, const char *before,
        uint32 after_size, const char *after, uint32 n)
{
    sb_add(sb, before_size, before);
    sb_addc(sb, '[');
    char nb[8];
    uint nl = uint_to_ascii(n, nb);
    sb_add(sb, nl, nb);
    sb_addc(sb, ']');
    sb_add(sb, after_size, after);
}

static inline void sb_insertnum(string_builder *sb, uint32 before_size, const char *before,
        uint32 after_size, const char *after, uint32 n)
{
    sb_add(sb, before_size, before);
    char nb[8];
    uint nl = uint_to_ascii(n, nb);
    sb_add(sb, nl, nb);
    sb_add(sb, after_size, after);
}

static inline void sb_addarr_if(string_builder *sb, uint32 before_size, const char *before,
        uint32 after_size, const char *after, uint32 n, bool b)
{
    sb_add_if(sb, before_size, before, b);
    sb_addc_if(sb, '[', b);
    char nb[8];
    uint nl = uint_to_ascii(n, nb);
    sb_add_if(sb, nl, nb, b);
    sb_addc_if(sb, ']', b);
    sb_add_if(sb, after_size, after, b);
}

static inline void sb_close_arr_and_endl_if(string_builder *sb, bool b) {
    assert(sb->used + 3 <= sb->cap);
    sb->data[sb->used+0] = ']';
    sb->data[sb->used+1] = ';';
    sb->data[sb->used+2] = '\n';
    sb->used += b*3;
}

static inline void sb_insertnum_if(string_builder *sb, uint32 before_size, const char *before,
        uint32 after_size, const char *after, uint32 n, bool b)
{
    sb_add_if(sb, before_size, before, b);
    char nb[8];
    uint nl = uint_to_ascii(n, nb);
    sb_add_if(sb, nl, nb, b);
    sb_add_if(sb, after_size, after, b);
}

static inline void sb_add_digit_if(string_builder *sb, uint n, bool b)
{
    assert(sb->used+1 <= sb->cap);
    sb->data[sb->used] &= zero_if(b);
    sb->data[sb->used] += (n + '0') & max_if(b);
    sb->used += b;
}

static inline void sb_replace(string_builder *sb, uint i, uint len, const char *str)
{
    assert(i+len < sb->used);
    memcpy(sb->data+i,str,len);
}

static inline void sb_replace_c(string_builder *sb, uint i, char c)
{
    assert(i < sb->used);
    sb->data[i] = c;
}

static inline void sb_replace_digit(string_builder *sb, uint i, int n)
{
    assert(i < sb->used);
    sb->data[i] = n + '0';
}

static inline void sb_replace_digit_if(string_builder *sb, uint i, int n, bool b)
{
    assert(i < sb->used || !b);
    sb->data[i] &= zero_if(b);
    sb->data[i] += (n + '0') & max_if(b);
}

static inline void sb_replace_uint(string_builder *sb, uint i, int u)
{
    assert(i < sb->used);
    assert(u < 1000000);
    char nb[8];
    uint nl = uint_to_ascii(u,nb);
    assert(sb->used + nl <= sb->cap);
    for(uint j=0; j < nl; ++j)
        sb->data[sb->used+j] = nb[j];
    // memcpy(sb->data+sb->used,nb,nl);
}

typedef struct {
    char *data;
    uint64_t used;
    uint64_t cap;
    allocator *alloc;
} string_buffer;

string_buffer new_string_buffer(uint64 cap, allocator *alloc);
void free_string_buffer(string_buffer *buf);

string string_buffer_get_string(string_buffer *buf, string *str);
string string_buffer_get_string_from_cstring(string_buffer *buf, const char *cstr);

#endif // include guard
