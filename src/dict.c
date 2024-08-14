#include "dict.h"

// dictionary will resize itself when 7/8ths full, take this into account when setting cap
void new_dict_fn(u32 cap, u16 width, u16 flags, allocator *alloc, dictionary *h)
{
    assert(cap % 16 == 0 && "cap must be a multiple of 16");
    h->width = width;
    h->flags = flags;
    h->cap   = cap;
    h->rem   = cap / 8 * 7;
    h->alloc = alloc;
    h->data  = allocate(alloc, (align(width, 8) + 8 + 1) * cap);
}

dict_iter new_dict_iter(dictionary *dict)
{
    dict_iter it;
    it.dict = dict;
    it.i = 0;
    return it;
}

struct dict_kv dict_iter_next(dict_iter *it)
{
    struct dict_kv ret = {};

    __m128i a = _mm_load_si128((__m128i*)((u8*)it->dict->data + it->i - (it->i % 16)));
    u16  mask = _mm_movemask_epi8(a) >> (it->i % 16);

    while(!mask) {
        it->i = align(it->i+1, 16);

        if (it->i >= it->dict->cap)
            return ret;

        a    = _mm_load_si128((__m128i*)((u8*)it->dict->data + it->i));
        mask = _mm_movemask_epi8(a);
    }
    it->i += ctz16(mask); // groups fill up from i=0, but some might have been deleted

    ret.key = *(u64*)((u8*)it->dict->data + it->dict->cap + (align(it->dict->width, 8) + 8) * it->i);
    ret.val = (u8*)it->dict->data + it->dict->cap + (align(it->dict->width, 8) + 8) * it->i + 8;

    it->i += 1;

    return ret;
}

void dict_expand(dictionary *dict)
{
    dictionary old = *dict;
    new_dict_fn(dict->cap * 2, dict->width, dict->flags, dict->alloc, dict);

    dict_iter iter = new_dict_iter(&old);
    while(1) {
        struct dict_kv it = dict_iter_next(&iter);

        if (!it.val)
            break;

        dict_add_hash(dict, it.key, it.val);
    }
}

enum {
    DICT_FULL  = 0b10000000,
    DICT_EMPTY = 0b00000000,
};

struct dict_kv dict_add_hash(dictionary *dict, u64 hash, void *value)
{
    if (!dict->rem) {
        if (!(dict->flags & DICT_RESIZE))
            assert(false && "hash map overflowed: no slots remaining but resize flag not set");
        dict_expand(dict);
    }

    u8 *g   = dict->data;
    u32 i   = (hash % dict->cap) - ((hash % dict->cap) % 16);
    u32 inc = 16;
    while(inc < dict->cap) {
        __m128i a = _mm_load_si128((__m128i*)(g + i));
        u16  mask = ~_mm_movemask_epi8(a);

        if (!mask) {
            i    = (i+inc) % dict->cap;
            inc += 16;
            continue;
        }

        u32 w   =  align(dict->width, 8) + 8;
        u32 tz  =  ctz16(mask) & maxif(mask);
        g[i+tz] = (hash >> 57) | DICT_FULL;

        memcpy(g + dict->cap + (w * (i + tz)) + 0, &hash, 8);
        memcpy(g + dict->cap + (w * (i + tz)) + 8, value, dict->width);
        dict->rem -= 1;

        struct dict_kv kv;
        kv.key = hash;
        kv.val = g + dict->cap + (w * (i + tz)) + 8;
        return kv;
    }

    assert(false && "failed to add hash");
    return (struct dict_kv) {};
}

struct dict_kv dict_find_hash(dictionary *dict, u64 hash)
{
    u32 i     = (hash % dict->cap) - ((hash % dict->cap) % 16);
    u8  top7  = (hash >> 57) | DICT_FULL;
    __m128i a = _mm_set1_epi8(top7);

    for(u32 inc = 16; inc < dict->cap; inc += 16) {
        __m128i b = _mm_load_si128((__m128i*)((u8*)dict->data + i));
        __m128i c = _mm_cmpeq_epi8(a, b);
        u16  mask = _mm_movemask_epi8(c);
        u32  pc   = popcnt16(mask);
        for(u32 j=0; j < pc; ++j) {
            u32 tz = ctz16(mask);
            mask &= ~(1<<tz);

            u64 h = *(u64*)((u8*)dict->data + dict->cap + (align(dict->width, 8) + 8) * (i + tz));
            if (h != hash)
                continue;

            struct dict_kv kv;
            kv.key = h;
            kv.val = (u8*)dict->data + dict->cap + (align(dict->width, 8) + 8) * (i + tz) + 8;
            return kv;
        }
        i = (i+inc) % dict->cap;
    }
    return (struct dict_kv) {};
}
