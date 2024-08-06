#ifndef SOL_LOG_H_INCLUDE_GUARD_
#define SOL_LOG_H_INCLUDE_GUARD_

#include "print.h"

typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
} log_level_severity;

static const char *LOG_LEVEL_MSG_TABLE[] = {
    "NONE",
    "ERROR",
};

#define LOG_LEVEL 1
#define LOG_BREAK 1

static inline void fn_log_print(
    log_level_severity  severity,
    const char         *file,
    int                 line,
    const char         *function,
    const char         *msg, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, msg);
    string_format_backend(buf, msg, args);

    if (severity >= LOG_LEVEL) {
        print("LOG %s (%s, line %i, fn %s): ", LOG_LEVEL_MSG_TABLE[severity], file, line, function);
        buf[strlen(buf) + 1] = '\0';
        buf[strlen(buf) + 0] = '\n';
        print_count_chars(buf, strlen(buf));
    }
}

#ifndef _WIN32
    #define asm __asm__
#endif

#if LOG_LEVEL

#if LOG_BREAK
    #define LOG_BREAKPOINT println("LOG BREAK"); asm("int $3")
#else
    #define LOG_BREAKPOINT
#endif


#define log_print(severity, msg...)                                                \
    do {                                                                           \
        fn_log_print(severity, __FILE__, __LINE__, __FUNCTION__, msg);             \
        LOG_BREAKPOINT;                                                            \
    } while(0)

#define log_print_if(predicate, severity, msg...)                          \
    do {                                                                   \
        if ((predicate)) {                                                \
            fn_log_print(severity, __FILE__, __LINE__, __FUNCTION__, msg); \
            LOG_BREAKPOINT;                                                \
        }                                                                  \
    } while(0)

#else

#define log_print(severity, msg...)
#define log_print_if(predicate, severity, msg...)

#endif

#define log_print_error(msg...) \
    log_print(LOG_LEVEL_ERROR, msg)
#define log_print_error_if(predicate, msg...) \
    log_print_if(predicate, LOG_LEVEL_ERROR, msg)

#endif // include guard
