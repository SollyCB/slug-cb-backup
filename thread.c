#include <errno.h>
#include "allocator.h"
#include "thread.h"
#include "log.h"

// @Todo
// There is some bad interface stuff in this file. See the comment above the free_thread_pool
// implementation for a better idea of what I mean. As it says there, I am pretty sure that
// the actual work related implementation stuff is solid (from memory and from testing) I
// can see that the interface is very weak in some places.

static mutex thread_work_queue_locks[THREAD_WORK_QUEUE_COUNT];

static thread_work_queue new_thread_work_queue(uint i, allocator *alloc);
static uint thread_work_queue_add(thread_work_queue *queue, uint work_count, struct thread_work *work);
static void free_thread_work_queue(thread_work_queue *work_queue);

static void* thread_start(thread *self);
static uint thread_try_to_acquire_work(thread_work_queue *queue, uint id, struct thread_work *work);
static void* thread_shutdown(thread *self);

static uint thread_do_private_work(thread *self);
static private_thread_work_queue new_private_work_queue(uint cap, allocator *alloc);

enum  {
    THREAD_PRIVATE_RESULT_COMPLETE,
    THREAD_PRIVATE_RESULT_INCOMPLETE,
    THREAD_PRIVATE_RESULT_ALL_INCOMPLETE,
};

enum {
    THREAD_RESULT_SUCCESS      = 0,
    THREAD_RESULT_QUEUE_FULL   = 1,
    THREAD_RESULT_QUEUE_EMPTY  = 2,
    THREAD_RESULT_QUEUE_IN_USE = 3,
    THREAD_RESULT_POOL_BUSY    = 4,
    THREAD_RESULT_MUTEX_ERROR  = 5,
};

enum {
    THREAD_BUSY_BIT                     = 0x01,
    THREAD_SLEEP_BIT                    = 0x02,
    THREAD_SHUTDOWN_BIT                 = 0x04,
    THREAD_GRACEFUL_BIT                 = 0x08,
    THREAD_QUEUE_EMPTY_BIT              = 0x10,
    THREAD_QUEUE_TRYING_TO_ADD_WORK_BIT = 0x20,
};

int new_thread_pool(struct allocation buffers_heap[THREAD_COUNT], struct allocation buffers_temp[THREAD_COUNT], allocator *alloc, thread_pool *pool)
{
    uint i;
    for(i=0;i<THREAD_WORK_QUEUE_COUNT;++i)
        pool->work_queues[i] = new_thread_work_queue(i, alloc);

    int res;
    for(i=0;i<THREAD_COUNT;++i) {
        pool->threads[i].id = i+1; // disambiguate from main thread
        pool->threads[i].pool_write_flags = 0x0;
        pool->threads[i].thread_write_flags = 0x0;
    #if ARENA
        pool->threads[i].persistent = new_arena_allocator(buffers_heap[i].size, NULL);
    #else
        pool->threads[i].persistent = new_heap_allocator(buffers_heap[i].size, buffers_heap[i].data);
    #endif
        pool->threads[i].temp = new_linear_allocator(buffers_temp[i].size, buffers_temp[i].data);
        pool->threads[i].public_work_queues = pool->work_queues;
        pool->threads[i].private_work_queue = new_private_work_queue(1024, &pool->threads[i].persistent);
        res = create_thread(&pool->threads[i].handle, NULL, thread_start, &pool->threads[i]);
        log_print_error_if(res, "failed to create thread %u", i);
    }
    return res;
}

#define THREAD_WAIT_POOL_IDLE_MAX_LOOP_COUNT 0xffff

static inline bool is_thread_idle(thread *t) {
    return t->thread_write_flags & THREAD_BUSY_BIT;
}

static inline bool thread_signal_shutdown(thread *t, uint graceful_bit) {
    atomic_or(&t->pool_write_flags, THREAD_SHUTDOWN_BIT | graceful_bit);
    return is_thread_idle(t);
}

// @Todo Although I am pretty sure that the threading implementation itself is pretty solid
// (from memory) this interface is pretty weak. A second ago this function was literally just
// blocking forever or always returning true, but the return value was thread_result.
// I have seen similar interface issues elsewhere. Although it is not a desperate thing to
// work on, as I know what everything is doing when I see it break, but this is definitely
// a file that needs some makeup.
void free_thread_pool(thread_pool *pool, bool graceful)
{
    thread_status("Freeing threadpool");
    uint i;
    for(i=0;i<THREAD_COUNT;++i)
        thread_signal_shutdown(&pool->threads[i], THREAD_GRACEFUL_BIT & max32_if_true(graceful));

    void *rets[THREAD_COUNT];
    for(i=0;i<THREAD_COUNT;++i)
        join_thread(pool->threads[i].handle, &rets[i]);

    for(i=0;i<THREAD_WORK_QUEUE_COUNT;++i)
        free_thread_work_queue(&pool->work_queues[i]);

    thread_status("Thread pool free");
}

uint thread_add_work(thread_pool *pool, uint count, struct thread_work *work, thread_work_queue_priority priority)
{
    return thread_work_queue_add(&pool->work_queues[priority], count, work);
}

static thread_work_queue new_thread_work_queue(uint i, allocator *alloc)
{
    thread_work_queue ret;
    int err = init_mutex(&thread_work_queue_locks[i]);
    log_print_error_if(err, "failed to init mutex, err code %s", strerror(err));
    ret.lock = &thread_work_queue_locks[i];
    ret.work = sallocate(alloc, *ret.work, THREAD_WORK_QUEUE_SIZE);
    ret.tail = 0;
    ret.head = 0;
    ret.alloc = alloc;
    return ret;
}

static void free_thread_work_queue(thread_work_queue *work_queue)
{
    deallocate(work_queue->alloc, work_queue->work);
    work_queue->work = NULL;
    shutdown_mutex(work_queue->lock);
}

static uint thread_work_queue_add(thread_work_queue *queue, uint work_count, struct thread_work *work)
{
    assert(work_count);
    uint hi = inc_and_wrap(queue->head, 1, THREAD_WORK_QUEUE_SIZE);
    uint tail;
    atomic_load(&queue->tail, &tail);
    uint cnt = 0;
    while(hi != tail && cnt < work_count) {
        for(;hi!=tail && cnt < work_count;hi = inc_and_wrap_no_mod(hi, 1, THREAD_WORK_QUEUE_SIZE)) {
            queue->work[hi] = work[cnt];
            cnt++;
            atomic_store(&queue->head, &hi);
        }
        atomic_load(&queue->tail, &tail);
    }
    return cnt;
}

static inline uint thread_pause(uint pause_mask) {
    uint max = 64; // MAX_BACKOFF @Test Find a good value.
    for (uint i=pause_mask; i; --i)
        _mm_pause();

    pause_mask = pause_mask < max ? pause_mask << 1 : max;
    return pause_mask;
}

static inline void thread_begin_work(thread *self, struct thread_work *w)
{
    atomic_or(&self->thread_write_flags, THREAD_BUSY_BIT);

    struct thread_work_arg arg = {
        .self = self,
        .allocs = (struct allocators) {
            .temp = &self->temp,
            .persistent = &self->persistent
        },
        .arg = w->arg,
    };

    w->fn(&arg);
    allocator_reset_linear(&self->temp);

    atomic_and(&self->thread_write_flags, ~THREAD_BUSY_BIT);
}

static void* thread_start(thread *self)
{
    thread_status("Begin thread %u", self->id);
    struct thread_work w;
    assert(THREAD_WORK_QUEUE_COUNT < 32 && "need bigger masks");
    uint32 in_use_mask, empty_mask;
    uint pause_mask = 1;
    uint i;
    uint head, tail;
    while(!(self->pool_write_flags & THREAD_SHUTDOWN_BIT)) {

        thread_do_private_work(self);

        in_use_mask = 0x0;
        empty_mask = 0x0;
        for(i=0;i<THREAD_WORK_QUEUE_COUNT;++i) {

            // @Test This could be done with one load using flags,
            // but that would also generate a write. I assume two
            // loads is faster.
            atomic_load(&self->public_work_queues[i].head, &head);
            atomic_load(&self->public_work_queues[i].tail, &tail);
            if (tail == head) {
                empty_mask |= 1 << i;
                continue;
            }

            switch(thread_try_to_acquire_work(&self->public_work_queues[i], self->id, &w)) {
            case THREAD_RESULT_SUCCESS:
                thread_begin_work(self, &w);
                i = 0; // try to acquire highest priority work
                in_use_mask &= ~(1 << i);
                empty_mask &= ~(1 << i);
                pause_mask = 1;
                break;
            case THREAD_RESULT_QUEUE_IN_USE:
                in_use_mask |= 1 << i;
                break;
            case THREAD_RESULT_QUEUE_EMPTY:
                empty_mask |= 1 << i; // may have become empty between cmpxchg and thread_begin_work()
                break;
            case THREAD_RESULT_MUTEX_ERROR:
                log_print_error("mutex error acquiring work from queue %u - thread id %u", i, self->id);
                break;
            default:
                break;
            }
        }
        // @Todo Probably want to react differently depending on empty vs full, or maybe do not want to control that
        // here, and only want to react the main thread setting flags, as it will understand the workload.
        if ((in_use_mask | empty_mask) == ~(Max_u32 << THREAD_WORK_QUEUE_COUNT)) {
            thread_status("thread %u pausing for %u cycles", self->id, pause_mask);
            pause_mask = thread_pause(pause_mask);
        }
    }

    // @Todo Return work stats instead of null to judge core utilisation.
    return thread_shutdown(self);
}

static uint thread_try_to_acquire_work(thread_work_queue *queue, uint id, struct thread_work *work)
{
    switch(try_to_acquire_mutex(queue->lock)) {
    case 0:
        break;
    case EBUSY:
        thread_status("thread %u found in use queue", id);
        return THREAD_RESULT_QUEUE_IN_USE;
    default:
        log_print_error("mutex attempted acquisition returned abnormal error code %s, thread id %u", strerror(errno), id);
        return THREAD_RESULT_MUTEX_ERROR;
    }

    uint result = THREAD_RESULT_SUCCESS;
    if (atomic_cmpxchg_bool(&queue->head, queue->tail, queue->tail)) {
        thread_status("thread %u found empty queue", id);
        result = THREAD_RESULT_QUEUE_EMPTY;
        goto release_lock;
    }

    uint ti = inc_and_wrap_no_mod(queue->tail, 1, THREAD_WORK_QUEUE_SIZE);
    assert(queue->work[ti].fn);

    thread_status("thread %u got work item %u", id, ti);

    *work = queue->work[ti];
    queue->work[ti].arg = NULL; // Crash on invalid access.
    queue->work[ti].fn = NULL;
    queue->tail = ti;

release_lock: // @Todo This release can happen earlier
    release_mutex(queue->lock);
    return result;
}

static void* thread_shutdown(thread *self)
{
    if (!(self->pool_write_flags & THREAD_GRACEFUL_BIT)) {
        thread_status("thread %u shutting down ungracefully", self->id);
        return NULL;
    }
    thread_status("thread %u shutting down with grace", self->id);

    struct thread_work w;
    uint empty_mask = 0x0;
    uint in_use_mask = 0x0;
    uint pause_mask = 1;
    while(empty_mask != ~(Max_u32 << THREAD_WORK_QUEUE_COUNT)) {
        in_use_mask = 0x0;
        for(uint i=0;i<THREAD_WORK_QUEUE_COUNT;++i) {
            if (empty_mask & (1<<i))
                continue;

            switch(thread_try_to_acquire_work(&self->public_work_queues[i], self->id, &w)) {
            case THREAD_RESULT_SUCCESS:
                thread_begin_work(self, &w);
                in_use_mask &= ~(1 << i);
                pause_mask = 1;
                break;
            case THREAD_RESULT_QUEUE_IN_USE:
                in_use_mask |= 1 << i;
                break;
            case THREAD_RESULT_QUEUE_EMPTY:
                empty_mask |= 1 << i;
                break;
            case THREAD_RESULT_MUTEX_ERROR:
                log_print_error("mutex error acquiring work from queue %u - thread id %u", i, self->id);
                break;
            default:
                break;
            }
        }
        // @Todo Probably want to react differently depending on empty vs full, or maybe do not want to control that
        // here, and only want to react the main thread setting flags, as it will understand the workload.
        if (in_use_mask == ~(Max_u32 << THREAD_WORK_QUEUE_COUNT)) {
            thread_status("thread %u pausing for %u cycles", self->id, pause_mask);
            pause_mask = thread_pause(pause_mask);
        }
    }
    uint all_incomplete_count = 0;
    pause_mask = 1;
    bool private_work_complete = 0;
    while(!private_work_complete) {
        switch(thread_do_private_work(self)) {
        case THREAD_PRIVATE_RESULT_COMPLETE:
            private_work_complete = 1;
            continue;
        case THREAD_PRIVATE_RESULT_INCOMPLETE:
            all_incomplete_count = 0;
            pause_mask = 1;
            continue;
        case THREAD_PRIVATE_RESULT_ALL_INCOMPLETE:
            all_incomplete_count++;
            if (all_incomplete_count > 100) {
                log_print_error("thread %u failed to complete private work before shutdown", self->id);
                return (void*)-1;
            }
            thread_status("thread %u pausing for %u cycles", self->id, pause_mask);
            thread_pause(pause_mask);
            continue;
        default:
            log_print_error("Impossible case");
        }
    }
    return NULL;
}

// NOTE: Can only be called by the worker thread itself!
bool thread_add_private_work(thread *self, struct private_thread_work *pw)
{
    // @Note Idk if I want this reallocating the private queue: I can imagine some
    // scenario where someone has a pointer into the work queue (even though this would be
    // dumb) and then it relocates out under that address.
    #if 0
    if (q->count >= q->cap) {
        struct private_thread_work *old_work = q->work;
        q->cap *= 2;
        q->work = sallocate(&self->persistent, *q->work, q->cap);
        smemcpy(q->work, old_work, *q->work, q->count);
    }
    #endif

    struct private_thread_work_queue *q = &self->private_work_queue;
    if (q->count >= q->cap) {
        log_print_error("private work queue overflow in thread %u", self->id);
        return false;
    }

    q->work[q->count] = *pw;
    q->count++;

    return true;
}

static uint thread_do_private_work(thread *self)
{
    private_thread_work_queue *q = &self->private_work_queue;
    bool did_smtg = false;
    bool32 ready;
    for(uint i=0; i < q->count; ++i) {
        atomic_load(q->work[i].ready, &ready);
        if (!ready)
            continue;
        thread_begin_work(self, &q->work[i].work);
        q->work[i] = q->work[q->count - 1];
        q->count--;
        did_smtg = true;
    }
    if (q->count == 0)
        return THREAD_PRIVATE_RESULT_COMPLETE;
    else if (did_smtg)
        return THREAD_PRIVATE_RESULT_INCOMPLETE;
    return THREAD_PRIVATE_RESULT_ALL_INCOMPLETE;
}

static private_thread_work_queue new_private_work_queue(uint cap, allocator *alloc)
{
    private_thread_work_queue ret = {
        .cap = cap,
        .work = sallocate(alloc, *ret.work, cap),
    };
    return ret;
}
