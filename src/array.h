#ifndef SOL_ARRAY_H_INCLUDE_GUARD_
#define SOL_ARRAY_H_INCLUDE_GUARD_

#include "defs.h"
#include "allocator.h"

#define ARRAY_METADATA_WIDTH 4
#define ARRAY_METADATA_SIZE (sizeof(uint64) * ARRAY_METADATA_WIDTH)

// Backend
static inline void* fn_new_array(uint64 cap, uint64 width, allocator *alloc) {
    uint64 *ret = allocate(alloc, cap * width + ARRAY_METADATA_SIZE);
    ret[0] = width * cap;
    ret[1] = 0;
    ret[2] = width;
    ret[3] = (uint64)alloc;
    ret += ARRAY_METADATA_WIDTH;
    assert(((uint64)ret % 16) == 0); // align for SIMD
    return ret;
}

static inline void* fn_realloc_array(void *a) {
    uint64 *array = (uint64*)a - ARRAY_METADATA_WIDTH;
    if (array[0] <= array[1] * array[2]) {
        array = reallocate_with_old_size((allocator*)(array[3]),
                array, array[0] + ARRAY_METADATA_SIZE, array[0] * 2 + ARRAY_METADATA_SIZE);
        assert(array);
        array[0] *= 2;
    }
    assert(((uint64)(array + ARRAY_METADATA_WIDTH) % 16) == 0); // align for SIMD
    return array + ARRAY_METADATA_WIDTH;
}

static inline size_t array_cap(void *array) {
    return ((uint64*)array - 4)[0] / ((uint64*)array - 4)[2];
}

static inline size_t array_len(void *array) {
    return ((uint64*)array - 4)[1];
}

static inline void array_set_len(void *array, size_t len) {
    ((uint64*)array - 4)[1] = len;
}

static inline size_t array_elem_width(void *array)
{
    return ((uint64*)array-4)[2];
}

static inline size_t array_byte_len(void *a)
{
    return array_elem_width(a)*array_len(a);
}

static inline struct allocation array_allocation(void *a)
{
    struct allocation ret;
    ret.size = array_byte_len(a)+ARRAY_METADATA_SIZE;
    ret.data = (uint64*)a-4;
    return ret;
}

static inline void array_inc(void *array) {
   ((uint64*)array - 4)[1] += 1;
}

static inline void array_dec(void *array) {
   ((uint64*)array - 4)[1] -= 1;
}

static inline void free_array(void *a) {
    uint64 *array = (uint64*)a - 4;
    allocator *alloc = (allocator*)(array[3]);
    deallocate(alloc, array);
}

static inline void* load_array(const char *fname, allocator *alloc)
{
    struct file f = file_read_bin_all(fname,alloc);
    uint64 *ret = (uint64*)f.data;
    ret[3] = (uint64)alloc;
    ret += ARRAY_METADATA_WIDTH;
    assert(((uint64)ret % 16) == 0); // align for SIMD
    return ret;
}

static inline void store_array(const char *fname, void *a)
{
    FILE *f = fopen(fname,"wb");
    struct allocation alloc = array_allocation(a);
    fwrite(alloc.data,1,alloc.size,f);
    fclose(f);
}

// Frontend
#define new_array(cap, type, alloc) fn_new_array(cap, sizeof(type), alloc)
#define array_last(array) (array[array_len(array)-1])
#define array_add(array, elem) \
    array = fn_realloc_array(array); \
    array[array_len(array)] = elem; \
    array_inc(array)

#endif // include guard
