#if TEST
#include "test.h"

// Format colors
#define RED    "\e[1;31m"
#define GREEN  "\e[1;32m"
#define YELLOW "\e[1;33m"
#define BLUE   "\e[1;34m"
#define NC     "\e[0m"

#define TEST_PRINT_TESTS 1

#if TEST_PRINT_TESTS
    #define print_test_status(...) println(__VA_ARGS__)
#else
    #define print_test_status(...)
#endif

test_suite load_tests(allocator *alloc)
{
    print_test_status("\nTest Config:");
    if (TEST_LIST_BROKEN) {
        print_test_status("TEST_LIST_BROKEN  == true, printing broken tests...");
    } else {
        print_test_status("TEST_LIST_BROKEN  == false, broken tests silent...");
    }
    if (TEST_LIST_SKIPPED) {
        print_test_status("TEST_LIST_SKIPPED == true, printing skipped tests...");
    } else {
        print_test_status("TEST_LIST_SKIPPED == false, skipped tests silent...");
    }
    print_test_status("\nBeginning Tests...");

    test_suite ret = (test_suite){};
    ret.tests   = new_array(128, *ret.tests, alloc);
    ret.modules = new_array(128, *ret.modules, alloc);
    ret.str_buf = new_string_buffer(8192, alloc); // @Todo This can overflow, add the reallocating buffer from sol.h
    ret.alloc = alloc;
    return ret;
}

void end_tests(test_suite *suite)
{
    if (suite->fail)
        print_test_status("");

    // -Wunused
    // uint32 skipped_count = 0;
    // uint32 broken_count = 0;
    // uint32 fail_count = 0;

    // Ofc these should just be separate arrays on the suite. But it does not matter for now. Bigger fish.
    for(uint32 i = 0; i < array_len(suite->modules); ++i) {
        switch(suite->modules[i].result) {
        case TEST_RESULT_BROKEN:
            print_test_status("%sMODULE BROKEN:%s", YELLOW, NC);
            print_test_status("    %s", suite->modules[i].info.cstr);
            // broken_count++; -Wunused
            break;
        case TEST_RESULT_SKIPPED:
            print_test_status("%sMODULE SKIPPED:%s", YELLOW, NC);
            print_test_status("    %s", suite->modules[i].info.cstr);
            // skipped_count++; -Wunused
            break;
        case TEST_RESULT_FAIL:
            print_test_status("%sMODULE FAILED:%s", RED, NC);
            print_test_status("    %s", suite->modules[i].info.cstr);
            // fail_count++; -Wunused
            break;
        case TEST_RESULT_SUCCESS:
            break;
        }
    }

    if (suite->fail || (suite->broken && TEST_LIST_BROKEN) || (suite->skipped && TEST_LIST_SKIPPED))
        print_test_status("");

    if (suite->skipped)
        print_test_status("%sTESTS WERE SKIPPED%s", YELLOW, NC);
    else
        print_test_status("%sNO TESTS SKIPPED%s", GREEN, NC);

    if (suite->broken)
        print_test_status("%sTESTS ARE BROKEN%s", YELLOW, NC);
    else
        print_test_status("%sNO TESTS BROKEN%s", GREEN, NC);

    if (suite->fail)
        print_test_status("%sTESTS WERE FAILED%s", RED, NC);
    else
        print_test_status("%sNO TESTS FAILED%s", GREEN, NC);

    print_test_status("");

    assert(!suite->skipped && !suite->broken && !suite->fail);

    free_string_buffer(&suite->str_buf);
    free_array(suite->tests);
    free_array(suite->modules);

    allocator_reset_linear(suite->alloc);
}

void begin_test_module(
    test_suite *suite,
    const char *name,
    const char *function_name,
    const char *file_name,
    bool        broken,
    bool        skipped)
{
    test_module mod = (test_module){};
    mod.start = array_len(suite->tests);

    char info_buffer[256];
    string_format(info_buffer, "[%s, fn %s] %s", file_name, function_name, name);
    assert(strlen(info_buffer) < 255);

    mod.info = string_buffer_get_string_from_cstring(&suite->str_buf, info_buffer);
    if (broken) {
        mod.result = TEST_RESULT_BROKEN;
    } else if (skipped) {
        mod.result = TEST_RESULT_SKIPPED;
    } else {
        mod.result = TEST_RESULT_SUCCESS;
    }
    array_add(suite->modules, mod);
}

// Message macros
#if TEST_LIST_BROKEN
#define TEST_MSG_BROKEN(info) \
    print_test_status("%sBroken Test: %s%s", YELLOW, NC, info)
#else
#define TEST_MSG_BROKEN(name)
#endif

#if TEST_LIST_SKIPPED
#define TEST_MSG_SKIPPED(name) \
    print_test_status("%sSkipped Test%s '%s'", YELLOW, NC, name)
#else
#define TEST_MSG_SKIPPED(name)
#endif

#define SKIP_BROKEN_TEST_MSG(mod) \
    print_test_status("%sWarning: Module Skips %u Broken Tests...%s", YELLOW, mod.skipped_broken_test_names.len, NC)

#define TEST_MSG_FAIL(name, info, msg) print_test_status("%sFAILED TEST %s%s:\n%s\n    %s", RED, name, NC, info, msg)

#define TEST_MSG_PASS print_test_status("%sOK%s", GREEN, NC)

// Tests
void test_backend(
    test_suite *suite,
    const char *function_name,
    const char *file_name,
    int         line_number,
    const char *test_name,
    const       char *msg,
    bool        test_passed,
    bool        broken)
{
    test_result result;
    test_module *module = &array_last(suite->modules);

    char info[127];
    string_format(info, "%s, line %i, fn %s", file_name,  line_number, function_name);

    if (broken || module->result == TEST_RESULT_BROKEN || module->result == TEST_RESULT_SKIPPED) {
        if (broken || module->result == broken) {
            TEST_MSG_BROKEN(info);
            result = TEST_RESULT_BROKEN;
            suite->broken++;
        } else {
            print_test_status("%s", info);
            TEST_MSG_SKIPPED(test_name);
            result = TEST_RESULT_SKIPPED;
            suite->skipped++;
        }
    } else {
        if (test_passed) {
            result = TEST_RESULT_SUCCESS;
        } else {
            TEST_MSG_FAIL(test_name, info, msg);
            result = TEST_RESULT_FAIL;
            module->result = TEST_RESULT_FAIL;
            suite->fail++;
        }
    }

    if (result != TEST_RESULT_SUCCESS) {
        string str = string_buffer_get_string_from_cstring(&suite->str_buf, info);
        test t = (test){.info = str, .result = result, .index = array_len(suite->tests)};
        array_add(suite->tests, t);
    }

    return;
}

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
    const char *file_name)
{
    bool test_passed = arg1 == arg2;

    char msg[256];
    string_format(msg, "%s = %i, %s = %i", arg1_name, arg1, arg2_name, arg2);
    test_backend(suite, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

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
    const char *file_name)
{
    bool test_passed = arg1 < arg2;

    char msg[256];
    string_format(msg, "%s = %i, %s = %i", arg1_name, arg1, arg2_name, arg2);
    test_backend(suite, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

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
    const char *file_name)
{
    // float f1 = arg1 - arg2;
    // float f2 = arg2 - arg1;
    // bool test_passed = (f1 < inaccuracy) && (f2 < inaccuracy) ? true : false;

    float inaccuracy = 0.000001;
    bool test_passed = fabsf(arg1 - arg2) < inaccuracy;

    char msg[256];
    string_format(msg, "%s = %f, %s = %f", arg1_name, arg1, arg2_name, arg2);
    test_backend(suite, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

void test_streq(
    test_suite *suite,
    const char *test_name,
    const char *function_name,
    const char *arg1_name,
    const char *arg2_name,
    const char* arg1,
    const char* arg2,
    bool        broken,
    int         line_number,
    const char *file_name)
{
    bool test_passed = strcmp(arg1, arg2) == 0;

    char msg[256];
    string_format(msg, "%s = %s, %s = %s", arg1_name, arg1, arg2_name, arg2);
    test_backend(suite, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

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
    const char *file_name)
{
    bool test_passed = arg1 == arg2;

    char msg[256];
    string_format(msg, "%s = %uh, %s = %uh", arg1_name, arg1, arg2_name, arg2);
    test_backend(suite, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

#endif // #if TEST
