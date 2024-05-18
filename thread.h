#ifndef SOL_THREAD_H_INCLUDE_GUARD_
#define SOL_THREAD_H_INCLUDE_GUARD_

// @Todo @Threading Now that I have a better handle on threading and memory ordering, I should
// have a look over this implementation and see if it can be tidied up. Off the top of my head
// the only thing that might be made better is some atomics being replaced by fences, but idk
// if that really means anyhting. Maybe I will find a whopping great stinker though.

#include <pthread.h>
#include "defs.h"
#include "allocator.h"

#define THREAD_PRINT_STATUS 0

#if THREAD_PRINT_STATUS
    #define thread_status(m...) println(m)
#else
    #define thread_status(m...)
#endif

#ifndef _WIN32
    #define mutex pthread_mutex_t
    #define thread_handle pthread_t
#else
    // idk if this is right yet, really just a placeholder for now.
    #define mutex HANDLE
    #define thread_handle HANDLE
#endif

#define THREAD_COUNT 1
#define THREAD_WORK_QUEUE_COUNT 3
#define THREAD_WORK_QUEUE_SIZE 1024

#if THREAD_WORK_QUEUE_SIZE & (THREAD_WORK_QUEUE_SIZE - 1)
    #error "power of 2 please"
#endif

// @Todo windows equivalents
#ifndef _WIN32
    #define atomic_add(p, val) __sync_fetch_and_add   (p, val)
    #define atomic_sub(p, val) __sync_fetch_and_sub   (p, val)
    #define atomic_or(p, val) __sync_fetch_and_or     (p, val)
    #define atomic_and(p, val) __sync_fetch_and_and   (p, val)
    #define atomic_xor(p, val) __sync_fetch_and_xor   (p, val)
    #define atomic_nand(p, val) __sync_fetch_and_nand (p, val)
    #define atomic_cmpxchg_type(p, old_val, new_val) __sync_type_compare_and_swap(p, old_val, new_val)
    #define atomic_cmpxchg_bool(p, old_val, new_val) __sync_bool_compare_and_swap(p, old_val, new_val)

    #define atomic_load(from, to) __atomic_load(from, to, __ATOMIC_ACQUIRE)
    #define atomic_store(to, from) __atomic_store(to, from, __ATOMIC_RELEASE)

    #define init_mutex(m) pthread_mutex_init(m, NULL)
    #define shutdown_mutex(m) pthread_mutex_destroy(m)
    #define acquire_mutex(m) pthread_mutex_lock(m)
    #define try_to_acquire_mutex(m) pthread_mutex_trylock(m)
    #define release_mutex(m) pthread_mutex_unlock(m)

    #define create_thread(handle, info, fn, arg) pthread_create(handle, info, (void*(*)(void*))fn, (void*)arg)
    #define join_thread(handle, ret) pthread_join(handle, ret)
#endif


struct thread_work {
    void *(*fn)(void *);
    void *arg;
};

typedef struct thread thread;
struct thread_work_arg {
    thread *self;
    struct allocators allocs;
    void *arg;
};

typedef enum {
    THREAD_WORK_QUEUE_PRIORITY_HIGH   = 0,
    THREAD_WORK_QUEUE_PRIORITY_MEDIUM = 1,
    THREAD_WORK_QUEUE_PRIORITY_LOW    = 2,
} thread_work_queue_priority;

typedef struct {
    uint tail;
    uint head;
    mutex *lock;
    struct thread_work *work;
    allocator *alloc;
} thread_work_queue;

typedef struct {
    uint64 work_items_completed;
    uint64 time_working; // milliseconds
    uint64 time_paused; // milliseconds
} thread_shutdown_info;

struct private_thread_work {
    bool32 *ready;
    struct thread_work work;
};

typedef struct private_thread_work_queue {
    uint count;
    uint cap;
    struct private_thread_work *work;
} private_thread_work_queue;

struct thread {
    uint thread_write_flags;
    uint id;
    thread_handle handle;
    thread_work_queue *public_work_queues;
    private_thread_work_queue private_work_queue;
    allocator temp;
    allocator persistent;
    uint pool_write_flags;
    thread_shutdown_info prog_info;
};

typedef struct {
    thread threads[THREAD_COUNT];
    thread_work_queue work_queues[THREAD_WORK_QUEUE_COUNT];
} thread_pool;

int new_thread_pool(struct allocation buffers_heap[THREAD_COUNT], struct allocation buffers_temp[THREAD_COUNT], allocator *alloc, thread_pool *pool);
void free_thread_pool(thread_pool *pool, bool graceful);

uint thread_add_work(thread_pool *pool, uint count, struct thread_work *work, thread_work_queue_priority priority);
bool thread_add_private_work(thread *self, struct private_thread_work *pw);

#define cast_work_fn(fn) ((void* (*)(void*))fn)
#define cast_work_arg(w) ((void*)(w))
#define thread_add_work_high(pool, cnt, work)   thread_add_work(pool, cnt, work, THREAD_WORK_QUEUE_PRIORITY_HIGH)
#define thread_add_work_medium(pool, cnt, work) thread_add_work(pool, cnt, work, THREAD_WORK_QUEUE_PRIORITY_MEDIUM)
#define thread_add_work_low(pool, cnt, work)    thread_add_work(pool, cnt, work, THREAD_WORK_QUEUE_PRIORITY_LOW)


static inline void signal_thread_true(bool32 *b)
{
    bool32 one = 1;
    atomic_store(b, &one);
    _mm_sfence();
}

static inline void signal_thread_false(bool32 *b)
{
    bool32 zero = 0;
    atomic_store(b, &zero);
    _mm_sfence();
}

#endif // include guard
