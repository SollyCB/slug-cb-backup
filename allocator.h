#ifndef SOL_ALLOCATOR_H_INCLUDE_GUARD_
#define SOL_ALLOCATOR_H_INCLUDE_GUARD_

#include "defs.h"
#include "assert.h"

GCC_IGNORE_WARNINGS_BEGIN
#define tlsf_assert assert
#include "external/tlsf.h"
GCC_IGNORE_WARNINGS_END

#define ALLOCATOR_ALIGNMENT 16

#define alloc_align(sz) align(sz, ALLOCATOR_ALIGNMENT)
#define alloc_align_type(t) align(sizeof(t), ALLOCATOR_ALIGNMENT)

/*
   @Todo: This file and its corresponding source are really old and have little
   ugly silly bits and comments. I need to move in the slightly more refined versions
   from sol.h...
*/

struct allocation {
    void *data;
    size_t size;
};

typedef struct {
    uint64 cap;
    uint64 used;
    uint8 *mem;
    void *tlsf_handle;
} heap_allocator;

typedef struct {
    uint64 cap;
    uint64 used;
    uint8 *mem;
} linear_allocator;

typedef enum {
    ALLOCATOR_HEAP_BIT        = 0x01,
    ALLOCATOR_LINEAR_BIT      = 0x02,
    ALLOCATOR_DO_NOT_FREE_BIT = 0x04,

    ALLOCATOR_TYPE_BITS = ALLOCATOR_HEAP_BIT | ALLOCATOR_LINEAR_BIT,
} allocator_flag_bits;

typedef struct allocator allocator;

struct allocator {
    uint32 flags;
    void* (*fpn_allocate)(allocator*, size_t);
    void* (*fpn_reallocate)(allocator*, void*, size_t);
    void  (*fpn_deallocate)(allocator*, void*);
    void* (*fpn_reallocate_with_old_size)(allocator *, void *, size_t, size_t);
    union {
        heap_allocator heap;
        linear_allocator linear;
    };
};

struct allocators {
    allocator *temp;
    allocator *persistent;
};

// Leave 'buffer' null to allocate cap from standard allocator.
allocator new_allocator(size_t cap, void *buffer, allocator_flag_bits type);
void free_allocator(allocator *alloc);

#define new_heap_allocator(cap, buffer) new_allocator(cap, buffer, ALLOCATOR_HEAP_BIT)
#define new_linear_allocator(cap, buffer) new_allocator(cap, buffer, ALLOCATOR_LINEAR_BIT)

static inline void *allocate(allocator *alloc, size_t size) {
    return alloc->fpn_allocate(alloc, size);
}

static inline void *reallocate(allocator *alloc, void *ptr, size_t new_size) {
    return alloc->fpn_reallocate(alloc, ptr, new_size);
}

static inline void *reallocate_with_old_size(allocator *alloc, void *ptr, size_t old_size, size_t new_size) {
    return alloc->fpn_reallocate_with_old_size(alloc, ptr, old_size, new_size);
}

static inline void deallocate(allocator *alloc, void *ptr) {
    return alloc->fpn_deallocate(alloc, ptr);
}

static inline void *allocate_and_zero(allocator *alloc, size_t size) {
    void *ret = alloc->fpn_allocate(alloc, size);
    memset(ret, 0, alloc_align(size));
    return ret;
}

#define sallocate(alloc, type, count) allocate(alloc, sizeof(type) * (count))
#define sreallocate(alloc, ptr, type, count) reallocate(alloc, ptr, sizeof(type) * count)
#define sallocate_unaligned(alloc, type, count) allocate_unaligned(alloc, sizeof(type), sizeof(type) * count)

static inline size_t allocator_used(allocator *alloc) {
    switch(alloc->flags & ALLOCATOR_TYPE_BITS) {
        case ALLOCATOR_HEAP_BIT:
        return alloc->heap.used;
        case ALLOCATOR_LINEAR_BIT:
        return alloc->linear.used;
        default:
        assert(false && "Invalid allocator flags");
        return Max_u64;
    }
}

static inline void allocator_reset_linear(allocator *alloc) {
    if (alloc->flags & ALLOCATOR_LINEAR_BIT)
        alloc->linear.used = 0;
}

static inline void allocator_reset_linear_to(allocator *alloc, uint64 to) {
    assert(to <= alloc->linear.used);
    if (alloc->flags & ALLOCATOR_LINEAR_BIT)
        alloc->linear.used = to;
}

#endif // include guard
