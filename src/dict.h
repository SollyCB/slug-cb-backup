#ifndef DICT_H
#define DICT_H

#include "external/wyhash.h"
#include "defs.h"
#include "string.h"

static inline u64 hash_bytes(u32 len, void *data)
{
    return wyhash(data, len, 0, _wyp);
}

enum {
    DICT_RESIZE = 0x01,
};

typedef struct dictionary {
    u16        width;
    u16        flags;
    u32        cap;
    u32        rem;
    void      *data;
    allocator *alloc;
} dictionary;

typedef struct dict_iter {
    dictionary *dict;
    u32      i;
} dict_iter;

struct dict_kv {
    u64   key;
    void *val;
};

// dictionary will resize itself when 7/8ths full, take this into account when setting cap
void           new_dict_fn(u32 cap, u16 width, u16 flags, allocator *alloc, dictionary *dict);
dict_iter      new_dict_iter(dictionary *dict);
struct dict_kv dict_add_hash(dictionary *dict, u64 hash, void *value);
struct dict_kv dict_iter_next(dict_iter *it);
struct dict_kv dict_find_hash(dictionary *dict, u64 hash);

// dictionary will resize itself when 7/8ths full, take this into account when setting cap
#define new_dict(cap, type, flags, alloc, dict) new_dict_fn(cap, sizeof(type), flags, alloc, dict)

struct dict_kv dict_add(dictionary *dict, string key, void *value)
{
    u64 hash = hash_bytes(key.len, (void*)key.cstr);
    return dict_add_hash(dict, hash, value);
}
struct dict_kv dict_find(dictionary *dict, string key)
{
    u64 hash = hash_bytes(key.len, (void*)key.cstr);
    return dict_find_hash(dict, hash);
}

#endif // include guard
