#include "string.h"

static void realloc_string_buffer(string_buffer *buf, uint64 newsz);

string_buffer new_string_buffer(uint64 cap, allocator *alloc)
{
    string_buffer ret;
    ret.used = 0;
    ret.cap = align(cap, 16);
    ret.data = allocate(alloc, ret.cap);
    ret.alloc = alloc;
    return ret;
}

void free_string_buffer(string_buffer *buf)
{
    deallocate(buf->alloc, buf->data);
    buf->cap = 0;
    buf->used = 0;
}

string string_buffer_get_string(string_buffer *buf, string *str)
{
    if (buf->used + str->len + 1 > buf->cap) {
        log_print_error("string_buffer overflow");
        return (string){};
    }

    memcpy(buf->data + buf->used, str->cstr, str->len);
    string ret;
    ret.len = str->len;
    ret.cstr = buf->data + buf->used;

    buf->used += str->len + 1;
    buf->data[buf->used-1] = '\0';

    return ret;
}

string string_buffer_get_string_from_cstring(string_buffer *buf, const char *cstr)
{
    uint64 len = strlen(cstr);
    if (buf->used + len + 1 > buf->cap) {
        log_print_error("string_buffer overflow");
        return (string){};
    }

    memcpy(buf->data + buf->used, cstr, len);
    string ret;
    ret.len = len;
    ret.cstr = buf->data + buf->used;

    buf->used += len + 1;
    buf->data[buf->used-1] = '\0';

    return ret;
}

static void realloc_string_buffer(string_buffer *buf, uint64 newsz)
{
    newsz = align(newsz, 16);
    buf->cap  = newsz;
    buf->data = reallocate_with_old_size(buf->alloc, buf->data, buf->used, newsz);
}
