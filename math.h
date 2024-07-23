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

// should be constructed as:
//     bottom left near, bottom left far, bottom right far, bottom right near,
//     top left near, top left far, top right far, top right near,
struct box {
    vector p[8];
};

static inline void get_box(vector bln, vector blf, vector brf, vector brn,
                           vector tln, vector tlf, vector trf, vector trn, struct box *b)
{
    b->p[0] = bln; b->p[1] = blf; b->p[2] = brf; b->p[3] = brn;
    b->p[4] = tln; b->p[5] = tlf; b->p[6] = trf; b->p[7] = trn;
}

struct trs {
    vector t;
    vector r;
    vector s;
};

typedef struct matrix {
    float m[16];
} matrix cl_align(16);

// glsl compatibility for cpu interactive structs
#define vec4 vector
#define mat4 matrix
typedef uint32 uvec4[4] cl_align(16);

static inline vector get_vector(float x, float y, float z, float w)
{
    return (vector) {.x = x, .y = y, .z = z, .w = w};
}
#define vector4(x, y, z, w) get_vector(x, y, z, w)
#define vector3(x, y, z)    get_vector(x, y, z, 0)
#define vector2(x, y)       get_vector(x, y, 0, 0)

static inline void get_matrix(vector colx, vector coly, vector colz, vector colw, matrix *m)
{
    __m128 a = _mm_load_ps(&colx.x);
    __m128 b = _mm_load_ps(&coly.x);
    __m128 c = _mm_load_ps(&colz.x);
    __m128 d = _mm_load_ps(&colw.x);
    _mm_store_ps(&m->m[ 0], a);
    _mm_store_ps(&m->m[ 4], b);
    _mm_store_ps(&m->m[ 8], c);
    _mm_store_ps(&m->m[12], d);
}
#define matrix4(x, y, z, w, m) get_matrix(x, y, z, w, m)
#define matrix3(x, y, z, m) get_matrix(x, y, z, (vector){0}, m)

static inline void load_count_matrices_ua(uint count, float *from, matrix *to)
{
    for(uint i=0; i < count; ++i)
        matrix4(vector4(from[i*16+ 0], from[i*16+ 1], from[i*16+ 2], from[i*16+ 3]),
                vector4(from[i*16+ 4], from[i*16+ 5], from[i*16+ 6], from[i*16+ 7]),
                vector4(from[i*16+ 8], from[i*16+ 9], from[i*16+10], from[i*16+11]),
                vector4(from[i*16+12], from[i*16+13], from[i*16+14], from[i*16+15]), &to[i]);
}

static inline vector vector3_w(vector v, float w)
{
    v.w = w;
    return v;
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

static inline void print_vector(vector v)
{
    print("[%f, %f, %f, %f]", v.x, v.y, v.z, v.w);
}

static inline void println_vector(vector v)
{
    print("[%f, %f, %f, %f]\n", v.x, v.y, v.z, v.w);
}

static inline void print_box(struct box *b)
{
    for(uint i=0; i < carrlen(b->p) / 2; ++i) {
        if (i == 0) {
            print_vector(b->p[i]); print(" | box %uh", b);
        } else {
            println_vector(b->p[i]);
        }
    }
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

static inline vector div_vector(vector v1, vector v2)
{
    __m128 a = _mm_load_ps(&v1.x);
    __m128 b = _mm_load_ps(&v2.x);
    a = _mm_div_ps(a,b);
    vector r;
    _mm_store_ps(&r.x, a);
    return r;
}

static inline float sq_vector(vector v)
{
    v = mul_vector(v, v);
    return v.x + v.y + v.z + v.w;
}

static inline float dot(vector v1, vector v2)
{
    vector v3 = mul_vector(v1, v2);
    return v3.x + v3.y + v3.z + v3.w;
}

// w component returned as 0
static inline vector cross(vector p, vector q)
{
    vector ret;
    ret.x = p.y * q.z - p.z * q.y;
    ret.y = p.z * q.x - p.x * q.z;
    ret.z = p.x * q.y - p.y * q.x;
    ret.w = 0;
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

static inline float vector_len(vector v) {
    __m128 a = _mm_load_ps(&v.x);
    __m128 b = a;
    a = _mm_mul_ps(a,b);
    float *f = (float*)&a;
    return sqrtf(f[0] + f[1] + f[2]);
}
#define magnitude_vector(v) vector_len(v)

static inline vector normalize(vector v) {
    float f = vector_len(v);
    return scalar_div_vector(v, f);
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
    v = normalize(v);
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

static inline vector invert_quaternion(vector q)
{
    return vector4(-q.x, -q.y, -q.z, q.w);
}

// equivalent to applying rotation q2, followed by rotation q1
static inline vector hamilton_product(vector q1, vector q2)
{
    #if 0
    return (vector) {
        .x = q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
        .y = q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x,
        .z = q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w,
        .w = q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z,
    };
    #endif

    __m128 a,b,c,d,e;

    a = _mm_load_ps(&q2.x);

    b = _mm_set_ps1(q1.w);
    c = _mm_set_ps1(q1.x);
    d = _mm_set_ps1(q1.y);
    e = _mm_set_ps1(q1.z);

    b = _mm_mul_ps(a,b);
    c = _mm_mul_ps(a,c);
    d = _mm_mul_ps(a,d);
    e = _mm_mul_ps(a,e);

    vector x,y,z,w;
    _mm_store_ps(&w.x, b);
    _mm_store_ps(&x.x, c);
    _mm_store_ps(&y.x, d);
    _mm_store_ps(&z.x, e);

    return (vector) { // @Optimise This could likely be done better, idk.
        .x = w.x + x.w + y.z - z.y,
        .y = w.y - x.z + y.w + z.x,
        .z = w.z + x.y - y.x + z.w,
        .w = w.w - x.x - y.y - z.z,
    };
}
#define mul_quaternion(p, q) hamilton_product(p, q)

// rotate like the inverse of a rotation matrix (like a view matrix)
static inline vector rotate_active(vector p, vector q)
{
    vector v = hamilton_product(hamilton_product(invert_quaternion(q), p), q);
    return vector3(v.x, v.y, v.z);
}

// rotate like a rotation matrix
static inline vector rotate_passive(vector p, vector q)
{
    vector v = hamilton_product(hamilton_product(q, p), invert_quaternion(q));
    return vector3(v.x, v.y, v.z);
}

// rotate axis of rotation of p by q
static inline vector rotate_quaternion_axis(vector p, vector q)
{
    vector v = vector3(p.x, p.y, p.z);
    v = rotate_passive(p, q);
    return vector4(v.x, v.y, v.z, p.w);
}

static inline float quaternion_angle(vector q)
{
    return acosf(q.w) * 2;
}

static inline vector quaternion_axis(vector q)
{
    float a = quaternion_angle(q);
    vector r = scalar_div_vector(q, sinf(a/2));
    r.w = 0;
    return r;
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

static inline void count_invert_y_identity_matrix(uint count, matrix *m)
{
    __m128i a = (__m128i)_mm_set_ps(0,0,0,1);
    __m128i b = (__m128i)_mm_set_ps(0,0,-1,0);
    __m128i c = (__m128i)_mm_set_ps(0,1,0,0);
    __m128i d = (__m128i)_mm_set_ps(1,0,0,0);
    for(uint i = 0; i < count; ++i) {
        _mm_store_si128((__m128i*)(m[i].m+0), a);
        _mm_store_si128((__m128i*)(m[i].m+4), b);
        _mm_store_si128((__m128i*)(m[i].m+8), c);
        _mm_store_si128((__m128i*)(m[i].m+12), d);
    }
}

static inline void scale_matrix(vector v, matrix *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0] = v.x;
    m->m[5] = v.y;
    m->m[10] = v.z;
    m->m[15] = 1;
}

static inline void translation_matrix(vector v, matrix *m)
{
    identity_matrix(m);
    m->m[12] = v.x;
    m->m[13] = v.y;
    m->m[14] = v.z;
}

static inline void rotation_matrix(vector r, matrix *m)
{
    __m128 a = (__m128)_mm_load_si128((__m128i*)&r.x);
    __m128 b = a;
    a = _mm_mul_ps(a,b);
    float *f = (float*)&a;

    float xy = 2 * r.x * r.y;
    float xz = 2 * r.x * r.z;
    float yz = 2 * r.y * r.z;
    float wx = 2 * r.w * r.x;
    float wy = 2 * r.w * r.y;
    float wz = 2 * r.w * r.z;

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

    m->m[15] = 1;
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
    translation_matrix(trs->t, &t);
    rotation_matrix(trs->r, &r);
    scale_matrix(trs->s, &s);
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

static inline vector mul_matrix_vector(matrix *m, vector p)
{
    __m128 a;
    __m128 b;
    __m128 c;

    a = _mm_load_ps(m->m + 0);
    b = _mm_set1_ps(p.x);
    c = _mm_mul_ps(a, b);

    a = _mm_load_ps(m->m + 4);
    b = _mm_set1_ps(p.y);
    a = _mm_mul_ps(a, b);
    c = _mm_add_ps(a, c);

    a = _mm_load_ps(m->m + 8);
    b = _mm_set1_ps(p.z);
    a = _mm_mul_ps(a, b);
    c = _mm_add_ps(a, c);

    a = _mm_load_ps(m->m + 12);
    b = _mm_set1_ps(p.w);
    a = _mm_mul_ps(a, b);
    c = _mm_add_ps(a, c);

    // float *f = (float*)&c;
    // return get_vector(f[0], f[1], f[2], f[3]);
    vector v;
    _mm_store_ps(&v.x, c);

    return v;
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

// Inverts a 3x3
static inline bool invert(matrix *x, matrix *y)
{
    float m[3][8] cl_align(16);
    memset(m, 0, sizeof(m));

    m[0][0] = x->m[0]; m[1][0] = x->m[4]; m[2][0] = x->m[ 8];
    m[0][1] = x->m[1]; m[1][1] = x->m[5]; m[2][1] = x->m[ 9];
    m[0][2] = x->m[2]; m[1][2] = x->m[6]; m[2][2] = x->m[10];

    m[0][3] = 1; m[0][4] = 0; m[0][5] = 0;
    m[1][3] = 0; m[1][4] = 1; m[1][5] = 0;
    m[2][3] = 0; m[2][4] = 0; m[2][5] = 1;

    __m128 a,b,c,d,e,f,g;

    for(uint j=0; j < 3; ++j) {
        float max = 0;
        uint r;
        for(uint row=j; row < 3; ++row)
            if (fabs(m[row][j]) > max) {
                max = fabs(m[row][j]);
                r = row;
            }

        if (feq(max, 0))
            return false;

        a = _mm_load_ps(m[j] + 0);
        b = _mm_load_ps(m[j] + 4);

        if (r != j) {
            c = _mm_load_ps(m[r] + 0);
            d = _mm_load_ps(m[r] + 4);
            // @Optimise I feel that I can remove half these stores by avoiding
            // loading the same data later...
            _mm_store_ps(m[r] + 0, a);
            _mm_store_ps(m[r] + 4, b);
            _mm_store_ps(m[j] + 0, c);
            _mm_store_ps(m[j] + 4, d);
            a = c;
            b = d;
        }

        e = _mm_set1_ps(1 / m[j][j]);
        a = _mm_mul_ps(a, e);
        b = _mm_mul_ps(b, e);
        _mm_store_ps(m[j] + 0, a);
        _mm_store_ps(m[j] + 4, b);

        for(r=0; r < 3; ++r) {
            if (r == j)
                continue;

            e = _mm_set1_ps(-m[r][j]);
            f = _mm_mul_ps(e, a);
            g = _mm_mul_ps(e, b);
            c = _mm_load_ps(m[r] + 0);
            d = _mm_load_ps(m[r] + 4);
            c = _mm_add_ps(c, f);
            d = _mm_add_ps(d, g);
            _mm_store_ps(m[r] + 0, c);
            _mm_store_ps(m[r] + 4, d);
        }
    }

    matrix3(vector3(m[0][3], m[0][4], m[0][5]),
            vector3(m[1][3], m[1][4], m[1][5]),
            vector3(m[2][3], m[2][4], m[2][5]), y);

    return true;
}

static inline void invert_transform(matrix *m, matrix *r)
{
    matrix t;
    invert(m, &t);
    t.m[12] = -m->m[12];
    t.m[13] = -m->m[13];
    t.m[14] = -m->m[14];
    t.m[15] = 1;
    *r = t;
}

static inline void view_matrix(vector pos, vector dir, vector up, matrix *m)
{
    vector w = normalize(up);
    vector d = normalize(dir);
    vector r = normalize(cross(d, w));
    vector u = normalize(cross(r, d));

    matrix rot;
    matrix3(vector3(r.x, u.x, -d.x),
            vector3(r.y, u.y, -d.y),
            vector3(r.z, u.z, -d.z), &rot);

    rot.m[15] = 1;

    matrix trn;
    translation_matrix(scale_vector(pos, -1), &trn);

    mul_matrix(&rot, &trn, m);
}

static inline void move_to_camera(vector pos, vector dir, vector up, matrix *m)
{
    vector w = normalize(up);
    vector d = normalize(dir);
    vector r = normalize(cross(d, w));
    vector u = normalize(cross(r, d));

    matrix rot;
    matrix3(vector3( r.x,  r.y,  r.z),
            vector3( u.x,  u.y,  u.z),
            vector3(-d.x, -d.y, -d.z), &rot);

    rot.m[15] = 1;

    matrix trn;
    translation_matrix(pos, &trn);

    mul_matrix(&trn, &rot, m);
}

static inline float focal_length(float fov)
{
    return 1 / tanf(fov / 2);
}

// args: horizontal fov, aspect ratio, near plane, far plane
static inline void perspective_matrix(float fov, float a, float n, float f, matrix *m)
{
    float e = focal_length(fov);

    float l = -n / e;
    float r = n / e;
    float t = (a * n) / e;
    float b = -(a * n) / e;

    memset(m, 0, sizeof(*m));
    m->m[0] = (2 * n) / (r - l);
    m->m[5] = -(2 * n) / (t - b); // negate because Vulkan
    m->m[8] = (r + l) / (r - l);
    m->m[9] = (t + b) / (t - b);
    m->m[10] = -f / (f - n);
    m->m[11] = -1;
    m->m[14] = -(n * f) / (f - n);
}

// t should be y min and b y max because vulkan screen orientation
static inline void ortho_matrix(float l, float r, float b, float t,
                                float n, float f, matrix *m)
{
    matrix4(vector4(2 / (r-l), 0, 0,  0),
            vector4(0, 2 / (t-b), 0,  0), // do not flip, values are passed in flipped
            vector4(0, 0, -1 / (f-n), 0),
            vector4(-(r+l) / (r-l), -(t+b) / (t-b), -(f+n) / (2 * (f-n)) + 0.5, 1),
            m);
}

static inline float dist_point_line(vector p, vector s, vector v)
{
    float q = sq_vector(sub_vector(p, s));
    float r = pow(dot(sub_vector(p, s), v), 2);
    float d = powf(magnitude_vector(v), 2);

    // the sqrtf will nan if this is true because float error gives a negative
    if (feq(q - r / d, 0))
        return 0;

    return sqrtf(q - r / d);
}

static inline bool point_on_line(vector p, vector s, vector v)
{
    return feq(dist_point_line(p, s, v), 0);
}

enum { INTERSECT, LIES_IN, PARALLEL, };
static inline uint intersect_line_plane(vector p, vector s, vector v, vector *ret)
{
    {
        vector n = normalize(vector3(p.x, p.y, p.z));
        if (feq(dot(n, v), 0)) {
            if (feq(dot(n, s) + p.w, 0))
                return LIES_IN;
            else
                return PARALLEL;
        }
    }

    s.w = 1;
    v.w = 0;
    float t = -dot(p, s) / dot(p, v);

    s.w = 0;
    *ret = add_vector(s, scale_vector(v, t));

    return INTERSECT;
}

// point of intersection of three planes, does not check det == 0
static inline vector intersect_three_planes(vector l1, vector l2, vector l3)
{
    matrix m;
    matrix3(vector3(l1.x, l2.x, l3.x), vector3(l1.y, l2.y, l3.y), vector3(l1.z, l2.z, l3.z), &m);

    if (!invert(&m, &m)) {
        print_matrix(&m);
        log_print_error("matrix is not invertible");
    }

    vector d = vector3(-l1.w, -l2.w, -l3.w);
    return mul_matrix_vector(&m, d);
}

// find the point of intersection of two planes l1 and l2, q1 and q2 are points on the respective planes
static inline vector intersect_two_planes_point(vector l1, vector l2, vector q1, vector q2)
{
    matrix m;
    vector v,q,d;
    float d1,d2;

    d1 = dot(vector3(-l1.x, -l1.y, -l1.z), vector3(q1.x, q1.y, q1.z));
    d2 = dot(vector3(-l2.x, -l2.y, -l2.z), vector3(q2.x, q2.y, q2.z));

    v = cross(l1, l2);
    d = vector3(-d1, -d2, 0);
    matrix3(vector3(l1.x, l2.x, v.x), vector3(l1.y, l2.y, v.y), vector3(l1.z, l2.z, v.z), &m);

    if (!invert(&m, &m)) {
        print_matrix(&m);
        log_print_error("matrix is not invertible");
    }

    q = mul_matrix_vector(&m, d);

    return add_vector(q, scale_vector(v, -dot(v, q) / dot(v, v)));
}

static inline vector gram_schmidt(vector n, vector t)
{
    float d = dot(n,t);
    __m128 a = _mm_load_ps(&n.x);
    __m128 b = _mm_load_ps(&t.x);
    __m128 c = _mm_set1_ps(d);
    a = _mm_mul_ps(a,c);
    a = _mm_sub_ps(b,a);
    vector r;
    _mm_store_ps(&r.x, a);
    return r;
}

static inline float tangent_handedness(vector n, vector t1, vector t2)
{
    return dot(cross(n,t1),t2) > 0 ? 1 : -1;
}

void calc_vertex_normals16(
    uint    index_count,
    uint16 *indices,
    uint    vertex_count,
    uint    vertex_stride,
    float  *vertices,
    vector *ret_normals);

void calc_vertex_normals(
    uint    index_count,
    uint   *indices,
    uint    vertex_count,
    uint    vertex_stride,
    float  *vertices,
    vector *ret_normals);

void calc_vertex_tangents16(
    uint       index_count,
    uint16    *indices,
    uint       vertex_count,
    uint       vertex_stride,
    float     *vertices,
    bool       aligned_normals,
    uint       normal_stride,
    float     *normals,
    uint       texcoord_stride,
    float     *texcoords,
    allocator *alloc,
    vector    *ret_tangents);

void calc_vertex_tangents(
    uint       index_count,
    uint      *indices,
    uint       vertex_count,
    uint       vertex_stride,
    float     *vertices,
    bool       aligned_normals,
    uint       normal_stride,
    float     *normals,
    uint       texcoord_stride,
    float     *texcoords,
    allocator *alloc,
    vector    *ret_tangents);

#endif // include guard
