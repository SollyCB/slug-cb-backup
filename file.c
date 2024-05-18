#include "file.h"

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
