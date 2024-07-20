#include "file.h"

int file_open(const char *path, int flags)
{
    if ((flags & (O_RDONLY|O_WRONLY)) == (O_RDONLY|O_WRONLY))
        flags = (flags & ~(O_RDONLY|O_WRONLY)) | O_RDWR;
    int e = open(path, flags);
    log_print_error_if(!check_file_result(e), "failed to open file %s with perms %u: %s", path, flags, strerror(errno));
    return e;
}

bool file_close(int fd)
{
    int e = close(fd);
    log_print_error_if(!check_file_result(e), "failed to close file %i: %s", fd, strerror(errno));
    return check_file_result(e);
}

int64 file_write(int fd, uint64 offset, uint64 count, void *data)
{
    int64 sz = pwrite(fd, data, count, offset);
    log_print_error_if(!check_file_result(sz), "failed to write file %i: %s", fd, strerror(errno));
    log_print_error_if((uint64)sz != count,
            "failed to write all data requested to file %i: requested %u, written %u",
            fd, count, sz);
    return sz;
}

int64 file_read(int fd, uint64 offset, uint64 count, void *data)
{
    int64 sz = pread(fd, data, count, offset);
    log_print_error_if(!check_file_result(sz), "failed to read file %i: %s", fd, strerror(errno));
    log_print_error_if((uint64)sz != count && count != FILE_READ_ALL,
            "failed to read all data requested from file %i: requested %u, written %u",
            fd, count, sz);
    return sz;
}

static inline int64 file_open_write(const char *path, uint64 offset, uint64 count, void *data)
{
    int fd = file_open(path, O_WRONLY);
    uint64 r = file_write(fd, offset, count, data);
    file_close(fd);
    return r;
}

static inline int64 file_open_write_create(const char *path, uint64 offset, uint64 count, void *data)
{
    int fd = file_open(path, WRITE|CREATE);
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

static inline struct file file_read_all(const char *path, allocator *alloc)
{
    int fd = file_open(path, READ);
    struct stat stat;
    int e = fstat(fd, &stat);
    log_print_error_if(e, "failed to stat file %s: %s", path, strerror(errno));
    void *data = allocate(alloc, stat.st_size);
    file_read(fd, 0, stat.st_size, data);
    return (struct file) {.size = stat.st_size, .data = data};
}

/*--------------------------------------------------------------------------------*/
// @Deprecated
struct file file_read_bin_all(const char *file_name, allocator *alloc)
{
    FILE *f = fopen(file_name, "rb");
    if (!f) {
        log_print_error("FILE: Failed to open file '%s'", file_name);
        return (struct file){};
    }
    fseek(f, 0, SEEK_END);
    struct file ret;
    ret.size = ftell(f);
    ret.data = allocate(alloc, ret.size);
    fseek(f, 0, SEEK_SET);
    size_t s = fread(ret.data, 1, ret.size, f);
    log_print_error_if(s != ret.size, "failed to read entire file %s: file size %u, read %u", file_name, ret.size, s);
    fclose(f);
    return ret;
}

struct file file_read_char_all(const char *file_name, allocator *alloc)
{
    FILE *f = fopen(file_name, "r");
    if (!f) {
        log_print_error("FILE: Failed to open file '%s'", file_name);
        return (struct file){};
    }
    fseek(f, 0, SEEK_END);
    struct file ret;
    ret.size = ftell(f);
    ret.data = allocate(alloc, ret.size);
    fseek(f, 0, SEEK_SET);
    size_t s = fread(ret.data, 1, ret.size, f);
    log_print_error_if(s != ret.size, "failed to read entire file %s: file size %u, read %u", file_name, ret.size, s);
    fclose(f);
    return ret;
}

void file_read_bin_size(const char *file_name, size_t size, void *buffer)
{
    FILE *f = fopen(file_name, "rb");
    if (!f) {
        log_print_error("FILE: Failed to open file '%s'", file_name);
        return;
    }
    size_t s = fread(buffer, 1, size, f);
    log_print_error_if(s != size, "failed to read %u bytes from file %s, read %u", size, file_name, s);
    fclose(f);
}

void file_read_char_count(const char *file_name, size_t count, char *buffer)
{
    FILE *f = fopen(file_name, "r");
    if (!f) {
        log_print_error("FILE: Failed to open file '%s'", file_name);
        return;
    }
    size_t s = fread(buffer, 1, count, f);
    log_print_error_if(s != count, "failed to read size from file %s", file_name);
    fclose(f);
}

void file_write_bin(const char *file_name, size_t size, const void *data)
{
    FILE *f = fopen(file_name, "wb");
    if (!f) {
        log_print_error("FILE: Failed to open file '%s'", file_name);
        return;
    }
    fwrite(data, 1, size, f);
    fclose(f);
}

void file_write_char(const char *file_name, size_t count, const char *data)
{
    FILE *f = fopen(file_name, "w");
    if (!f) {
        log_print_error("FILE: Failed to open file '%s'", file_name);
        return;
    }
    fwrite(data, 1, count, f);
    fclose(f);
}

void file_append_bin(const char *file_name, size_t size, const void *data)
{
    FILE *f = fopen(file_name, "ab");
    if (!f) {
        log_print_error("FILE: Failed to open file '%s'", file_name);
        return;
    }
    fwrite(data, 1, size, f);
    fclose(f);
}

void file_append_char(const char *file_name, size_t count, const char *data)
{
    FILE *f = fopen(file_name, "a");
    if (!f) {
        log_print_error("FILE: Failed to open file '%s'", file_name);
        return;
    }
    fwrite(data, 1, count, f);
    fclose(f);
}
