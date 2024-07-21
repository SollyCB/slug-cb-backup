#ifndef SOL_PRINT_H_INCLUDE_GUARD_
#define SOL_PRINT_H_INCLUDE_GUARD_

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// 'args' must have been started, ends 'args' itself
void string_format_backend(char *format_buffer, const char *fmt, va_list args);

static inline void string_format(char *format_buffer, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    string_format_backend(format_buffer, fmt, args);
}

static inline void print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char format_buffer[1024];
    string_format_backend(format_buffer, fmt, args);

    fwrite(format_buffer, 1, strlen(format_buffer), stdout);
}

static inline void println(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char format_buffer[1024];
    string_format_backend(format_buffer, fmt, args);

    int tmp = strlen(format_buffer);
    format_buffer[tmp] = '\n';

    fwrite(format_buffer, 1, tmp + 1, stdout);
}

static inline void print_count_chars(const char *data, int count) {
    fwrite(data, 1, count, stdout);
}

static inline void println_count_chars(const char *data, int count) {
    fwrite(data, 1, count, stdout);
    println("");
}

static inline void print_ts(struct timespec ts)
{
    print("[%u sec, %u nsec]", ts.tv_sec, ts.tv_nsec);
}

static inline void println_ts(struct timespec ts)
{
    println("[%u sec, %u nsec]", ts.tv_sec, ts.tv_nsec);
}

static inline void print_time(long seconds, long nanoseconds) {
    char zbuf[32];
    int bi = 0;
    for(long i = 100000000; i > nanoseconds; i /= 10) {
        zbuf[bi] = '0';
        bi++;
    }
    zbuf[bi] = '\0';
    println("%u.%s%u", seconds, zbuf, nanoseconds);
}

#endif // include guard
