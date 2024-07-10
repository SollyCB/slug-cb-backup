#ifndef SOL_FILE_H_INCLUDE_GUARD_
#define SOL_FILE_H_INCLUDE_GUARD_

#include "defs.h"
#include "allocator.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "log.h"

struct file {
    char *data;
    size_t size;
};

#ifdef WIN32
    #include <io.h> // access()
#endif

static inline bool file_exists(const char *file_name)
{
    return access(file_name, R_OK|W_OK) == 0; // 0 is success code
}

// @Note breaks on windows?
static inline uint file_dir_name(const char *file_name, char *buf)
{
    uint p = 0;
    uint len = strlen(file_name);
    for(uint i = 0; i < len; ++i) {
        p = file_name[i] == '/' ? i : p;
        buf[i] = file_name[i];
    }
    buf[p+1] = 0;
    return p+1;
}

static inline uint file_extension(const char *file_name, char *buf)
{
    uint p = 0;
    uint len = strlen(file_name);
    for(uint i = 0; i < len; ++i) {
        buf[p] = file_name[i];
        p = file_name[i] == '.' ? 0 : p + 1;
    }
    buf[p] = 0;
    return p;
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

enum {
    READ = O_RDONLY,
    WRITE = O_WRONLY,
    CREATE = O_CREAT,
};
#define check_file_result(r) (r != -1)
#define FILE_READ_ALL Max_u64

int file_open(const char *path, int flags);
int64 file_write(int fd, uint64 offset, uint64 count, void *data);
int64 file_read(int fd, uint64 offset, uint64 count, void *data);
bool file_close(int fd);

static inline int64 file_open_write(const char *path, uint64 offset, uint64 count, void *data)
{
    int fd = file_open(path, O_WRONLY);
    uint64 r = file_write(fd, offset, count, data);
    file_close(fd);
    return r;
}

static inline int64 file_open_read(const char *path, uint64 offset, uint64 count, void *data)
{
    int fd = file_open(path, O_RDONLY);
    int64 r = file_read(fd, offset, count, data);
    file_close(fd);
    return r;
}

// @Deprecated
struct file file_read_bin_all(const char *file_name, allocator *alloc);
struct file file_read_char_all(const char *file_name, allocator *alloc);
void file_read_bin_size(const char *file_name, size_t size, void *buffer);
void file_read_char_count(const char *file_name, size_t count, char *buffer);

void file_write_bin(const char *file_name, size_t size, const void *data);
void file_write_char(const char *file_name, size_t count, const char *data);
void file_append_bin(const char *file_name, size_t size, const void *data);
void file_append_char(const char *file_name, size_t count, const char *data);

#endif // include guard
