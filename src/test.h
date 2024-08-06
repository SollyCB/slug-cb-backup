#if TEST

#ifndef SOL_TEST_HPP_INCLUDE_GUARD_
#define SOL_TEST_HPP_INCLUDE_GUARD_

#define TEST_LIST_BROKEN true
#define TEST_LIST_SKIPPED true

// Define 'TEST_LIST_BROKEN' to list broken tests, and 'TEST_LIST_SKIPPED' to list skipped tests
#include "assert.h"
#include "allocator.h"
#include "string.h"
#include "array.h"
#include "print.h"

typedef enum {
    TEST_RESULT_SUCCESS = 0,
    TEST_RESULT_FAIL = 1,
    TEST_RESULT_SKIPPED = 2,
    TEST_RESULT_BROKEN = 3,
} test_result;

typedef struct {
    string info;
    test_result result;
    uint32 start;
    uint32 end;
} test_module;

typedef struct {
    string info;
    test_result result;
    uint32 index;
} test;

typedef struct {
    uint32 fail;
    uint32 skipped;
    uint32 broken;
    string_buffer str_buf;
    test_module *modules;
    test *tests;
    allocator *alloc;
} test_suite;

test_suite load_tests(allocator *alloc);
void end_tests(test_suite *suite);

void begin_test_module(
    test_suite *suite,
    const char *name,
    const char *function_name,
    const char *file_name,
    bool broken,
    bool skipped);

void test_eq(
    test_suite *suite,
    const char *test_name,
    const char *function_name,
    const char *arg1_name,
    const char *arg2_name,
    int64       arg1,
    int64       arg2,
    bool        broken,
    int         line_number,
    const char *file_name);

void test_lt(
 test_suite *suite,
 const char *test_name,
 const char *function_name,
 const char *arg1_name,
 const char *arg2_name,
 int64       arg1,
 int64       arg2,
 bool        broken,
 int         line_number,
 const char *file_name);

void test_floateq(
 test_suite *suite,
 const char *test_name,
 const char *function_name,
 const char *arg1_name,
 const char *arg2_name,
 float       arg1,
 float       arg2,
 bool        broken,
 int         line_number,
 const char *file_name);

void test_streq(
 test_suite *suite,
 const char *test_name,
 const char *function_name,
 const char *arg1_name,
 const char *arg2_name,
 const       char* arg1,
 const       char* arg2,
 bool        broken,
 int         line_number,
 const char *file_name);

void test_ptreq(
 test_suite *suite,
 const char *test_name,
 const char *function_name,
 const char *arg1_name,
 const char *arg2_name,
 const       void *arg1,
 const       void* arg2,
 bool        broken,
 int         line_number,
 const char *file_name);

#define BEGIN_TEST_MODULE(name, broken, skipped) \
    begin_test_module(suite, name, __FUNCTION__, __FILE__, broken, skipped)

#define END_TEST_MODULE() array_last(suite->modules).end = (array_len(suite->tests) - 1)

#define TEST_EQ(test_name, arg1, arg2, broken) \
    test_eq(suite, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__)
#define TEST_LT(test_name, arg1, arg2, broken) \
    test_lt(suite, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__)
#define TEST_STREQ(test_name, arg1, arg2, broken) \
    test_streq(suite, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__)
#define TEST_FEQ(test_name, arg1, arg2, broken) \
    test_floateq(suite, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__)
#define TEST_PTREQ(test_name, arg1, arg2, broken) \
    test_ptreq(suite, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__)


#endif // include guard
#endif // #if TEST
