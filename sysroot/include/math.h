#ifndef _MATH_H
#define _MATH_H
/* glibc 2.28 libm, the float functions q2-wings uses (each at one version only). sqrtf and fabsf
 * become sqrt.s and abs.s with -fno-math-errno. */
void sincosf(float, float *, float *); /* clang joins a sinf and cosf of one angle into this */
float sinf(float), cosf(float), atanf(float), atan2f(float, float), floorf(float), ceilf(float),
    fmodf(float, float), sqrtf(float), fabsf(float);
#define M_PI 3.14159265358979323846
#endif
