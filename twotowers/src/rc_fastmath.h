/* (1.9.5) sin, cos and tan for the recompiled game code: the C library's
 * (the x87 instruction behind a long-double wrapper) were a visible part of
 * the game thread's time in a busy scene (every bone of every character).
 * fdlibm's kernels after a three-part reduction by pi/2; arguments beyond
 * about a million radians, infinities and NaNs go to the library. */
#ifndef RC_FASTMATH_H
#define RC_FASTMATH_H
#include <math.h>
#include <stdint.h>
static inline double rc_ksin(double x, double y)
{
    const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
                 S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    double z = x * x, v = z * x, r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}
static inline double rc_kcos(double x, double y)
{
    const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
                 C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    double z = x * x, r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6))))), hz = 0.5 * z, w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (z * r - x * y));
}
/* returns the quadrant; *y0 + *y1 = x - n*pi/2 */
static inline int rc_rem_pio2(double x, double *y0, double *y1)
{
    const double invpio2 = 6.36619772367581382433e-01,
                 p1 = 1.57079632673412561417e+00, p1t = 6.07710050650619224932e-11,
                 p2 = 6.07710050630396597660e-11, p2t = 2.02226624879595063154e-21,
                 p3 = 2.02226624871116645580e-21, p3t = 8.47842766036889956997e-32;
    double fn = (x * invpio2 + 6755399441055744.0) - 6755399441055744.0, r, w, t;   /* (round to nearest without a library call) */
    r = x - fn * p1; w = fn * p1t;
    t = r; w = fn * p2; r = t - w; w = fn * p2t - ((t - r) - w);
    t = r; w = fn * p3; r = t - w; w = fn * p3t - ((t - r) - w);
    *y0 = r - w; *y1 = (r - *y0) - w;
    return (int)(long long)fn;
}
static inline double rc_sin(double x)
{
    double a = fabs(x), y0, y1;
    if (!(a < 1.0e6)) return sin(x);
    if (a < 0.7853981633974483) return rc_ksin(x, 0.0);
    switch (rc_rem_pio2(x, &y0, &y1) & 3) {
    case 0: return rc_ksin(y0, y1);
    case 1: return rc_kcos(y0, y1);
    case 2: return -rc_ksin(y0, y1);
    default: return -rc_kcos(y0, y1);
    }
}
static inline double rc_cos(double x)
{
    double a = fabs(x), y0, y1;
    if (!(a < 1.0e6)) return cos(x);
    if (a < 0.7853981633974483) return rc_kcos(x, 0.0);
    switch (rc_rem_pio2(x, &y0, &y1) & 3) {
    case 0: return rc_kcos(y0, y1);
    case 1: return -rc_ksin(y0, y1);
    case 2: return -rc_kcos(y0, y1);
    default: return rc_ksin(y0, y1);
    }
}
static inline double rc_tan(double x)
{
    double a = fabs(x);
    if (!(a < 1.0e6)) return tan(x);
    return rc_sin(x) / rc_cos(x);
}
/* fscale's ldexp: a multiply by the power of two, when the result's
 * exponent stays normal (the library call otherwise) */
static inline double rc_ldexp(double x, int n)
{
    if (n > -1022 && n < 1023) {
        union { uint64_t u; double d; } p;
        p.u = (uint64_t)(n + 1023) << 52;
        {
            double r = x * p.d;
            if (fabs(r) >= 2.2250738585072014e-308 || r == 0.0) return r;
        }
    }
    return ldexp(x, n);
}
static inline float rc_sinf(float x) { return (float)rc_sin(x); }
static inline float rc_cosf(float x) { return (float)rc_cos(x); }
/* (2.1, online play) the rest of the game's math our own as well: atan2, log2 and exp2 (the x87's fpatan, fyl2x and f2xm1)
 * and the float versions the hand-written code uses. Online, every machine must compute the very same game -- Windows'
 * and Linux's C libraries round these differently in the last bit. Plain double arithmetic only (no FMA: x86-64's baseline);
 * the special values (infinities, NaN, 0) go to the library, where the C standard fixes the results. */
static inline double rc_atan(double x)
{
    double a = fabs(x), off = 0.0, z, r; int inv = 0;
    if (!(a < 1.0e300)) return atan(x);   /* (infinity, NaN) */
    if (a > 1.0) { a = 1.0 / a; inv = 1; }
    if (a > 0.2679491924311227) { off = 0.5235987755982988; a = (a * 1.7320508075688772 - 1.0) / (a + 1.7320508075688772); }
    z = a * a;
    { int k; r = 1.0 / 29; for (k = 27; k >= 1; k -= 2) r = ((k & 2) ? -1.0 : 1.0) / k + z * r; r *= a; }   /* (a - a^3/3 + a^5/5 ...: |a| < 0.268) */
    r += off;
    if (inv) r = 1.5707963267948966 - r;
    return x < 0.0 ? -r : r;
}
static inline double rc_atan2(double y, double x)
{
    double r;
    if (!(fabs(x) < 1.0e300) || !(fabs(y) < 1.0e300) || (x == 0.0 && y == 0.0)) return atan2(y, x);   /* (the standard's cases) */
    if (x == 0.0) return y > 0.0 ? 1.5707963267948966 : -1.5707963267948966;
    if (fabs(y) > fabs(x)) { r = 1.5707963267948966 - rc_atan(fabs(x / y)); r = x > 0.0 ? r : 3.141592653589793 - r; }
    else { r = rc_atan(fabs(y / x)); if (x < 0.0) r = 3.141592653589793 - r; }
    return signbit(y) ? -r : r;
}
static inline double rc_log2(double x)
{
    double m, s, z, l; int e;
    if (!(x > 0.0) || !(x < 1.0e308)) return log2(x);
    m = frexp(x, &e);
    if (m < 0.7071067811865476) { m *= 2.0; e--; }
    s = (m - 1.0) / (m + 1.0); z = s * s;
    { int k; l = 1.0 / 25; for (k = 23; k >= 1; k -= 2) l = 1.0 / k + z * l; l *= 2.0 * s; }   /* (2(s + s^3/3 + s^5/5 ...): ln m) */
    return (double)e + l * 1.4426950408889634;
}
static inline double rc_exp2(double x)
{
    double n, t, p;
    if (!(fabs(x) < 1000.0)) return exp2(x);
    n = (x + 6755399441055744.0) - 6755399441055744.0;
    t = (x - n) * 0.6931471805599453;
    { int k; double c[16]; c[0] = 1.0; for (k = 1; k < 16; k++) c[k] = c[k - 1] / k; p = c[15]; for (k = 14; k >= 0; k--) p = c[k] + t * p; }   /* (e^t, |t| < 0.35) */
    return ldexp(p, (int)n);
}
static inline double rc_exp(double x) { return (fabs(x) < 700.0) ? rc_exp2(x * 1.4426950408889634) : exp(x); }
static inline double rc_log(double x) { return (x > 0.0 && x < 1.0e308) ? rc_log2(x) * 0.6931471805599453 : log(x); }
static inline double rc_pow(double x, double y)
{
    if (!(fabs(x) < 1.0e300) || !(fabs(y) < 1.0e300) || x == 0.0 || y == 0.0) return pow(x, y);
    if (x > 0.0) return rc_exp2(y * rc_log2(x));
    if (y == floor(y)) { double r = rc_exp2(y * rc_log2(-x)); return fmod(y, 2.0) != 0.0 ? -r : r; }
    return pow(x, y);   /* (NaN) */
}
static inline double rc_asin(double x) { return (fabs(x) <= 1.0) ? rc_atan2(x, sqrt((1.0 - x) * (1.0 + x))) : asin(x); }
static inline double rc_acos(double x) { return (fabs(x) <= 1.0) ? rc_atan2(sqrt((1.0 - x) * (1.0 + x)), x) : acos(x); }
#define atan2(y, x) rc_atan2((y), (x))
#define atan(x) rc_atan(x)
#define log2(x) rc_log2(x)
#define exp2(x) rc_exp2(x)
#define exp(x) rc_exp(x)
#define log(x) rc_log(x)
#define pow(x, y) rc_pow((x), (y))
#define asin(x) rc_asin(x)
#define acos(x) rc_acos(x)
#define tanf(x) ((float)rc_tan(x))
#define atan2f(y, x) ((float)rc_atan2((y), (x)))
#define atanf(x) ((float)rc_atan(x))
#define asinf(x) ((float)rc_asin(x))
#define acosf(x) ((float)rc_acos(x))
#define expf(x) ((float)rc_exp(x))
#define logf(x) ((float)rc_log(x))
#define powf(x, y) ((float)rc_pow((x), (y)))
#define log2f(x) ((float)rc_log2(x))
#define exp2f(x) ((float)rc_exp2(x))
#define ldexp(x, n) rc_ldexp((x), (n))
#define sinf(x) rc_sinf(x)
#define cosf(x) rc_cosf(x)
#define sin(x) rc_sin(x)
#define cos(x) rc_cos(x)
#define tan(x) rc_tan(x)
#endif
