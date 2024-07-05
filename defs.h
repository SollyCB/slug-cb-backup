#ifndef SOL_DEFS_H_INCLUDE_GUARD_
#define SOL_DEFS_H_INCLUDE_GUARD_

extern int FRAME_I;
extern int SCR_H;
extern int SCR_W;
extern float ASPECT_RATIO;
extern float FOV;

#define ASPECT_RATIO ((float)SCR_H / SCR_W)
#define PERSPECTIVE_NEAR 0.1
#define PERSPECTIVE_FAR  100.0

#define _GNU_SOURCE

#define FRAME_COUNT 2

#define DEBUG 1
#define TEST  0
#define MULTITHREADED 1
#define GPU 1
#define CHECK_END_STATE 1
#define NO_DESCRIPTOR_BUFFER 1
#define DESCRIPTOR_BUFFER (!NO_DESCRIPTOR_BUFFER)

#define MAX_URI_LEN 64
#define STATUS_INCOMPLETE 0xeeffeeff

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <x86gprintrin.h>
#include <x86intrin.h>
#include <time.h>
#include "print.h"
#include "log.h"
#include "assert.h"

typedef unsigned int uint;

typedef uint32_t uint32;
typedef int32_t  int32;
typedef uint64_t uint64;
typedef int64_t  int64;
typedef uint16_t uint16;
typedef int16_t  int16;
typedef uint8_t  uint8;
typedef int8_t   int8;
typedef unsigned char uchar;

typedef uint32 bool32;

#define asm __asm__

#define Max_f32    FLT_MAX
#define Max_uint64 UINT64_MAX
#define Max_uint32 UINT32_MAX
#define Max_uint16 UINT16_MAX
#define Max_uint8  UINT8_MAX
#define Max_u64 UINT64_MAX
#define Max_u32 UINT32_MAX
#define Max_u16 UINT16_MAX
#define Max_u8  UINT8_MAX
#define Max_s64  INT64_MAX
#define Max_s32  INT32_MAX
#define Max_s16  INT16_MAX
#define Max_s8   INT8_MAX

#define float_or_max(f) ((float)((uint64)(f) | Max_u64))

#ifndef _WIN32
#define cl_align(s) __attribute__((aligned(s))) // compiler align
#else
#error Todo
#endif

#define max64_if_true(eval)  (Max_uint64 + (uint64)((eval) == 0))
#define max32_if_true(eval)  (Max_uint32 + (uint32)((eval) == 0))
#define max8_if_true(eval)   (Max_uint8  +  (uint8)((eval) == 0))
#define max64_if_false(eval) (Max_uint64 + (uint64)((eval) != 0))
#define max32_if_false(eval) (Max_uint32 + (uint32)((eval) != 0))
#define max8_if_false(eval)  (Max_uint8  +  (uint8)((eval) != 0))

// Wow the bugs here made it into a lot of commits... Embarrassing...
#define max_if(eval)  (Max_u64 + ((eval) == 0))
#define zero_if(eval) (Max_u64 + ((eval) != 0))
#define maxif(eval)   (0 - ((eval) != 0))
#define zeroif(eval)  (0 - ((eval) == 0))

#if 0
    #if (max_if(5) != Max_u64 || max_if(0) != 0)
        #error max_if()
    #elif (maxif(5) != Max_u64 || maxif(0) != 0)
        #error maxif()
    #elif (zero_if(0) != Max_u64 || zero_if(5) != 0)
        #error zero_if()
    #elif (zeroif(0) != Max_u64 || zeroif(5) != 0)
        #error zeroif()
    #endif
#endif

#define count_enum_flags(last_member) (ctz(last_member - 1) + 1)

const uint64 one64 = 1;

struct memreq {
    size_t size;
    size_t alignment;
};

struct range {
    uint offset;
    uint size;
};

struct range64 {
    size_t offset;
    size_t size;
};

struct pair_uint {
    uint a,b;
};

struct minmax {
    float min,max;
};

static inline uint64 align(uint64 size, uint64 alignment) {
    const uint64 alignment_mask = alignment - 1;
    return (size + alignment_mask) & ~alignment_mask;
}

static inline int max(int x, int y) {
    return x > y ? x : y;
}

static inline uint inc_and_wrap_no_mod(uint num, uint inc, uint max) {
    return (num+inc) & (max-1);
}

static inline uint dec_and_wrap_no_mod(uint num, uint dec, uint max) {
    return (num-dec) & (max-1);
}

static inline uint inc_and_wrap(uint num, uint inc, uint max) {
    return (num+inc) % max;
}

static inline bool before(uint64_t a, uint64_t b) {
    return ((int64)b - (int64)a) > 0;
}

static inline uint32 set_bit_idx(uint32 mask, uint i) {
    return mask | (1 << i);
}

static inline uint32 clear_bit_idx(uint32 mask, uint i) {
    return mask & ~(1 << i);
}

static inline struct timespec get_time_cpu_proc() {
    struct timespec ts;
    int ok = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    log_print_error_if(ok == (clock_t)-1, "failed to get time");
    return ts;
}

static inline struct timespec get_time_cpu_thread() {
    struct timespec ts;
    int ok = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    log_print_error_if(ok == (clock_t)-1, "failed to get time");
    return ts;
}

// This function is useful sometimes, such as for low level string building,
// but I cannot think of a decent name. I use copied as in "copied <size> bytes",
// but that is weak...
// Alternatives: copy_size, copy_bytes, memcpyd (same as current, but clearer that it inherits from memcpy).
static inline size_t copied(void *to, const void *from, size_t size) {
    memcpy(to, from, size);
    return size;
}

// I used to use memcpy with zero bytes instead of branch, turns out that is slow af.
static inline void memcpy_if(void *to, const void *from, size_t size, bool b) {
    if (b)
        memcpy(to, from, size);
}

#define zero(t) memset(&t,0,sizeof(t))
#define smemset(to, num, type, count) memset(to, num, sizeof(type) * count)
#define smemcpy(to, from, type, count) memcpy(to, from, sizeof(type) * count)
#define smemmove(to, from, type, count) memmove(to, from, sizeof(type) * count)
#define c_array_len(a) (sizeof(a)/sizeof(a[0]))
#define carrlen(a) (sizeof(a)/sizeof(a[0]))
#define cstr_as_array_len(a) ((sizeof(a)/sizeof(a[0])) - 1)
#define cstr_arrlen(a) ((sizeof(a)/sizeof(a[0])) - 1)

#define smemcpy_if(to, from, type, count, b) \
    memcpy_if(to, from, sizeof(type) * count, b)

#ifndef _WIN32

/* bit manipulation */
static inline int ctz16(unsigned short a) {
    // @Note This has to be copied between compiler directives because gcc will not compile
    // tzcnt16 with only one leading undescore. I assume this is a compiler bug, because tzcnt32
    // and 64 both want one leading underscore...
    return __tzcnt_u16(a);
}
static inline int ctz32(unsigned int a) {
    return _tzcnt_u32(a);
}
static inline int ctz64(uint64 a) {
    return _tzcnt_u64(a);
}
static inline int clz16(uint16 mask) {
    return __builtin_clzs(mask);
}
static inline int clz32(uint32 mask) {
    return __builtin_clz(mask);
}
static inline int clz64(uint64 mask) {
    return __builtin_clzl(mask);
}

//
// I am not sure about the best way to do pop counts. Writing explicitly the type in the function call
// seems useful, but having the compiler just use the type is also nice. Furthermore, it seems like these
// are super safe using shorter widths than expected by the function, such as:
//        uint64 x = Max; uint16 *y = &x; popcnt(*y);
// But using the bigger widths than expected does not work. Idk if there is any overhead to using the bigger types
// for smaller widths (then I never have to think about the type in the function name). I would expect that they
// are implemented identically.
//
// I am going to try always using the 64 bit version, and seeing if I get any bugs. I feel like if I get somewhat used
// to not specifying the type, but having to only in the case of 64 bit, I will start making mistakes. So I will use
// the option where I never have to consider it.
//
// #define popcnt(x) __builtin_popcount(x)
//
// Extending this point, gcc documents type generic versions __builtin_ctzg and __builtin_ctzg. But when using them
// they are undefined. These would also be very nice, but as I say, undefined. WTF?
// I am going to have a go using macro versions of these using subtractions. I want to see if run into stuff. From
// preliminary tests, its seems like they work fine with optimizations on, and I would have thought that the
// behviour would always be the same since they are instrinsics (as in their behaviour would not change regardless
// of the context they are in), but we will see I guess.
//

#define popcnt(x) __builtin_popcountl(x)

#define ctzc(x) (__builtin_ctz(x)-24)
#define ctzs(x) (__builtin_ctz(x)-16)
#define ctzi(x) __builtin_ctz(x)
#define ctzl(x) __builtin_ctzl(x)

#define ctz(x) __builtin_ctzl(x)

#define clzc(x) (__builtin_clz(x)-24)
#define clzs(x) (__builtin_clz(x)-16)
#define clz(x) __builtin_clz(x)
#define clzl(x) __builtin_clzl(x)

static inline int popcnt8(uint8 num) {
    uint32 tmp = num;
    tmp &= 0x000000ff;
    return (int)__builtin_popcount(tmp);
}
static inline int popcnt16(uint16 num) {
    uint32 tmp = num;
    tmp &= 0x0000ffff; // just to be sure;
    return (int)__builtin_popcount(tmp);
}
static inline int popcnt32(uint32 num) {
    return (int)__builtin_popcount(num);
}
static inline int popcnt64(uint64 num) {
    return (int)__builtin_popcountl(num);
}
static inline int pop_count16(uint16 num) {
    uint32 tmp = num;
    tmp &= 0x0000ffff; // just to be sure;
    return (int)__builtin_popcount(tmp);
}
static inline int pop_count32(uint32 num) {
    return (int)__builtin_popcount(num);
}
static inline int pop_count64(uint64 num) {
    return (int)__builtin_popcountl(num);
}

#else

static inline int ctz16(unsigned short a) {
    return (int)_tzcnt_u16(a);
}
static inline int ctz32(unsigned int a) {
    return (int)_tzcnt_u32(a);
}
static inline int ctz64(uint64 a) {
    return (int)_tzcnt_u64(a);
}
static inline int clz16(uint16 mask) {
    return __lzcnt16(mask);
}
static inline int clz32(uint32 mask) {
    return __lzcnt(mask);
}
static inline int clz64(uint64 mask) {
    return __lzcnt64(mask);
}
static inline int pop_count16(uint16 num) {
    return (int)__popcnt16(num);
}
static inline int pop_count32(uint32 num) {
    return (int)__popcnt(num);
}
static inline int pop_count64(uint64 num) {
    return (int)__popcnt64(num);
}
#endif // WIN32 or not

static inline int packed_sparse_array_index_to_bit(uint i, uint32 bits) {
    bits &= ~(0xffffffff << i);
    return pop_count32(bits);
}

static inline uint tzclr(uint64 *mask)
{
    uint tz = ctz(*mask);
    *mask &= ~(1<<tz);
    return tz;
}

// ma == mask array, ip == in place
static inline uint64 ma_and(uint64 *ma, uint64 i)
{
    uint64 one = 1;
    return ma[i>>6] & (one<<(i & 63));
}

static inline uint64 ma_or(uint64 *ma, uint64 i)
{
    uint64 one = 1;
    return ma[i>>6] | (one<<(i & 63));
}

static inline uint64 ma_and_not(uint64 *ma, uint64 i)
{
    uint64 one = 1;
    return ma[i>>6] & ~(one<<(i & 63));
}

static inline uint64 ma_or_if(uint64 *ma, uint64 i, bool b)
{
    uint64 one = 1 & b;
    return ma[i>>6] | (one<<(i & 63));
}

static inline void ma_or_if_ip(uint64 *ma, uint64 i, bool b)
{
    uint64 one = 1 & b;
    ma[i>>6] |= (one<<(i & 63));
}

static inline bool flag_check(uint64 flags, uint64 bit) {
    return (flags & bit) != 0;
}

static inline bool mask_array_check_no_mod(uint64 *masks, uint index) {
    return masks[index >> 6] & (one64 << (index & 63));
}

static inline void mask_array_set_no_mod(uint64 *masks, uint index) {
    masks[index >> 6] |= (one64 << (index & 63));
}

#endif // include guard
