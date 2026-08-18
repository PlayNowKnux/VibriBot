#include "r5900_float.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

// VRS depends on the EE's odd float behavior.
typedef struct {
    int sign;
    uint32_t mant;
    int exp2;
    int zero;
} Frep;

uint32_t ee_f32_bits(float v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b;
}
float ee_bits_f32(uint32_t b) {
    float v;
    memcpy(&v, &b, 4);
    return v;
}

uint32_t ee_fpu_input(uint32_t b) {
    uint32_t e = b & 0x7f800000u;
    if (e == 0)
        return b & 0x80000000u;
    if (e == 0x7f800000u)
        return (b & 0x80000000u) | 0x7f7fffffu;
    return b;
}

static Frep decode(uint32_t b) {
    Frep r = {0, 0, 0, 1};
    b = ee_fpu_input(b);
    uint32_t mag = b & 0x7fffffffu;
    if (!mag) {
        r.sign = (int)(b >> 31);
        return r;
    }
    uint32_t E = (b >> 23) & 0xffu;
    r.sign = (int)(b >> 31);
    r.mant = 0x800000u | (b & 0x7fffffu);
    r.exp2 = (int)E - 127 - 23;
    r.zero = 0;
    return r;
}

static int bitlen_u64(uint64_t x) {
    if (!x)
        return 0;
    int n = 0;
    if (x >> 32) {
        x >>= 32;
        n += 32;
    }
    if (x >> 16) {
        x >>= 16;
        n += 16;
    }
    if (x >> 8) {
        x >>= 8;
        n += 8;
    }
    if (x >> 4) {
        x >>= 4;
        n += 4;
    }
    if (x >> 2) {
        x >>= 2;
        n += 2;
    }
    if (x >> 1) {
        n += 1;
    }
    return n + 1;
}

/* The VRS COP1 paths only feed normalized 24-bit mantissas into this helper.
   After exponent normalization, all intermediate integer magnitudes fit in
   64 bits. Keeping this implementation to uint64_t makes the exact EE model
   portable to MSVC (which has no C/C++ __int128). */
static uint32_t rtz_ratio(int sign, uint64_t num, uint64_t den, int exp2) {
    if (num == 0)
        return sign ? 0x80000000u : 0u;
    if (den == 0)
        return sign ? 0xff7fffffu : 0x7f7fffffu;
    int bn = bitlen_u64(num) - 1;
    int bd = bitlen_u64(den) - 1;
    int d = bn - bd;
    int e = d + exp2;
    int less;
    if (d >= 0) {
        if (d >= 64)
            less = 1;
        else if (den > (UINT64_MAX >> d))
            less = 1;
        else
            less = num < (den << d);
    } else {
        int sh = -d;
        if (sh >= 64)
            less = 0;
        else if (num > (UINT64_MAX >> sh))
            less = 0;
        else
            less = (num << sh) < den;
    }
    if (less)
        --e;
    if (e > 127)
        return sign ? 0xff7fffffu : 0x7f7fffffu;
    if (e < -126)
        return sign ? 0x80000000u : 0u;

    int sh = exp2 - e + 23;
    uint64_t m;
    if (sh >= 0) {
        /* For normalized float inputs, bitlen(num)+sh <= 25 here. */
        m = (num << sh) / den;
    } else {
        m = num / (den << (-sh));
    }
    if (m < 0x800000u) {
        --e;
        ++sh;
        if (e < -126)
            return sign ? 0x80000000u : 0u;
        m = sh >= 0 ? (num << sh) / den : num / (den << (-sh));
    }
    if (m >= 0x1000000u) {
        ++e;
        m >>= 1;
        if (e > 127)
            return sign ? 0xff7fffffu : 0x7f7fffffu;
    }
    uint32_t E = (uint32_t)(e + 127);
    return ((uint32_t)sign << 31) | (E << 23) | ((uint32_t)m & 0x7fffffu);
}

uint32_t ee_cvt_s_w(int32_t v) {
    float f = (float)v;
    return ee_f32_bits(f);
}

int32_t ee_cvt_w_s(uint32_t a) {
    a = ee_fpu_input(a);
    uint32_t mag = a & 0x7fffffffu;
    if (!mag)
        return 0;
    int sign = (int)(a >> 31);
    int E = (int)((a >> 23) & 0xffu);
    uint32_t mant = 0x800000u | (a & 0x7fffffu);
    int e = E - 127;
    if (e < 0)
        return 0;
    if (e > 31)
        return sign ? INT32_MIN : INT32_MAX;
    uint64_t x;
    if (e >= 23)
        x = (uint64_t)mant << (e - 23);
    else
        x = (uint64_t)mant >> (23 - e);
    if (sign) {
        if (x >= 0x80000000ull)
            return INT32_MIN;
        return -(int32_t)x;
    }
    if (x > 0x7fffffffull)
        return INT32_MAX;
    return (int32_t)x;
}

uint32_t ee_mul_s_chop(uint32_t a, uint32_t b) {
    Frep A = decode(a), B = decode(b);
    int sign = A.sign ^ B.sign;
    if (A.zero || B.zero)
        return sign ? 0x80000000u : 0u;
    return rtz_ratio(sign, (uint64_t)A.mant * (uint64_t)B.mant, 1, A.exp2 + B.exp2);
}

uint32_t ee_div_s_chop(uint32_t a, uint32_t b) {
    Frep A = decode(a), B = decode(b);
    int sign = A.sign ^ B.sign;
    if (B.zero)
        return sign ? 0xff7fffffu : 0x7f7fffffu;
    if (A.zero)
        return sign ? 0x80000000u : 0u;
    return rtz_ratio(sign, (uint64_t)A.mant, (uint64_t)B.mant, A.exp2 - B.exp2);
}

uint32_t ee_add_s_chop(uint32_t a, uint32_t b) {
    a = ee_fpu_input(a);
    b = ee_fpu_input(b);
    int ea = (int)((a >> 23) & 0xffu), eb = (int)((b >> 23) & 0xffu), diff = ea - eb;
    if (diff >= 25)
        b &= 0x80000000u;
    else if (diff > 0)
        b &= 0xffffffffu << (diff - 1);
    else if (diff <= -25)
        a &= 0x80000000u;
    else if (diff < 0)
        a &= 0xffffffffu << ((-diff) - 1);
    Frep A = decode(a), B = decode(b);
    if (A.zero && B.zero)
        return 0;
    if (A.zero)
        return b;
    if (B.zero)
        return a;
    int base = A.exp2 < B.exp2 ? A.exp2 : B.exp2;
    int da = A.exp2 - base, db = B.exp2 - base;
    int64_t na = (int64_t)((uint64_t)A.mant << da);
    int64_t nb = (int64_t)((uint64_t)B.mant << db);
    if (A.sign)
        na = -na;
    if (B.sign)
        nb = -nb;
    int64_t sum = na + nb;
    if (sum == 0)
        return 0;
    int sign = sum < 0;
    uint64_t mag = (uint64_t)(sign ? -sum : sum);
    return rtz_ratio(sign, mag, 1, base);
}

uint32_t ee_div_s_nearest(uint32_t a, uint32_t b) {
    a = ee_fpu_input(a);
    b = ee_fpu_input(b);
    float fa = ee_bits_f32(a), fb = ee_bits_f32(b);
    if ((b & 0x7fffffffu) == 0)
        return ((a ^ b) & 0x80000000u) | 0x7f7fffffu;
    double q = (double)fa / (double)fb;
    float r = (float)q;
    return ee_f32_bits(r);
}

uint32_t ee_sin_s_nearest(uint32_t a) {
    float x = ee_bits_f32(ee_fpu_input(a));
    return ee_f32_bits((float)sin((double)x));
}
uint32_t ee_cos_s_nearest(uint32_t a) {
    float x = ee_bits_f32(ee_fpu_input(a));
    return ee_f32_bits((float)cos((double)x));
}
