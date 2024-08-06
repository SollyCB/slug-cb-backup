#include "hash_map.h"
#include "allocator.h"

bool fn_hash_map_insert_hash(hash_map *map, uint64 hash, void *elem, int elem_width)
{
    int g_idx = (hash & (map->cap - 1));
    g_idx    -= g_idx & 15;

    uint8  *data   = map->data;
    int  cap    = map->cap;
    int  stride = map->kv_stride;

    int  tz;
    uint16  mask;
    uint64 *phash;

    __m128i a;
    int inc = 0;
    while(inc < cap) {
        a    = _mm_load_si128((__m128i*)(data + g_idx));
        mask = _mm_movemask_epi8(a);

        if (!mask) {
            inc   += 16;
            g_idx += inc;
            g_idx &= cap - 1;
            continue;
        } else {
            tz = ctz16(mask);

            uint8 top7 = (hash >> 57) & HASH_MAP_FULL;
            data[g_idx + tz] = 0x0 | top7;

            phash  = (uint64*)(data + cap + (stride * (tz + g_idx)));
           *phash  =  hash;
            memcpy(data + cap + (stride * (tz + g_idx)) + 8, elem, elem_width);

            map->remaining -= 1;
            return true;
        }
    }
    return false;
}

void fn_hash_map_if_full(hash_map *map, int elem_width)
{
    assert(map->cap * 2 > map->cap && "mul overflow");
    log_print_error_if(!map->resize, "hash map overflow but resize is false");

    uint8 *old_data = map->data;
    int old_cap  = map->cap;

    map->cap      *= 2;
    map->data      = allocate(map->alloc, map->cap + map->cap * map->kv_stride);
    map->remaining = ((map->cap + 1) / 8) * 7;

    memset(map->data, HASH_MAP_EMPTY, map->cap);

    int stride = map->kv_stride;

    int  pc;
    int  tz;
    uint16  mask;
    uint64 *phash;

    __m128i a;
    for(int i = 0; i < old_cap; i += 16) {
        a    = _mm_load_si128((__m128i*)(old_data + i));
        mask = ~(_mm_movemask_epi8(a));

        pc = pop_count16(mask);
        for(int j = 0; j < pc; ++j) {
            tz    = ctz16(mask);
            mask ^= 1 << tz;

            phash = (uint64*)(old_data + old_cap + (stride * (tz + i)));
            assert(fn_hash_map_insert_hash(map, *phash, (uint8*)phash + 8, elem_width) && "hash map grow failure");
        }
    }
    free(old_data);
}
void* fn_hash_map_find_hash(hash_map *map, uint64 hash)
{
    uint8 top7   = (hash >> 57) & HASH_MAP_FULL;
    int g_idx = hash & (map->cap - 1);
    g_idx -= g_idx & 15;

    uint8 *data   = map->data;
    int stride = map->kv_stride;
    int cap    = map->cap;

    int  pc;
    int  tz;
    uint16  mask;
    uint64 *phash;

    __m128i a;
    __m128i b = _mm_set1_epi8(top7);

    int inc = 0;
    while(inc < cap) {
        a    = _mm_load_si128((__m128i*)(data + g_idx));
        a    = _mm_cmpeq_epi8(a, b);

        mask = _mm_movemask_epi8(a);
        pc   = pop_count16(mask);

        for(int i = 0; i < pc; ++i) {
            tz    = ctz16(mask);
            mask ^= 1 << tz;
            phash = (uint64*)(data + cap + (stride * (tz + g_idx)));
            if (*phash == hash)
                return (uint8*)phash + 8;
        }
        g_idx += 16;
        g_idx &= cap - 1;
        inc   += 16;
    }
    return NULL;
}

void* fn_hash_map_delete_hash(hash_map *map, uint64 hash)
{
    uint8 top7   = (hash >> 57) & HASH_MAP_FULL;
    int g_idx = hash & (map->cap - 1);
    g_idx    -= g_idx & 15;

    __m128i a;
    __m128i b = _mm_set1_epi8(top7);

    uint8 *data   = map->data;
    int stride = map->kv_stride;
    int cap    = map->cap;

    int  pc;
    int  tz;
    uint16  mask;
    uint64 *phash;

    int inc = 0;
    while(inc < cap) {
        a    = _mm_load_si128((__m128i*)(data + g_idx));
        a    = _mm_cmpeq_epi8(a, b);

        mask = _mm_movemask_epi8(a);
        pc   = pop_count16(mask);

        for(int i = 0; i < pc; ++i) {
            tz    = ctz16(mask);
            mask ^= 1 << tz;
            phash = (uint64*)(data + cap + (stride * (tz + g_idx)));
            if (*phash == hash) {
                data[g_idx + tz] = HASH_MAP_DELETED;
                return (uint8*)phash + 8;
            }
        }
        g_idx += 16;
        g_idx &= cap - 1;
        inc   += 16;
    }
    return NULL;
}

