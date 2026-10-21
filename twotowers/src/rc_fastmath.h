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
#define ldexp(x, n) rc_ldexp((x), (n))
#define sinf(x) rc_sinf(x)
#define cosf(x) rc_cosf(x)
#define sin(x) rc_sin(x)
#define cos(x) rc_cos(x)
#define tan(x) rc_tan(x)
#endif
