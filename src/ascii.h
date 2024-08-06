#ifndef SOL_ASCII_H_IMPL
#define SOL_ASCII_H_IMPL

#include "defs.h"
#include <immintrin.h>

// This file uses a lot of unaligned loads as lots of these functions are used for parsing
// text, where a char could be at any place in the file. It is simple to make aligned
// versions (see Appendix at EOF for examples), but reading the intel intrinsics docs, there
// seems to be no penalty to unaligned loads vs aligned:
//
//     movdqu xmm, m128
//         Latency and Throughput
//         Architecture	Latency	Throughput (CPI)
//         Alderlake	    6	0.333333333
//         Icelake Xeon	    6	0.55
//         Sapphire Rapids	6	0.333333333
//         Skylake	        6	0.5
//
//     movdqa xmm, m128
//         Latency and Throughput
//         Architecture	Latency	Throughput (CPI)
//         Alderlake	    6	0.333333333
//         Icelake Xeon	    6	0.55
//         Sapphire Rapids	6	0.333333333
//         Skylake	        6	0.5
//
// This seems pretty bizarre, but I cannot imagine that the tables are wrong. My guess is
// I am misunderstanding the implications of the table, as maybe the load itself is longer,
// but the instructions themselves take the same time, but that seems a silly notion, as
// the instruction would be considered complete once the data is there. Idk... but I am
// not going to add cycles aligning the addresses if I cannot see a proper benefit.

static inline uint uint_to_ascii(uint32 num, char *buf) {
    uint cap = num < 10 ? 1 : log10(num) + 1;
    for(uint i = 0; i < cap; ++i) {
        buf[cap-i-1] = (num % 10) + '0';
        num /= 10;
    }
    buf[cap] = 0;
    return cap;
}

static inline uint32 simd_find_char(const char *data, char c) {
    __m128i a;
    __m128i b = _mm_set1_epi8(c);
    uint16 m0 = 0;
    uint32 i;
    for(i = 0; !m0; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        a = _mm_cmpeq_epi8(a, b);
        m0 = _mm_movemask_epi8(a);
    }
    return i + ctz16(m0) - 16;
}

static inline uint32 simd_find_number_char(const char *data) {
    __m128i a;
    __m128i b = _mm_set1_epi8('0' - 1);
    __m128i c = _mm_set1_epi8('9' + 1);
    __m128i d, e;
    uint16 m0 = 0;
    uint32 i;
    for(i = 0; !m0; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        d = _mm_cmpgt_epi8(a, b);
        e = _mm_cmplt_epi8(a, c);
        d = _mm_and_si128(d, e);
        m0 = _mm_movemask_epi8(d);
    }
    return i + ctz16(m0) - 16;
}

static inline bool simd_find_char_interrupted(const char *data, char c0, char c1, uint32 *ret) {
    __m128i a;
    __m128i b = _mm_set1_epi8(c0);
    __m128i c = _mm_set1_epi8(c1);
    __m128i d;
    uint16 m0 = 0;
    uint16 m1 = 0;
    uint32 i;
    for(i = 0; !m0 && !m1; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        d = _mm_cmpeq_epi8(a, b);
        m0 = _mm_movemask_epi8(d);
        d = _mm_cmpeq_epi8(a, c);
        m1 = _mm_movemask_epi8(d);
    }
    uint32 tz0 = ctz16(m0) | max32_if_false(m0);
    uint32 tz1 = ctz16(m1) | max32_if_false(m1);
    *ret = 0;
    *ret += (tz0 + i - 16) & max32_if_true(tz0 < tz1);
    *ret += (tz1 + i - 16) & max32_if_true(tz1 < tz0);
    return tz0 < tz1;
}

static inline uint16 simd_match_char(const char *data, char c) {
    __m128i a = _mm_loadu_si128((__m128i*)(data));
    __m128i b = _mm_set1_epi8(c);
    a = _mm_cmpeq_epi8(a, b);
    return _mm_movemask_epi8(a);
}

// only for contiguous number chars, does not count '.' or 'e'
static inline uint32 simd_ascii_integer_len(const char *data) {
    __m128i a;
    __m128i b = _mm_set1_epi8('0' - 1);
    __m128i c = _mm_set1_epi8('9' + 1);
    __m128i d, e;
    uint16 m0 = 0xffff;
    uint32 i;
    for(i = 0; m0 == 0xffff; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        d = _mm_cmpgt_epi8(a, b);
        e = _mm_cmplt_epi8(a, c);
        d = _mm_and_si128(d, e);
        m0 = _mm_movemask_epi8(d);
    }
    m0 &= ~(0xffff << ctz16(~m0));
    return i - 16 + pop_count16(m0);
}

// Will count a trailing 'e', e.g. 123.456e, validate floats elsewhere.
static inline uint32 simd_ascii_double_len(const char *data) {
    __m128i a;
    __m128i b = _mm_set1_epi8('0' - 1);
    __m128i c = _mm_set1_epi8('9' + 1);
    __m128i d = _mm_set1_epi8('-');
    __m128i e = _mm_set1_epi8('e');
    __m128i f = _mm_set1_epi8('.');
    __m128i g,h;
    uint16 m0 = 0xffff;
    uint32 i;
    for(i = 0; m0 == 0xffff; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        g = _mm_cmpgt_epi8(a, b);
        h = g;
        g = _mm_cmplt_epi8(a, c);
        h = _mm_and_si128(h, g);
        g = _mm_cmpeq_epi8(a, d);
        h = _mm_or_si128(h, g);
        g = _mm_cmpeq_epi8(a, e);
        h = _mm_or_si128(h, g);
        g = _mm_cmpeq_epi8(a, f);
        h = _mm_or_si128(h, g);
        m0 = _mm_movemask_epi8(h);
    }
    return i - 16 + ctz16(~m0);
}

static inline uint32 simd_ascii_double_e(const char *data) {
    __m128i a;
    __m128i b = _mm_set1_epi8('0' - 1);
    __m128i c = _mm_set1_epi8('9' + 1);
    __m128i d = _mm_set1_epi8('.');
    __m128i e, f;
    uint16 m0 = 0xffff;
    uint32 i;
    for(i = 0; m0 == 0xffff; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        e = _mm_cmpgt_epi8(a, b);
        f = _mm_cmplt_epi8(a, c);
        e = _mm_and_si128(e, f);
        f = _mm_cmpeq_epi8(a, d);
        e = _mm_or_si128(e, f);
        m0 = _mm_movemask_epi8(e);
    }
    int tz0 = ctz16(~m0);
    return (i + tz0 - 16) | max32_if_false(data[i + tz0 - 16] == 'e');
}

static inline uint32 simd_skip_to_whitespace(const char *data) {
    __m128i a;
    __m128i b = _mm_set1_epi8(' ');
    __m128i c = _mm_set1_epi8('\n');
    __m128i d, e;
    uint16 m0 = 0;
    uint32 i;
    for(i = 0; !m0; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        d = _mm_cmpeq_epi8(a, b);
        e = _mm_cmpeq_epi8(a, c);
        d = _mm_or_si128(d, e);
        m0 = _mm_movemask_epi8(d);
    }
    return i + ctz16(m0) - 16;
}

static inline uint32 simd_skip_over_whitespace(const char *data) {
    __m128i a;
    __m128i b = _mm_set1_epi8(' ');
    __m128i c = _mm_set1_epi8('\n');
    __m128i d, e;
    uint16 m0 = 0xffff;
    uint32 i;
    for(i = 0; m0 == 0xffff; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        d = _mm_cmpeq_epi8(a, b);
        e = _mm_cmpeq_epi8(a, c);
        d = _mm_or_si128(d, e);
        m0 = _mm_movemask_epi8(d);
    }
    return i + ctz16(~m0) - 16;
}

static inline bool simd_find_char_interrupted_by_not_whitespace(const char *data, char x) {
    __m128i a;
    __m128i b = _mm_set1_epi8(x);
    __m128i c = _mm_set1_epi8(' ');
    __m128i d = _mm_set1_epi8('\n');
    __m128i e;
    uint16 m0;
    uint16 m1 = 0xffff;
    for(int i = 0; m1 == 0xffff; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data + i));
        e = _mm_cmpeq_epi8(a, b);
        m0 = _mm_movemask_epi8(e);
        e = _mm_cmpeq_epi8(a, c);
        m1 = _mm_movemask_epi8(e);
        e = _mm_cmpeq_epi8(a, d);
        m1 |= _mm_movemask_epi8(e);
    }
    int lz0 = ctz16(m0);
    int lz1 = ctz16(~m1);
    return lz0 == lz1 && pop_count16(m0);
}

static inline bool simd_find_char_interrupted_by_not_whitespace_reverse(const char *data, char x) {
    __m128i a;
    __m128i b = _mm_set1_epi8(x);
    __m128i c = _mm_set1_epi8(' ');
    __m128i d = _mm_set1_epi8('\n');
    __m128i e;
    uint16 m0;
    uint16 m1 = 0xffff;
    for(int i = 0; m1 == 0xffff; i += 16) {
        a = _mm_loadu_si128((__m128i*)(data - i));
        e = _mm_cmpeq_epi8(a, b);
        m0 = _mm_movemask_epi8(e);
        e = _mm_cmpeq_epi8(a, c);
        m1 = _mm_movemask_epi8(e);
        e = _mm_cmpeq_epi8(a, d);
        m1 |= _mm_movemask_epi8(e);
    }
    int lz0 = clz16(m0);
    int lz1 = clz16(~m1);
    return lz0 == lz1 && pop_count16(m0);
}

static inline int64 ascii_to_integer(const char *data) {
    data += simd_find_number_char(data);
    int64 ret = 0;
    for(uint32 i = 0; data[i] >= '0' && data[i] <= '9'; ++i) {
        ret *= 10;
        ret += data[i] - '0';
    }
    uint64 tmp = ret;
    ret -= tmp & max64_if_true(data[-1] == '-');
    ret -= tmp & max64_if_true(data[-1] == '-');
    return ret;
}

static inline double ascii_to_double(const char *data) {
    data += simd_find_number_char(data);
    int len_bd = simd_ascii_integer_len(data);
    int len_ad = simd_ascii_integer_len(data + len_bd + 1) & max32_if_true(data[len_bd] == '.');
    int64 bd = ascii_to_integer(data);
    int64 ad = ascii_to_integer(data + ((len_bd + 1) & max32_if_true(len_ad)));

    double ret = ad & max64_if_true(len_ad);
    ret /= pow(10, len_ad);
    ret += bd < 0 ? -bd : bd;

    uint32 e_idx = simd_ascii_double_e(data);
    e_idx &= max32_if_true(e_idx != Max_u32);
    int64 exp = ascii_to_integer(data + e_idx);
    exp &= max64_if_true(e_idx);

    ret *= pow(10, exp);
    ret = data[-1] == '-' && ret > 0 ? -ret : ret;

    return ret;
}

// Appendix

/* Aligned load version of 'simd_find_char_interrupted_by_not_whitespace_reverse'
    __m128i a;
    __m128i b = _mm_set1_epi8(x);
    __m128i c = _mm_set1_epi8(' ');
    __m128i d = _mm_set1_epi8('\n');
    __m128i e;
    uint16 m0;
    uint16 m1 = 0xffff;
    bool ret = true;

    data = data - ((uint64)data & 15);

    a = _mm_load_si128((__m128i*)(data));
    e = _mm_cmpeq_epi8(a, b);
    m0 = _mm_movemask_epi8(e);
    e = _mm_cmpeq_epi8(a, c);
    m1 = _mm_movemask_epi8(e);
    e = _mm_cmpeq_epi8(a, d);
    m1 |= _mm_movemask_epi8(e);

    m0 |= 0xffff << ((uint64)data & 15);
    m1 |= 0xffff << ((uint64)data & 15);

    for(int i = 16; m1 == 0xffff; i += 16) {
        a = _mm_load_si128((__m128i*)(data - i));
        e = _mm_cmpeq_epi8(a, b);
        m0 = _mm_movemask_epi8(e);
        e = _mm_cmpeq_epi8(a, c);
        m1 = _mm_movemask_epi8(e);
        e = _mm_cmpeq_epi8(a, d);
        m1 |= _mm_movemask_epi8(e);
    }
    int lz0 = clz16(m0);
    int lz1 = clz16(~m1);
    return lz0 == lz1 && pop_count16(m0);
*/

#endif // include guard
