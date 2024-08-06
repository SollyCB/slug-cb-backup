#ifndef SOL_JSON_H_INCLUDE_GUARD_
#define SOL_JSON_H_INCLUDE_GUARD_

#include "defs.h"
#include "allocator.h"
#include "ascii.h"
#include "string.h"

typedef enum {
    JSON_TYPE_INVALID = 0,
    JSON_TYPE_STRING = 1,
    JSON_TYPE_NUMBER = 2,
    JSON_TYPE_OBJECT = 3,
    JSON_TYPE_ARRAY = 4,
    JSON_TYPE_BOOL = 5,
    JSON_TYPE_NULL = 6,
} json_type;

typedef string json_string;
typedef double json_number;
typedef bool   json_bool;
typedef uint64 json_null;

typedef struct json json;

typedef struct {
    uint32 key_count;
    json_string *keys;
    struct json *values;
} json_object;

typedef struct json_array json_array;

struct json_array {
    uint32 len;
    json_type elem_type;
    union {
        json_string *strs;
        json_number *nums;
        json_object *objs;
        json_array  *arrs;
        json_bool   *booleans;
        json_null   *nulls;
    };
};

struct json {
    json_type type;
    union {
        json_string str;
        json_number num;
        json_object obj;
        json_array  arr;
        json_bool   boolean;
        json_null   null;
    };
};

json parse_json(struct file *f, allocator *alloc, struct allocation *mem_used);
void print_json(json *j);

static inline uint json_find_key(json_object *obj, const char *key) {
    uint cnt = obj->key_count;
    for(uint i = 0; i < cnt; ++i)
        if (memcmp(key, obj->keys[i].cstr, strlen(key)) == 0)
            return i;
    return Max_u32;
}

#endif // include guard
