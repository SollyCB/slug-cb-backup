#ifndef SOL_TIMER_H_INCLUDE_GUARD_
#define SOL_TIMER_H_INCLUDE_GUARD_

#include "defs.h"

struct timer {
    struct timespec s; // start
    struct timespec e; // end
};

static inline struct timer start_timer()
{
    struct timer t;
    t.s = get_time_cpu_thread();
    return t;
}

static inline struct timespec end_timer(struct timer *t)
{
    t->e = get_time_cpu_thread();
    struct timespec ret;
    ret.tv_sec = t->e.tv_sec - t->s.tv_sec; // assumes never wrapped
    ret.tv_nsec = labs((int64)t->e.tv_nsec - (int64)t->s.tv_nsec);
    return ret;
}

static inline void end_timer_and_print(struct timer *t) {
    struct timespec ts = end_timer(t);
    print_time(ts.tv_sec,ts.tv_nsec);
}

static inline float get_float_time_proc()
{
    struct timespec t = get_time_cpu_proc();
    return (float)t.tv_sec + ((float)t.tv_nsec * 1e-9);
}

#endif // include guard
