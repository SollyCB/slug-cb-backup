#ifndef SOL_HASH_MAP_H_INCLUDE_GUARD_
#define SOL_HASH_MAP_H_INCLUDE_GUARD_

#include "external/wyhash.h"
#include "defs.h"
#include "assert.h"
#include "allocator.h"
#include <immintrin.h>

#define HASH_MAP_FULL        0b01111111
#define HASH_MAP_EMPTY       0b11111111
#define HASH_MAP_DELETED     0b10000000
#define HASH_MAP_GROUP_WIDTH 16

typedef struct {
    int cap;
    int remaining;
    int kv_stride;
    uint8 *data;
    bool resize;
    allocator *alloc;
} hash_map;

bool  fn_hash_map_insert_hash(hash_map *map, uint64 hash, void *elem, int elem_width);
void  fn_hash_map_if_full(hash_map *map, int elem_width);
void* fn_hash_map_find_hash(hash_map *map, uint64 hash);
void* fn_hash_map_delete_hash(hash_map *map, uint64 hash);

                        /* Frontend Functions */
static inline uint64 get_hash(int byte_len, void *bytes) {
    return wyhash(bytes, byte_len, 0, _wyp);
}

static inline hash_map fn_new_hash_map(int cap, int elem_width, bool resize, allocator *alloc) {
    hash_map ret  = {};
    ret.cap       = align(cap, 16);
    ret.remaining = ((cap + 1) / 8) * 7;
    ret.kv_stride = align(8 + elem_width, 16);
    ret.data      = allocate(alloc, ret.cap + ret.cap * ret.kv_stride);
    ret.alloc = alloc;
    ret.resize = resize;
    memset(ret.data, HASH_MAP_EMPTY, ret.cap);
    return ret;
}

static inline void fn_free_hash_map(hash_map *map) {
    deallocate(map->alloc, map->data);
}

static inline bool fn_hash_map_insert(hash_map *map, int byte_len, void *key, void *elem, int elem_width) {
    if (map->remaining == 0)
        fn_hash_map_if_full(map, elem_width);

    uint64 hash = wyhash(key, byte_len, 0, _wyp);
    return fn_hash_map_insert_hash(map, hash, elem, elem_width);
}
static inline bool fn_hash_map_insert_str(hash_map *map, const char *key, void *elem, int elem_width) {
    if (map->remaining == 0)
        fn_hash_map_if_full(map, elem_width);

    uint64 hash = wyhash(key, strlen(key), 0, _wyp);
    return fn_hash_map_insert_hash(map, hash, elem, elem_width);
}

static inline void* fn_hash_map_find(hash_map *map, int byte_len, void *bytes) {
    uint64 hash = wyhash(bytes, byte_len, 0, _wyp);
    return fn_hash_map_find_hash(map, hash);
}
static inline void* fn_hash_map_find_str(hash_map *map, const char* key) {
    uint64 hash = wyhash(key, strlen(key), 0, _wyp);
    return fn_hash_map_find_hash(map, hash);
}

static inline void* fn_hash_map_delete(hash_map *map, int byte_len, void *bytes) {
    uint64 hash = wyhash(bytes, byte_len, 0, _wyp);
    return fn_hash_map_delete_hash(map, hash);
}
static inline void* fn_hash_map_delete_str(hash_map *map, const char* key) {
    uint64 hash = wyhash(key, strlen(key), 0, _wyp);
    return fn_hash_map_delete_hash(map, hash);
}

                        /* Frontend Macros */
#define new_dynamic_hash_map(cap, type, alloc) fn_new_hash_map(cap, sizeof(type), true, alloc)
#define new_static_hash_map(cap, type, alloc) fn_new_hash_map(cap, sizeof(type), false, alloc)
#define free_hash_map(p_map) fn_free_hash_map(p_map)

#define hash_map_insert(p_map, p_key, p_value) fn_hash_map_insert(p_map, sizeof(*(p_key)), p_key, p_value, sizeof(*p_value))
#define hash_map_insert_str(p_map, str_key, p_value) fn_hash_map_insert_str(p_map, str_key, p_value, sizeof(*p_value))

#define hash_map_find(p_map, p_key) fn_hash_map_find(p_map, sizeof(*(p_key)), p_key)
#define hash_map_find_str(p_map, str_key) fn_hash_map_find_str(p_map, str_key)

#define find_hash(map, hash) fn_hash_map_find_hash(map, hash)

#define hash_map_delete(p_map, p_key) fn_hash_map_delete(p_map, sizeof(*(key)), p_key)
#define hash_map_delete_str(p_map, str_key) fn_hash_map_delete_str(p_map, str_key)

#endif // include guard
