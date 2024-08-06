#ifndef SOL_ASSERT_H_IMPL
#define SOL_ASSERT_H_IMPL

#if DEBUG
#if _WIN32

#define assert(x) \
    if (!(x)) {printf("\n    [file: %s, line: %i, fn %s]\n        ** ASSERTION FAILED **: %s\n\n", __FILE__, __LINE__, __FUNCTION__, #x); __debugbreak;}

#else

#define assert(x) \
    if (!(x)) {printf("\n    [file: %s, line: %i, fn %s]\n        ** ASSERTION FAILED **: %s\n\n", __FILE__, __LINE__, __FUNCTION__, #x); asm("int $3");}

#endif // _WIN32 or not

#else
#define assert(x)
#endif // if DEBUG

#endif // include guard
