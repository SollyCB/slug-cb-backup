#ifndef SOL_FILE_H_INCLUDE_GUARD_
#define SOL_FILE_H_INCLUDE_GUARD_

#include "defs.h"
#include "allocator.h"
#include <sys/stat.h>
#include <unistd.h>
#include "log.h"

struct file {
    char *data;
    size_t size;
};

// @Note breaks on windows?
static inline uint get_dir(const char *file_name, char *buf) {
    uint p = 0;
    uint len = strlen(file_name);
    for(uint i = 0; i < len; ++i) {
        if (file_name[i] == '/')
            p = i;
        buf[i] = file_name[i];
    }
    buf[p+1] = 0;
    return (uint)p+1;
}

static inline void file_last_modified(const char *path, struct timespec *ts) {
    struct stat s;
    int err = stat(path, &s);
    log_print_error_if(err, "failed to stat file %s", path);
    *ts = s.st_mtim;
}

static inline bool has_file_changed(const char *path, struct timespec *then) {
    struct timespec now;
    file_last_modified(path, &now);
    return now.tv_sec != then->tv_sec || now.tv_nsec != then->tv_nsec;
}

struct file file_read_bin_all(const char *file_name, allocator *alloc);
struct file file_read_char_all(const char *file_name, allocator *alloc);
void file_read_bin_size(const char *file_name, size_t size, void *buffer);
void file_read_char_count(const char *file_name, size_t count, char *buffer);

void file_write_bin(const char *file_name, size_t size, const void *data);
void file_write_char(const char *file_name, size_t count, const char *data);
void file_append_bin(const char *file_name, size_t size, const void *data);
void file_append_char(const char *file_name, size_t count, const char *data);

#endif // include guard
