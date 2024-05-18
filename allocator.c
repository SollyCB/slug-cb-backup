#include "allocator.h"
#include "print.h"
#include "thread.h"
#include <stdlib.h>

static heap_allocator fn_new_heap_allocator(uint64 cap, void *buffer);
static linear_allocator fn_new_linear_allocator(uint64 cap, void *buffer);
static void free_heap_allocator(heap_allocator *alloc);
static void free_linear_allocator(linear_allocator *alloc);

static void* malloc_heap(allocator *alloc, uint64 size);
static void* malloc_linear(allocator *alloc, uint64 size);
static void* realloc_heap(allocator *alloc, void *ptr, uint64 new_size);
static void* realloc_linear(allocator *alloc, void *ptr, uint64 new_size);
static void* realloc_linear_with_old_size(allocator *alloc, void *ptr, uint64 old_size, uint64 new_size);
static void free_allocation_heap(allocator *alloc, void *ptr);
static void free_allocation_linear(allocator *alloc, void *ptr); // Does nothing

static inline void* realloc_heap_with_old_size(allocator *alloc, void *ptr, size_t old_sz, size_t new_sz) {
    return realloc_heap(alloc, ptr, new_sz);
}

allocator new_allocator(size_t cap, void *buffer, allocator_flag_bits type)
{
    allocator ret = (allocator){};
    ret.flags = type;

    cap = align(cap, ALLOCATOR_ALIGNMENT);

    if (!buffer)
        buffer = malloc(cap);
    else
        ret.flags |= ALLOCATOR_DO_NOT_FREE_BIT;

    switch(type) {
        case ALLOCATOR_HEAP_BIT:
        ret.heap = fn_new_heap_allocator(cap, buffer);
        ret.fpn_allocate = malloc_heap;
        ret.fpn_reallocate = realloc_heap;
        ret.fpn_deallocate = free_allocation_heap;
        ret.fpn_reallocate_with_old_size = realloc_heap_with_old_size;
        break;
        case ALLOCATOR_LINEAR_BIT:
        ret.linear = fn_new_linear_allocator(cap, buffer);
        ret.fpn_allocate = malloc_linear;
        ret.fpn_reallocate = realloc_linear;
        ret.fpn_deallocate = free_allocation_linear;
        ret.fpn_reallocate_with_old_size = realloc_linear_with_old_size;
        break;
        default:
        assert(false && "Invalid allocator type");
        break;
    }
    return ret;
}

void free_allocator(allocator *alloc)
{
    if (alloc->flags & ALLOCATOR_DO_NOT_FREE_BIT)
        return;
    switch(alloc->flags & ALLOCATOR_TYPE_BITS) {
        case ALLOCATOR_HEAP_BIT:
        free_heap_allocator(&alloc->heap);
        break;
        case ALLOCATOR_LINEAR_BIT:
        free_linear_allocator(&alloc->linear);
        break;
        default:
        break;
    }
}

static heap_allocator fn_new_heap_allocator(uint64 cap, void *buffer)
{
    heap_allocator ret;
    ret.cap = align(cap, ALLOCATOR_ALIGNMENT);
    ret.mem = buffer;
    ret.used = 0;
    ret.tlsf_handle = tlsf_create_with_pool(ret.mem, ret.cap);
    return ret;
}

static linear_allocator fn_new_linear_allocator(uint64 cap, void *buffer)
{
    linear_allocator ret;
    ret.cap = align(cap, ALLOCATOR_ALIGNMENT);
    ret.mem = buffer;
    ret.used = 0;
    return ret;
}

static void free_heap_allocator(heap_allocator *alloc)
{
    uint64 stats[] = {0, alloc->cap};
    if (alloc->used) {
        pool_t p = tlsf_get_pool(alloc->tlsf_handle);
        tlsf_walk_pool(p, NULL, (void*)&stats);
        println(" Size Remaining In Heap Allocator: %u", alloc->used);
    }

    free(alloc->mem);
    alloc->cap = 0;
}

static void free_linear_allocator(linear_allocator *alloc)
{
    free(alloc->mem);
    alloc->cap = 0;
}

static void* malloc_heap(allocator *alloc, uint64 size)
{
    assert(size < alloc->heap.cap);
    void *ret = tlsf_memalign(alloc->heap.tlsf_handle, ALLOCATOR_ALIGNMENT, align(size, 16));
    alloc->heap.used += tlsf_block_size(ret);
    return ret;
}

static void* malloc_linear(allocator *alloc, uint64 size)
{
    assert(alloc->flags & ALLOCATOR_LINEAR_BIT);
    size = align(size, ALLOCATOR_ALIGNMENT);
    alloc->linear.used = align(alloc->linear.used, ALLOCATOR_ALIGNMENT); // really this is unnecessary, as only aligned sizes are ever allocated.
    void *ret = (void*)(alloc->linear.mem + alloc->linear.used);
    alloc->linear.used += size;
    assert(alloc->linear.used <= alloc->linear.cap && "Linear Allocator Overflow");
    return ret;
}

static void* realloc_heap(allocator *alloc, void *ptr, uint64 new_size)
{
    assert(alloc->flags & ALLOCATOR_HEAP_BIT);
    uint64 old_size = tlsf_block_size(ptr);
    alloc->heap.used -= old_size;
    ptr = tlsf_realloc(alloc->heap.tlsf_handle, ptr, align(new_size, 16));
    alloc->heap.used += tlsf_block_size(ptr);
    return ptr;
}

static void* realloc_linear(allocator *alloc, void *ptr, uint64 new_size)
{
    void *p_old = ptr;
    ptr = malloc_linear(alloc, new_size);
    memcpy(ptr, p_old, new_size);
    return ptr;
}

static void* realloc_linear_with_old_size(allocator *alloc, void *ptr, uint64 old_size, uint64 new_size)
{
    void *p_old = ptr;
    ptr = malloc_linear(alloc, new_size);
    memcpy(ptr, p_old, old_size);
    return ptr;
}

static void free_allocation_heap(allocator *alloc, void *ptr)
{
    assert(alloc->flags & ALLOCATOR_HEAP_BIT);
    uint64 size = tlsf_block_size(ptr);
    tlsf_free(alloc->heap.tlsf_handle, ptr);
    alloc->heap.used -= size;
}

void free_allocation_linear(allocator *alloc, void *ptr) { /* Do nothing */ }

