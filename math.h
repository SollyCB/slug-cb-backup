#ifndef SOL_MATH_H_INCLUDE_GUARD_
#define SOL_MATH_H_INCLUDE_GUARD_

#include "defs.h"

// @Todo Idk if this is too big/too small. Same magnitude as used in test.c.
#define FLOAT_ERROR 0.000001

static inline bool feq(float a, float b)
{
    return fabsf(a - b) < FLOAT_ERROR;
}

static inline float lerp(float a, float b, float c)
{
    return a + c * (b - a);
}

static inline float clamp(float num, float min, float max)
{
    if (num > max)
        num = max;
    else if (num < min)
        num = min;
    return num;
}

#define PI 3.1415926
#define PI_OVER_180 0.01745329

static inline float radf(float x) {
    return x * PI_OVER_180;
}

typedef struct {
    float x;
    float y;
    float z;
    float w;
} vector cl_align(16);

struct trs {
    vector t;
    vector r;
    vector s;
};

typedef struct matrix {
    float m[16];
} matrix cl_align(16);

static inline vector get_vector(float x, float y, float z, float w)
{
    return (vector) {.x = x, .y = y, .z = z, .w = w};
}

static inline void get_trs(vector t, vector r, vector s, struct trs *trs)
{
    *trs = (struct trs) {
        .t = t,
        .r = r,
        .s = s,
    };
}

static inline void print_matrix(matrix *m)
{
    print("[\n");
    const uint cols[] = {0,4,8,12};
    uint i,j;
    for(i=0;i<4;++i) {
        print("    ");
        for(j=0;j<4;++j)
            print("%f, ", m->m[cols[j]+i]);
        print("\n");
    }
    print("]\n");
}

static inline void println_vector(vector v)
{
    print("[%f, %f, %f, %f]\n", v.x, v.y, v.z, v.w);
}

static inline void array_to_vector(float *arr, vector v)
{
    smemcpy(&v, arr, *arr, 4);
}

static inline vector scalar_mul_vector(vector v, float s)
{
    __m128 a = _mm_load_ps(&v.x);
    __m128 b = _mm_set1_ps(s);
    a = _mm_mul_ps(a,b);
    vector r;
    _mm_store_ps(&r.x, a);
    return r;
}
#define scale_vector(v, s) scalar_mul_vector(v, s)

static inline vector scalar_div_vector(vector v, float s)
{
    __m128 a = _mm_load_ps(&v.x);
    __m128 b = _mm_set1_ps(s);
    a = _mm_div_ps(a,b);
    vector r;
    _mm_store_ps(&r.x, a);
    return r;
}

static inline vector mul_vector(vector v1, vector v2)
{
    __m128 a = _mm_load_ps(&v1.x);
    __m128 b = _mm_load_ps(&v2.x);
    a = _mm_mul_ps(a,b);
    vector r;
    _mm_store_ps(&r.x, a);
    return r;
}

static inline float dot(vector v1, vector v2)
{
    vector v3 = mul_vector(v1, v2);
    return v3.x + v3.y + v3.z;
}

static inline vector cross(vector p, vector q)
{
    vector ret;
    ret.x = p.y * q.z - p.z * q.y;
    ret.y = p.z * q.x - p.x * q.z;
    ret.z = p.x * q.y - p.y * q.x;
    return ret;
}

static inline vector add_vector(vector v1, vector v2)
{
    __m128 a = _mm_load_ps(&v1.x);
    __m128 b = _mm_load_ps(&v2.x);
    a = _mm_add_ps(a,b);
    vector r;
    _mm_store_ps(&r.x, a);
    return r;
}

static inline vector sub_vector(vector v1, vector v2)
{
    __m128 a = _mm_load_ps(&v1.x);
    __m128 b = _mm_load_ps(&v2.x);
    a = _mm_sub_ps(a,b);
    vector r;
    _mm_store_ps(&r.x, a);
    return r;
}

static inline float magnitude_vector(vector v) {
    __m128 a = _mm_load_ps(&v.x);
    __m128 b = a;
    a = _mm_mul_ps(a,b);
    float *f = (float*)&a;
    return sqrtf(f[0] + f[1] + f[2]);
}

static inline vector normalize(vector v) {
    float f = magnitude_vector(v);
    vector r = scalar_div_vector(v, f);
    r.w = 0;
    return r;
}

static inline vector lerp_vector(vector a, vector b, float c) {
    vector ret;
    ret = sub_vector(b, a);
    ret = scalar_mul_vector(ret, c);
    return add_vector(a, ret);
}

// angle in radians
    static inline vector quaternion(float angle, vector v)
{
    float f = angle/2;
    float sf = sinf(f);
    vector r;
    __m128 a;
    __m128 b;
    a = _mm_load_ps(&v.x);
    b = _mm_set1_ps(sf);
    a = _mm_mul_ps(a, b);
    _mm_store_ps(&r.x, a);
    r.w = cosf(f);
    return r;
}

// equivalent to applying rotation q2, followed by rotation q1
static inline vector hamilton_product(vector *q2, vector *q1)
{
    return (vector) {
        .w = q1->w * q2->w - q1->x * q2->x - q1->y * q2->y - q1->z * q2->z,
        .x = q1->w * q2->x + q1->x * q2->w + q1->y * q2->z - q1->z * q2->y,
        .y = q1->w * q2->y - q1->x * q2->z + q1->y * q2->w + q1->z * q2->x,
        .z = q1->w * q2->z + q1->x * q2->y - q1->y * q2->x + q1->z * q2->w,
    };
}

static inline void copy_matrix(matrix *to, matrix *from)
{
    __m128i a = _mm_load_si128((__m128i*)(from->m+0));
    __m128i b = _mm_load_si128((__m128i*)(from->m+4));
    __m128i c = _mm_load_si128((__m128i*)(from->m+8));
    __m128i d = _mm_load_si128((__m128i*)(from->m+12));
    _mm_store_si128((__m128i*)(to->m+0),a);
    _mm_store_si128((__m128i*)(to->m+4),b);
    _mm_store_si128((__m128i*)(to->m+8),c);
    _mm_store_si128((__m128i*)(to->m+12),d);
}

static inline void identity_matrix(matrix *m)
{
    __m128 a = _mm_set_ps(0,0,0,1);
    __m128 b = _mm_set_ps(0,0,1,0);
    __m128 c = _mm_set_ps(0,1,0,0);
    __m128 d = _mm_set_ps(1,0,0,0);
    _mm_store_ps(m->m+ 0, a);
    _mm_store_ps(m->m+ 4, b);
    _mm_store_ps(m->m+ 8, c);
    _mm_store_ps(m->m+12, d);
}

// @Optimise Idk if using a global here is faster than initializing the matrix.
// When I was testing this, my testing was majorly flawed.
matrix IDENTITY_MATRIX = {
    .m = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1,
    },
};

inline static bool is_ident(matrix *m)
{
    return memcmp(m, &IDENTITY_MATRIX, sizeof(*m)) == 0;
}

// @Optimise It looks like letting the compiler decide how to init stuff is
// better. See above implementation of identity_matrix
static inline void count_identity_matrix(uint count, matrix *m)
{
    __m128i a = (__m128i)_mm_set_ps(0,0,0,1);
    __m128i b = (__m128i)_mm_set_ps(0,0,1,0);
    __m128i c = (__m128i)_mm_set_ps(0,1,0,0);
    __m128i d = (__m128i)_mm_set_ps(1,0,0,0);
    for(uint i = 0; i < count; ++i) {
        _mm_store_si128((__m128i*)(m[i].m+0), a);
        _mm_store_si128((__m128i*)(m[i].m+4), b);
        _mm_store_si128((__m128i*)(m[i].m+8), c);
        _mm_store_si128((__m128i*)(m[i].m+12), d);
    }
}

static inline void scale_matrix(vector *v, matrix *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0] = v->x;
    m->m[5] = v->y;
    m->m[10] = v->z;
    m->m[15] = 1;
}

static inline void translation_matrix(vector *v, matrix *m)
{
    identity_matrix(m);
    m->m[12] = v->x;
    m->m[13] = v->y;
    m->m[14] = v->z;
}

static inline void rotation_matrix(vector *r, matrix *m)
{
    __m128 a = (__m128)_mm_load_si128((__m128i*)r);
    __m128 b = a;
    a = _mm_mul_ps(a,b);
    float *f = (float*)&a;

    float xy = 2 * r->x * r->y;
    float xz = 2 * r->x * r->z;
    float yz = 2 * r->y * r->z;
    float wx = 2 * r->w * r->x;
    float wy = 2 * r->w * r->y;
    float wz = 2 * r->w * r->z;

    identity_matrix(m);

    m->m[0] = f[3] + f[0] - f[1] - f[2];
    m->m[4] = xy - wz;
    m->m[8] = xz + wy;

    m->m[1] = xy + wz;
    m->m[5] = f[3] - f[0] + f[1] - f[2];
    m->m[9] = yz - wx;

    m->m[2] = xz - wy;
    m->m[6] = yz + wx;
    m->m[10] = f[3] - f[0] - f[1] + f[2];
}

static inline void mul_matrix(matrix *x, matrix *y, matrix *z)
{
    const uint cols[] = {0,4,8,12};
    __m128 a;
    __m128 b;
    float *f = (float*)&b;
    matrix m;
    uint i,j;
    for(i=0;i<4;++i) {
        a = _mm_set_ps(x->m[12+i],x->m[8+i],x->m[4+i],x->m[0+i]);
        for(j=0;j<4;++j) {
            b = _mm_load_ps(y->m + 4*j);
            b = _mm_mul_ps(a,b);
            m.m[cols[j]+i] = f[0]+f[1]+f[2]+f[3];
        }
    }
    copy_matrix(z,&m);
}

static inline void convert_trs(struct trs *trs, matrix *ret)
{
    matrix t,r,s;
    translation_matrix(&trs->t, &t);
    rotation_matrix(&trs->r, &r);
    scale_matrix(&trs->s, &s);
    mul_matrix(&t,&r,&r);
    mul_matrix(&r,&s,ret);
}

static inline void scalar_mul_matrix(matrix *m, float f)
{
    __m128 a = (__m128)_mm_load_si128((__m128i*)(m->m+0));
    __m128 b = (__m128)_mm_load_si128((__m128i*)(m->m+4));
    __m128 c = (__m128)_mm_load_si128((__m128i*)(m->m+8));
    __m128 d = (__m128)_mm_load_si128((__m128i*)(m->m+12));
    __m128 e = _mm_set1_ps(f);
    a = _mm_mul_ps(a,e);
    b = _mm_mul_ps(b,e);
    c = _mm_mul_ps(c,e);
    d = _mm_mul_ps(d,e);
    _mm_store_si128((__m128i*)(m->m+0),(__m128i)a);
    _mm_store_si128((__m128i*)(m->m+4),(__m128i)b);
    _mm_store_si128((__m128i*)(m->m+8),(__m128i)c);
    _mm_store_si128((__m128i*)(m->m+12),(__m128i)d);
    m->m[15] = 1;
}

static inline vector mul_matrix_vector(matrix *m, vector *p)
{
    __m128 a;
    __m128 b;
    __m128 c;

    a = _mm_load_ps(m->m + 0);
    b = _mm_set1_ps(p->x);
    c = _mm_mul_ps(a, b);

    a = _mm_load_ps(m->m + 4);
    b = _mm_set1_ps(p->y);
    a = _mm_mul_ps(a, b);
    c = _mm_add_ps(a, c);

    a = _mm_load_ps(m->m + 8);
    b = _mm_set1_ps(p->z);
    a = _mm_mul_ps(a, b);
    c = _mm_add_ps(a, c);

    a = _mm_load_ps(m->m + 12);
    b = _mm_set1_ps(p->w);
    a = _mm_mul_ps(a, b);
    c = _mm_add_ps(a, c);

    float *f = (float*)&c;
    return get_vector(f[0], f[1], f[2], f[3]);
}

static inline void transpose(matrix *m)
{
    uint cols[] = {0, 4, 8, 12};
    int cnt = -1;
    matrix t;
    for(uint i=0; i < 16; ++i) {
        cnt += (i & 3) == 0;
        t.m[cols[i & 3] + cnt] = m->m[i];
    }
    copy_matrix(m, &t);
}

static inline void view_matrix(vector pos, vector fwd, matrix *m)
{
    vector fo = get_vector(0, 0, 1, 0);
    vector fn = normalize(fwd);
    vector ax = cross(fo, fn);
    normalize(ax);

    matrix mr;
    float angle = acosf(dot(fo, fn));
    vector rot = quaternion(angle, ax);
    rotation_matrix(&rot, &mr);

    matrix mt;
    vector vt = get_vector(-pos.x, -pos.y, -pos.z, 0);
    translation_matrix(&vt, &mt);

    mul_matrix(&mr, &mt, m);
}

// args: horizontal fov, 1 / aspect ratio, near plane, far plane
static inline void proj_matrix(float fov, float a, float n, float f, matrix *m)
{
    float e = 1 / tanf(fov / 2);
    float l = -n / e;
    float r = n / e;
    float t = (a * n) / e;
    float b = -(a * n) / e;

    memset(m, 0, sizeof(*m));
    m->m[0] = (2 * n) / (r - l);
    m->m[5] = -(2 * n) / (t - b); // negate because Vulkan - or not?
    m->m[8] = (r + l) / (r - l);
    m->m[9] = (t + b) / (t - b);
    m->m[10] = -(f + n) / (f - n);
    m->m[11] = -1;
    m->m[14] = -(2 * n * f) / (f - n);
}

#endif // include guard
