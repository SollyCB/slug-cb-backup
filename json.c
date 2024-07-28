#include "json.h"

typedef enum {
    JSON_RESULT_SUCCESS = 0,
    JSON_RESULT_KEY = 1,
    JSON_RESULT_END = 2,
    JSON_RESULT_INVALID,
} json_result;

typedef json (*json_parse_fn)(const char *, uint32 *, allocator *);

static json json_parse_object(const char *data, uint32 *offset, allocator *alloc);
static json json_parse_array(const char *data, uint32 *offset, allocator *alloc);
static json json_parse_string(const char *data, uint32 *offset, allocator *alloc);
static json json_parse_number(const char *data, uint32 *offset, allocator *alloc);
static json json_parse_bool(const char *data, uint32 *offset, allocator *alloc);
static json json_parse_null(const char *data, uint32 *offset, allocator *alloc);
static uint32 json_object_count_keys(const char *data);
static json_parse_fn json_array_get_elem_info(const char *data, json_array *arr, allocator *alloc);
static json_result json_parser_find_key(const char *data, uint32 *pos);
static json_result json_skip_over_key_and_value(const char *data, uint32 *pos);
static json_result json_skip_to_equivalent_depth(const char *data, uint32 *pos, char open, char close);

static void print_json_with_depth(json *j, int depth);
static void json_print_object(json_object *a, int depth);
static void json_print_array(json_array *a, int depth);
static void json_print_string(json_string *a, int depth);
static void json_print_number(json_number *a, int depth);
static void json_print_bool(json_bool *a, int depth);
static void json_print_null(json_null *a, int depth);

// data[0] == first char of type
static inline json_type json_get_type(const char *data)
{
    switch(data[0]) {
    case '"':
        return JSON_TYPE_STRING;
    case '{':
        return JSON_TYPE_OBJECT;
    case '[':
        return JSON_TYPE_ARRAY;
    case 'f':
    case 't':
        return JSON_TYPE_BOOL;
    case 'n':
        return JSON_TYPE_NULL;
    default:
        return JSON_TYPE_NUMBER;
    }
}

static inline json_result json_validate_null(const char *data) {
    return strcmp(data, "null") ? JSON_RESULT_INVALID : JSON_RESULT_SUCCESS;
}

static inline json_result json_validate_bool(const char *data) {
    return memcmp(data, "true", 4) && memcmp(data, "false", 5) ? JSON_RESULT_INVALID : JSON_RESULT_SUCCESS;
}

static inline json_result json_validate_number(const char *data) {
    uint32 len = simd_ascii_double_len(data);
    switch(data[len]) {
    case '}':
    case ']':
    case ' ':
    case '\n':
    case ',':
        return JSON_RESULT_SUCCESS;
    default:
        log_print_error("Invalid number, found %c immediately following it", data[len]);
        return JSON_RESULT_INVALID;
    }
}

static inline json_result json_skip_over_string(const char *data, uint32 *pos) {
    *pos += 1;
    while(1) {
        *pos += simd_find_char(data + *pos, '"');
        if (data[*pos] == '\\') {
            *pos += 1;
            continue;
        } else {
            *pos += 1;
            break;
        }
    }
    return JSON_RESULT_SUCCESS;
}

static inline json_result json_skip_over_number(const char *data, uint32 *pos) {
    json_result ret = json_validate_number(data + *pos);
    log_print_error_if(ret != JSON_RESULT_SUCCESS, "Failed to validate number - char index %u", *pos);
    *pos += simd_ascii_double_len(data + *pos);
    return ret;
}

static inline json_result json_skip_over_object(const char *data, uint32 *pos) {
    return json_skip_to_equivalent_depth(data, pos, '{', '}');
}

static inline json_result json_skip_over_array(const char *data, uint32 *pos) {
    return json_skip_to_equivalent_depth(data, pos, '[', ']');
}

static inline json_result json_skip_over_bool(const char *data, uint32 *pos) {
    json_result ret = json_validate_bool(data + *pos);
    log_print_error_if(ret != JSON_RESULT_SUCCESS, "Failed to validate bool - char index %u", *pos);
    *pos += 4 + (data[*pos] == 'f');
    return ret;
}

static inline json_result json_skip_over_null(const char *data, uint32 *pos) {
    json_result ret = json_validate_null(data + *pos);
    log_print_error_if(ret != JSON_RESULT_SUCCESS, "Failed to validate null - char index %u", *pos);
    pos += 4;
    return ret;
}

static inline uint32 json_skip_to_value(const char *data) {
    uint32 ret = simd_find_char(data, ':');
    ret++;
    ret += simd_skip_over_whitespace(data + ret);
    return ret;
}

// file size * 2 seems large enough to hold the C translation -- nvm, had to increase it 28 Jul 2024
#define JSON_ALLOCATION_SIZE_MULTIPLIER 4

json parse_json(struct file *f, allocator *alloc, struct allocation *mem_used)
{
    size_t size = JSON_ALLOCATION_SIZE_MULTIPLIER * f->size;
    void *buf = allocate(alloc, size);
    allocator json_alloc = new_linear_allocator(size, buf);

    if (mem_used)
        *mem_used = (struct allocation){buf, size};

    uint32 pos = 0;
    json ret;
    ret.type = json_get_type(f->data);
    switch(ret.type) {
    case JSON_TYPE_OBJECT:
        ret = json_parse_object(f->data, &pos, &json_alloc);
        return ret;
    case JSON_TYPE_ARRAY:
        ret = json_parse_array(f->data, &pos, &json_alloc);
        return ret;
    case JSON_TYPE_STRING:
        log_print_error("String is an invalid top level item");
        return (json){};
    case JSON_TYPE_NUMBER:
        log_print_error("Number is an invalid top level item");
        return (json){};
    case JSON_TYPE_BOOL:
        log_print_error("Bool is an invalid top level item");
        return (json){};
    case JSON_TYPE_NULL:
        log_print_error("Null is an invalid top level item");
        return (json){};
    default:
        log_print_error("Failed to determine type of top level item");
        return (json){};
    }
}

void print_json(json *j)
{
    print_json_with_depth(j, 1);
    println("");
}

static json json_parse_object(const char *data, uint32 *offset, allocator *alloc)
{
    uint32 pos = *offset + (data[*offset] == '{');
    json ret;
    ret.type = JSON_TYPE_OBJECT;
    ret.obj.key_count = json_object_count_keys(data + pos);
    if (ret.obj.key_count == Max_u32)
        goto fail;

    ret.obj.values = sallocate(alloc, *ret.obj.values, ret.obj.key_count);
    ret.obj.keys = sallocate(alloc, *ret.obj.keys, ret.obj.key_count);

    uint32 tmp;
    for(uint32 i = 0; i < ret.obj.key_count; ++i) {
        json_parser_find_key(data, &pos);
        ret.obj.keys[i].cstr = data + pos + 1;
        tmp = pos;
        json_skip_over_string(data, &pos);
        ret.obj.keys[i].len = pos - tmp - 2;
        pos += json_skip_to_value(data + pos);
        ret.obj.values[i].type = json_get_type(data + pos);
        switch(ret.obj.values[i].type) {
        case JSON_TYPE_STRING:
            ret.obj.values[i] = json_parse_string(data, &pos, NULL);
            if (ret.obj.values[i].type == JSON_TYPE_INVALID) {
                log_print_error("Got invalid string while parsing object value - char index %u", pos);
                goto fail;
            }
            break;
        case JSON_TYPE_NUMBER:
            ret.obj.values[i] = json_parse_number(data, &pos, NULL);
            if (ret.obj.values[i].type == JSON_TYPE_INVALID) {
                log_print_error("Got invalid object while parsing object value - char index %u", pos);
                goto fail;
            }
            break;
        case JSON_TYPE_OBJECT:
            ret.obj.values[i] = json_parse_object(data, &pos, alloc);
            if (ret.obj.values[i].type == JSON_TYPE_INVALID) {
                log_print_error("Got invalid object while parsing object value - char index %u", pos);
                goto fail;
            }
            break;
        case JSON_TYPE_ARRAY:
            ret.obj.values[i] = json_parse_array(data, &pos, alloc);
            if (ret.obj.values[i].type == JSON_TYPE_INVALID) {
                log_print_error("Got invalid array while parsing object value - char index %u", pos);
                goto fail;
            }
            break;
        case JSON_TYPE_BOOL:
            ret.obj.values[i] = json_parse_bool(data, &pos, NULL);
            if (ret.obj.values[i].type == JSON_TYPE_INVALID) {
                log_print_error("Got invalid bool while parsing object value - char index %u", pos);
                goto fail;
            }
            break;
        case JSON_TYPE_NULL:
            ret.obj.values[i] = json_parse_null(data, &pos, NULL);
            if (ret.obj.values[i].type == JSON_TYPE_INVALID) {
                log_print_error("Got invalid null while parsing object value - char index %u", pos);
                goto fail;
            }
            break;
        default:
            log_print_error("Got unrecognisable type parsing object value - char index %u", pos);
            goto fail;
        }
    }
    pos += simd_find_char(data + pos, '}');
    *offset = pos + 1;
    return ret;

fail:
    log_print_error("Failed to parse object, Location - ");
    print_count_chars(data + pos, 100);
    println("\n");
    return (json){};
}

inline static void json_fill_array_elem(json_array *to, json *from, uint i) {
    switch(from->type) {
    case JSON_TYPE_STRING:
        to->strs[i] = from->str;
        break;
    case JSON_TYPE_NUMBER:
        to->nums[i] = from->num;
        break;
    case JSON_TYPE_OBJECT:
        to->objs[i] = from->obj;
        break;
    case JSON_TYPE_ARRAY:
        to->arrs[i] = from->arr;
        break;
    case JSON_TYPE_BOOL:
        to->booleans[i] = from->boolean;
        break;
    case JSON_TYPE_NULL:
        to->nulls[i] = from->null;
        break;
    default:
        log_print_error("Invalid type while filling array elem");
    }
}

static json json_parse_array(const char *data, uint32 *offset, allocator *alloc)
{
    uint32 pos = *offset + (data[*offset] == '[');
    json ret;
    ret.type = JSON_TYPE_ARRAY;
    json_parse_fn parse_fn = json_array_get_elem_info(data + pos, &ret.arr, alloc);
    if (ret.arr.len == Max_u32) {
        log_print_error("Failed to find array len - char index %u", pos);
        goto fail;
    }
    uint32 i;
    json tmp;
    for(i = 0; i < ret.arr.len; ++i) {
        pos += simd_skip_over_whitespace(data + pos);
        tmp = parse_fn(data, &pos, alloc);
        json_fill_array_elem(&ret.arr, &tmp, i);
        pos += simd_skip_over_whitespace(data + pos);
        pos += data[pos] == ',';
        pos += simd_skip_over_whitespace(data + pos);
    }
    assert(data[pos] == ']');
    *offset = pos + 1;
    return ret;

fail:
    log_print_error("Failed to parse array - char index %u", pos);
    return (json){};
}

static json json_parse_string(const char *data, uint32 *offset, allocator *alloc)
{
    assert(data[*offset] == '"');
    uint32 pos = *offset;
    json_skip_over_string(data, &pos);
    json ret;
    ret.type = JSON_TYPE_STRING;
    ret.str.cstr = data + *offset + 1;
    ret.str.len = pos - *offset - 2;
    *offset = pos;
    return ret;
}

static json json_parse_number(const char *data, uint32 *offset, allocator *alloc)
{
    assert(data[*offset] == '-' || (data[*offset] >= '0' && data[*offset] <= '9'));
    if (json_validate_number(data + *offset) != JSON_RESULT_SUCCESS) {
        log_print_error("Failed to validate number - char index %u", *offset);
        return (json){};
    }
    json ret;
    ret.type = JSON_TYPE_NUMBER;
    ret.num = ascii_to_double(data + *offset);
    *offset += simd_ascii_double_len(data + *offset);
    return ret;
}

static json json_parse_bool(const char *data, uint32 *offset, allocator *alloc)
{
    assert(data[*offset] == 't' || data[*offset] == 'f');
    if (json_validate_bool(data + *offset) != JSON_RESULT_SUCCESS) {
        log_print_error("Failed to validate bool - char index %u", *offset);
        return (json){};
    }
    json ret;
    ret.type = JSON_TYPE_BOOL;
    ret.boolean = data[*offset] == 't' ? true : false;
    *offset += 4 + (!ret.boolean);
    return ret;
}

static json json_parse_null(const char *data, uint32 *offset, allocator *alloc)
{
    assert(data[*offset] == 'n');
    if (json_validate_null(data + *offset) != JSON_RESULT_SUCCESS) {
        log_print_error("Failed to validate null - char index %u", *offset);
        return (json){};
    }
    json ret;
    ret.type = JSON_TYPE_NULL;
    ret.null = 0;
    *offset += 4;
    return ret;
}

static uint32 json_object_count_keys(const char *data)
{
    uint32 ret = 0;
    uint32 pos = data[0] == '{';
    while(1) {
        switch(json_parser_find_key(data, &pos)) {
        case JSON_RESULT_KEY:
            ret++;
            if (json_skip_over_key_and_value(data, &pos) != JSON_RESULT_SUCCESS)
                return Max_u32;
            else
                continue;
        case JSON_RESULT_END:
            return ret;
        default:
            log_print_error("Failed to find key or find closing '}' - char index %u", pos);
            return Max_u32;
        }
    }
}
static json_parse_fn json_array_get_elem_info(const char *data, json_array *arr, allocator *alloc)
{
    uint32 pos = data[0] == '[';
    pos += simd_skip_over_whitespace(data + pos);
    json_result (*skip_fn)(const char *, uint32 *);
    json_parse_fn parse_fn;
    json_type type = json_get_type(data + pos);
    void **elems;
    uint elem_size;
    switch(type) {
    case JSON_TYPE_STRING:
        skip_fn = json_skip_over_string;
        parse_fn = json_parse_string;
        arr->elem_type = JSON_TYPE_STRING;
        elems = (void**)&arr->strs;
        elem_size = sizeof(*arr->strs);
        break;
    case JSON_TYPE_NUMBER:
        skip_fn = json_skip_over_number;
        parse_fn = json_parse_number;
        arr->elem_type = JSON_TYPE_NUMBER;
        elems = (void**)&arr->nums;
        elem_size = sizeof(*arr->nums);
        break;
    case JSON_TYPE_OBJECT:
        skip_fn = json_skip_over_object;
        parse_fn = json_parse_object;
        arr->elem_type = JSON_TYPE_OBJECT;
        elems = (void**)&arr->objs;
        elem_size = sizeof(*arr->objs);
        break;
    case JSON_TYPE_ARRAY:
        skip_fn = json_skip_over_array;
        parse_fn = json_parse_array;
        arr->elem_type = JSON_TYPE_ARRAY;
        elems = (void**)&arr->arrs;
        elem_size = sizeof(*arr->arrs);
        break;
    case JSON_TYPE_BOOL:
        skip_fn = json_skip_over_bool;
        parse_fn = json_parse_bool;
        arr->elem_type = JSON_TYPE_BOOL;
        elems = (void**)&arr->booleans;
        elem_size = sizeof(*arr->booleans);
        break;
    case JSON_TYPE_NULL:
        skip_fn = json_skip_over_null;
        parse_fn = json_parse_null;
        arr->elem_type = JSON_TYPE_NULL;
        elems = (void**)&arr->nulls;
        elem_size = sizeof(*arr->nulls);
        break;
    default:
        log_print_error("Got invalid element type while counting array elems - char index %u", pos);
        arr->len = Max_u32;
        return NULL;
    }
    uint32 i;
    for(i = 0; data[pos] != ']'; ++i) {
        if (skip_fn(data, &pos) != JSON_RESULT_SUCCESS) {
            log_print_error("Failed to skip element %u in array", i);
            arr->len = Max_u32;
            return NULL;
        }
        pos += simd_skip_over_whitespace(data + pos);
        pos += data[pos] == ',';
        pos += simd_skip_over_whitespace(data + pos);
    }
    arr->len = i;
    *elems = allocate(alloc, elem_size * arr->len);
    return parse_fn;
}

static json_result json_parser_find_key(const char *data, uint32 *pos)
{
    *pos += simd_skip_over_whitespace(data + *pos);
    if (data[*pos] == ',') {
        *pos += 1;
        *pos += simd_skip_over_whitespace(data + *pos);
    }
    if (data[*pos] == '"')
        if (data[((int)*pos) - 1] == '\\') {
            log_print_error("Found escaped '\"' outside of a key - char index %u", *pos);
            return JSON_RESULT_INVALID;
        } else {
            return JSON_RESULT_KEY;
        }
    else if (data[*pos])
        return JSON_RESULT_END;
    else
        return JSON_RESULT_INVALID;
}

static json_result json_skip_over_key_and_value(const char *data, uint32 *pos)
{
    json_skip_over_string(data, pos);
    *pos += simd_skip_over_whitespace(data + *pos);
    if (data[*pos] != ':') {
        log_print_error("Found '%c' in place of ':' when skipping key - char index %u", data[*pos], *pos);
        return JSON_RESULT_INVALID;
    } else {
        *pos += 1;
    }
    *pos += simd_skip_over_whitespace(data + *pos);
    switch(json_get_type(data + *pos)) {
    case JSON_TYPE_STRING:
        return json_skip_over_string(data, pos);
    case JSON_TYPE_NUMBER:
        return json_skip_over_number(data, pos);
    case JSON_TYPE_OBJECT:
        return json_skip_over_object(data, pos);
    case JSON_TYPE_ARRAY:
        return json_skip_over_array(data, pos);
    case JSON_TYPE_BOOL:
        return json_skip_over_bool(data, pos);
    case JSON_TYPE_NULL:
        return json_skip_over_null(data, pos);
    default:
        return JSON_RESULT_INVALID;
    }
}

static json_result json_skip_to_equivalent_depth(const char *data, uint32 *pos, char open, char close)
{
    *pos += data[*pos] == open;
    *pos += simd_skip_over_whitespace(data + *pos);
    int32 stack = 1;
    uint32 i, j, pc0, pc1, tz0, tz1;
    uint16 m0, m1;
    for(i = 0; true; i += 16) {
        m0 = simd_match_char(data + *pos + i, open);
        m1 = simd_match_char(data + *pos + i, close);
        pc0 = pop_count16(m0);
        pc1 = pop_count16(m1);
        for(j = 0; j < pc0 + pc1; ++j) {
            tz0 = ctz16(m0) | max32_if_false(m0);
            tz1 = ctz16(m1) | max32_if_false(m1);
            stack += tz0 < tz1;
            stack -= tz0 > tz1;
            if (stack == 0) {
                *pos += i + tz1 + 1;
                assert(data[*pos-1] == close);
                return JSON_RESULT_SUCCESS;
            } else if (stack < 0) {
                log_print_error("Too many '%c' relative to '%c' while skipping object - char index %u", close, open, *pos + i);
                return JSON_RESULT_INVALID;
            }
            m0 ^= (tz0 < tz1) << (tz0 & max32_if_true(tz0 < 16));
            m1 ^= (tz0 > tz1) << (tz1 & max32_if_true(tz1 < 16));
        }
    }
}

void print_json_with_depth(json *j, int depth)
{
    switch(j->type) {
    case JSON_TYPE_STRING:
        json_print_string(&j->str, depth);
        return;
    case JSON_TYPE_NUMBER:
        json_print_number(&j->num, depth);
        return;
    case JSON_TYPE_OBJECT:
        json_print_object(&j->obj, depth);
        return;
    case JSON_TYPE_ARRAY:
        json_print_array(&j->arr, depth);
        return;
    case JSON_TYPE_BOOL:
        json_print_bool(&j->boolean, depth);
        return;
    case JSON_TYPE_NULL:
        json_print_null(&j->null, depth);
        return;
    default:
        println("INVALID");
        return;
    }
}

#define JSON_PRINT_INDENT_BUFFER_SIZE 32
#define JSON_PRINT_SPACE_COUNT 2
const char JSON_PRINT_INDENT_BUFFER[JSON_PRINT_INDENT_BUFFER_SIZE] = "                                ";
#define JSON_PRINT_INDENT(depth) print_count_chars(JSON_PRINT_INDENT_BUFFER, (depth) * JSON_PRINT_SPACE_COUNT)

static void json_print_object(json_object *a, int depth)
{
    println("{");
    for(uint i = 0; i < a->key_count; ++i) {
        JSON_PRINT_INDENT(depth);
        print("\"");
        print_count_chars(a->keys[i].cstr, a->keys[i].len);
        print("\": ");
        print_json_with_depth(&a->values[i], depth + 1);
        println(",");
    }
    JSON_PRINT_INDENT(depth - 1);
    print("}");
}

static void json_print_array(json_array *a, int depth)
{
    println("[");
    for(uint i = 0; i < a->len; ++i) {
        JSON_PRINT_INDENT(depth);
        switch(a->elem_type) {
        case JSON_TYPE_OBJECT:
            json_print_object(&a->objs[i], depth + 1);
            break;
        case JSON_TYPE_ARRAY:
            json_print_array(&a->arrs[i], depth + 1);
            break;
        case JSON_TYPE_STRING:
            json_print_string(&a->strs[i], depth + 1);
            break;
        case JSON_TYPE_NUMBER:
            json_print_number(&a->nums[i], depth + 1);
            break;
        case JSON_TYPE_BOOL:
            json_print_bool(&a->booleans[i], depth + 1);
            break;
        case JSON_TYPE_NULL:
            json_print_null(&a->nulls[i], depth + 1);
            break;
        default:
            log_print_error("Cannot print invalid array elem");
            break;
        }
        println(",");
    }
    JSON_PRINT_INDENT(depth - 1);
    print("]");
}

static void json_print_string(json_string *a, int depth)
{
    print("\"");
    print_count_chars(a->cstr, a->len);
    print("\"");
}

static void json_print_number(json_number *a, int depth)
{
    print("%f", *a);
}

static void json_print_bool(json_bool *a, int depth)
{
    *a ? print("true") : println("false");
}

static void json_print_null(json_null *a, int depth)
{
    print("NULL");
}

