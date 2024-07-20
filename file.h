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

static inline struct timespec file_last_modified(const char *path) {
    struct stat s;
    int err = stat(path, &s);
    log_print_error_if(err, "failed to stat file %s: %s", path, strerror(errno));
    return s.st_mtim;
}

static inline bool has_file_changed(const char *path, struct timespec then) {
    struct timespec now = file_last_modified(path);
    return now.tv_sec != then.tv_sec || now.tv_nsec != then.tv_nsec;
}

static inline bool file_resize(int fd, uint64 sz)
{
    int e = ftruncate(fd, sz);
    log_print_error_if(e == -1, "failed to resize file %i: %s", fd, strerror(errno));
    return e != -1;
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
static inline int64 file_open_write(const char *path, uint64 offset, uint64 count, void *data);
static inline int64 file_open_write_create(const char *path, uint64 offset, uint64 count, void *data);
static inline int64 file_open_read(const char *path, uint64 offset, uint64 count, void *data);
static inline struct file file_read_all(const char *path, allocator *alloc);

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
