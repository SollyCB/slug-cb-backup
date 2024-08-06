#include "ringbuffer.h"

bool init_ringbuffer(ringbuffer *buf, int size, uint flags)
{
    size = align(size, getpagesize());
    assert(size < Max_s32);

    void *m = mmap(NULL, size * 2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (m == MAP_FAILED) {
        log_print_error("failed to map data, size %u", size * 2);
        return false;
    }

    int fd = memfd_create("ringbuffer", 0x0);
    if(fd == -1) {
        log_print_error("failed to memfd_create");
        goto fail_fd_create;
    }

    if (ftruncate(fd, size) == -1) {
        log_print_error("failed to truncate file - size %u", size);
        goto fail_truncate;
    }

    void *p1 = mmap((uchar*)m + 0,    size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);
    void *p2 = mmap((uchar*)m + size, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);

    if (p1 == MAP_FAILED || p2 == MAP_FAILED) {
        log_print_error("failed to map pages to file");
        goto fail_truncate;
    }

    buf->size = size;
    buf->data = m;
    buf->flags = flags;
    buf->head = 0;
    buf->tail = 0;

    return true;

fail_truncate:
    close(fd);
fail_fd_create:
    munmap(m, size*2);
    return false;
}

static inline bool rb_head_crosses_tail(ringbuffer *rb, int size)
{
    assert(rb->head < Max_s32 - size);
    // @Todo There *must* be a nice algorithm to find this without branches.
    int h = rb->head;
    int t = rb->tail;
    if (h > t && h + size - rb->size > t)
        return true;
    if (h < t && h + size > t)
        return true;
    return false;
}

static inline bool rb_tail_crosses_head(ringbuffer *rb, int size)
{
    assert(rb->tail < Max_s32 - size);
    // @Todo There *must* be a nice algorithm to find this without branches.
    int t = rb->tail;
    int h = rb->head;
    if (t > h && t + size - rb->size > h)
        return true;
    if (t < h && t + size > h)
        return true;
    return false;
}

void* rballoc(ringbuffer *rb, int size)
{
    size = allocalign(size);
    assert(size <= rb->size);

    if (!(rb->flags & RB_NOTAIL) && rb_head_crosses_tail(rb, size)) {
        log_print_error("ringbuffer overflow, head would cross tail: allocation size = %u, rb = {.size = %u, .head = %u, .tail = %u}",
                        size, rb->size, rb->head, rb->tail);
        return NULL;
    }

    void *ret = rb->data + rb->head;
    rb->head += size;
    rb->head -= rb->size & maxif(rb->head >= rb->size);

    return ret;
}

bool rbfree(ringbuffer *rb, int size)
{
    if (!(rb->flags & RB_NOTAIL) && rb_tail_crosses_head(rb, size)) {
        log_print_error("ringbuffer underflow, tail would cross head: allocation size = %u, rb = {.size = %u, .head = %u, .tail = %u}",
                        size, rb->size, rb->head, rb->tail);
        return 0;
    }

    rb->tail += size;
    rb->tail -= rb->size & maxif(rb->tail >= rb->size);

    return true;
}
