#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#include "allocator.h"
#include "print.h"
#include "thread.h"

static linear_allocator fn_new_linear_allocator(uint64 cap, void *buffer);
static void free_linear_allocator(linear_allocator *alloc);

static void* malloc_linear(allocator *alloc, uint64 size);
static void* realloc_linear(allocator *alloc, void *ptr, uint64 new_size);
static void* realloc_linear_with_old_size(allocator *alloc, void *ptr, uint64 old_size, uint64 new_size);
static void* free_allocation_linear(allocator *alloc, void *ptr); // Does nothing
static void init_arena(allocator *alloc, size_t min_block_size);
static void* arena_alloc(allocator *alloc, size_t size);
static void* free_arena(arena_allocator *alloc);
static void* arena_dealloc(allocator *alloc, void* ptr);
static void* arena_realloc(allocator *alloc, void *ptr, size_t old_size, size_t new_size);

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
    case ALLOCATOR_LINEAR_BIT:
        ret.linear = fn_new_linear_allocator(cap, buffer);
        ret.fpn_allocate = malloc_linear;
        ret.fpn_reallocate = realloc_linear;
        ret.fpn_deallocate = free_allocation_linear;
        ret.fpn_reallocate_with_old_size = realloc_linear_with_old_size;
        break;
    case ALLOCATOR_ARENA_BIT:
        init_arena(&ret, cap);
        ret.fpn_allocate = arena_alloc;
        ret.fpn_reallocate = NULL;
        ret.fpn_deallocate = arena_dealloc;
        ret.fpn_reallocate_with_old_size = arena_realloc;
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
        case ALLOCATOR_LINEAR_BIT:
        free_linear_allocator(&alloc->linear);
        break;
        default:
        break;
    }
}

static linear_allocator fn_new_linear_allocator(uint64 cap, void *buffer)
{
    linear_allocator ret;
    ret.cap = align(cap, ALLOCATOR_ALIGNMENT);
    ret.mem = buffer;
    ret.used = 0;
    return ret;
}

static void free_linear_allocator(linear_allocator *alloc)
{
    free(alloc->mem);
    alloc->cap = 0;
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

void* free_allocation_linear(allocator *alloc, void *ptr) { /* Do nothing */ return NULL; }

// Arena
static struct arena_footer* arena_add_block(uint size, uint min_size)
{
    size += sizeof(struct arena_footer);
    size = size < min_size ? min_size : align(size, getpagesize());
    void *data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (!data) {
        log_print_error("Failed to allocate arena block - %s", strerror(errno));
        return NULL;
    }
    size -= sizeof(struct arena_footer);
    struct arena_footer *ret = (struct arena_footer*)((uchar*)data + size);
    ret->check_bytes = ARENA_FOOTER_CHECK_BYTES;
    ret->allocation_count = 0;
    ret->used = 0;
    ret->base = data;
    ret->next = NULL;
    return ret;
}

static void init_arena(allocator *alloc, size_t min_block_size)
{
    arena_allocator *arena = &alloc->arena;
    arena->min_block_size = align(min_block_size + sizeof(struct arena_footer), getpagesize());
    arena->head = NULL;
    arena->tail = NULL;
}

static void* free_arena(arena_allocator *arena)
{
    uint block_i = 0;
    do {
        log_print_error_if(arena->tail->check_bytes != ARENA_FOOTER_CHECK_BYTES,
                "block %u has invalid header, check bytes == %uh", block_i, arena->tail->check_bytes);
        struct arena_footer *tmp = arena->tail;
        arena->tail = arena->tail->next;
        if (munmap(tmp->base, align(tmp->cap + sizeof(struct arena_footer), getpagesize()))) {
            log_print_error("failed to unmap block %u - arena address %uh, %s",
                    block_i, arena, strerror(errno));
            return tmp;
        }
        block_i++;
    } while(arena->tail);
    *arena = (arena_allocator){};
    return NULL;
}

static void* arena_alloc(allocator *alloc, size_t size)
{
    arena_allocator *arena = &alloc->arena;
    log_print_error_if(arena->head && arena->head->check_bytes != ARENA_FOOTER_CHECK_BYTES,
                "head has invalid header, check bytes == %uh", arena->head->check_bytes);
    if (size == 0)
        return NULL;
    size = allocalign(size);
    if (!arena->head) {
        arena->head = arena_add_block(size, arena->min_block_size);
        arena->tail = arena->head;
    } else if(arena->head->used + size > arena->head->cap) {
        struct arena_footer *new_block = arena_add_block(size, arena->min_block_size);
        arena->head->next = new_block;
        arena->head = new_block;
    }
    void *ret = (uchar*)arena->head->base + arena->head->used;
    arena->head->used += size;
    arena->head->allocation_count++;

    #if 0
    if (FRAMES_ELAPSED % 100 == 0) {
        struct arena_footer *f = alloc->arena.tail;
        int i = 0;
        uint64 s = 0;
        while(f) {
            s += f->used;
            f = f->next;
            i++;
        }
        println("Block Count %i, Used %u", i, s);
    }
    #endif

    return ret;
}

static void* arena_dealloc(allocator *alloc, void* ptr)
{
    arena_allocator *arena = &alloc->arena;
    // Makes the allocator very fast if only using one block. Basically just
    // a reference counted linear allocator at that point.
    if (arena->tail == arena->head) {
        log_print_error_if(!(arena->head->base <= ptr &&
                            (uchar*)arena->head->base + arena->head->used > (uchar*)ptr),
                            "arena_deallocate was passed an invalid pointer - arena address %uh, pointer %uh", arena, ptr);
        arena->head->allocation_count -= 1;
        if (!arena->head->allocation_count)
            arena->head->used = 0;
        return NULL;
    }
    struct arena_footer *pos = arena->tail;
    struct arena_footer *prev = arena->tail;
    uint block_i = 0;
    while(1) {
        log_print_error_if(pos->check_bytes != ARENA_FOOTER_CHECK_BYTES,
                "block %u has invalid header, check bytes == %uh", block_i, pos->check_bytes);
        if (pos->base <= ptr && (uchar*)pos->base + pos->used > (uchar*)ptr) {
            break;
        } else if (!pos->next) {
            log_print_error("arena_deallocate was passed an invalid pointer - arena address %uh, pointer %uh", arena, ptr);
            return (struct arena_footer*)0xcafebabecafebabe;
        }
        prev = pos;
        pos = pos->next;
        block_i++;
    }
    pos->allocation_count -= 1;
    if (pos->allocation_count)
        return NULL;

    if (pos == arena->head) {
        arena->head = prev;
        arena->head->next = NULL;
    } else if (pos == arena->tail) {
        arena->tail = pos->next;
    } else {
        prev->next = pos->next;
    }
    size_t size = pos->cap;
    if (munmap(pos->base, align(pos->cap + sizeof(struct arena_footer), getpagesize()))) {
        log_print_error("failed to unmap block %u - arena address %uh, %s",
                block_i, arena, strerror(errno));
        return pos;
    }
    return NULL;
}

static void* arena_realloc(allocator *alloc, void *ptr, size_t old_size, size_t new_size)
{
    arena_allocator *arena = &alloc->arena;
    void *ret = arena_alloc(alloc, new_size);
    if (!ret) return NULL;
    memcpy(ret, ptr, old_size);
    struct arena_footer *freed = arena_dealloc(alloc, ptr);
    log_print_error_if(freed, "failed to free pointer %uh - arena address %uh", ptr, alloc);
    return freed ? freed : ret;
}

