#ifndef SOL_ALLOCATOR_H_INCLUDE_GUARD_
#define SOL_ALLOCATOR_H_INCLUDE_GUARD_

#include "defs.h"
#include "assert.h"

#define ALLOCATOR_ALIGNMENT 16

#define allocalign(sz) align(sz, ALLOCATOR_ALIGNMENT)
#define alloc_align(sz) align(sz, ALLOCATOR_ALIGNMENT)
#define alloc_align_type(t) align(sizeof(t), ALLOCATOR_ALIGNMENT)

/*
   @Todo: This file and its corresponding source are really old and have little
   ugly silly bits and comments. I need to move in the slightly more refined versions
   from sol.h...

   new_allocator should not return such a large struct, allocator stores too many function
   pointers since I never use realloc, there are some redundant lines, etc.
*/

struct allocation {
    void *data;
    size_t size;
};

typedef struct {
    uint64 cap;
    uint64 used;
    uint8 *mem;
} linear_allocator;

#define ARENA_FOOTER_CHECK_BYTES 0xfeedbeef

struct arena_footer {
    uint check_bytes;
    uint allocation_count;
    size_t used;
    size_t cap;
    void *base;
    struct arena_footer *next;
};

typedef struct arena_allocator {
    struct arena_footer *tail;
    struct arena_footer *head;
    size_t max_total_size;
    size_t min_block_size;
} arena_allocator;

typedef enum {
    ALLOCATOR_LINEAR_BIT      = 0x01,
    ALLOCATOR_ARENA_BIT       = 0x02,
    ALLOCATOR_DO_NOT_FREE_BIT = 0x04,

    ALLOCATOR_TYPE_BITS = ALLOCATOR_ARENA_BIT | ALLOCATOR_LINEAR_BIT,
} allocator_flag_bits;

typedef struct allocator allocator;

struct allocator {
    uint32 flags;
    void* (*fpn_allocate)(allocator*, size_t);
    void* (*fpn_reallocate)(allocator*, void*, size_t);
    void* (*fpn_deallocate)(allocator*, void*);
    void* (*fpn_reallocate_with_old_size)(allocator *, void *, size_t, size_t);
    union {
        linear_allocator linear;
        arena_allocator  arena;
    };
};

struct allocators {
    allocator *temp;
    allocator *persistent;
};

// Leave 'buffer' null to allocate cap from standard allocator.
allocator new_allocator(size_t cap, void *buffer, allocator_flag_bits type);
void free_allocator(allocator *alloc);

#define new_linear_allocator(cap, buffer) new_allocator(cap, buffer, ALLOCATOR_LINEAR_BIT)
#define new_arena_allocator(cap, buffer) new_allocator(cap, buffer, ALLOCATOR_ARENA_BIT)

static inline void *allocate(allocator *alloc, size_t size) {
    return alloc->fpn_allocate(alloc, size);
}

static inline void *reallocate(allocator *alloc, void *ptr, size_t new_size) {
    return alloc->fpn_reallocate(alloc, ptr, new_size);
}

static inline void *reallocate_with_old_size(allocator *alloc, void *ptr, size_t old_size, size_t new_size) {
    return alloc->fpn_reallocate_with_old_size(alloc, ptr, old_size, new_size);
}

static inline void* deallocate(allocator *alloc, void *ptr) {
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

static inline size_t allocator_used(allocator *alloc)
{
    switch(alloc->flags & ALLOCATOR_TYPE_BITS) {
    case ALLOCATOR_LINEAR_BIT:
    {
        return alloc->linear.used;
    }
    case ALLOCATOR_ARENA_BIT:
    {
        size_t s = 0;
        struct arena_footer *f = alloc->arena.tail;
        while(f) { s += f->used; f = f->next; }
        return s;
    }
    default:
        assert(false && "Invalid allocator flags");
        return Max_u64;
    }
}

static inline uint allocator_block_count(allocator *alloc)
{
    log_print_error_if(!(alloc->flags & ALLOCATOR_ARENA_BIT),
                       "linear allocators do not use blocks");

    struct arena_footer *f = alloc->arena.tail;

    uint count = 0;
    while(f->next) { count++; f = f->next; }

    return count;
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
