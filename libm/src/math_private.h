/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

/*
 * from: @(#)fdlibm.h 5.1 93/09/24
 * $NetBSD: math_private.h,v 1.16.8.1 2012/05/09 18:22:36 riz Exp $
 */

#ifndef _MATH_PRIVATE_H_
#define _MATH_PRIVATE_H_

#include <sys/types.h>

#include <endian.h>
#include <stdint.h>

/* The original fdlibm code used statements like:
	n0 = ((*(int*)&one)>>29)^1;		* index of high word *
	ix0 = *(n0+(int*)&x);			* high word of x *
	ix1 = *((1-n0)+(int*)&x);		* low word of x *
   to dig two 32 bit words out of the 64 bit IEEE floating point
   value.  That is non-ANSI, and, moreover, the gcc instruction
   scheduler gets it wrong.  We instead use the following macros.
   Unlike the original code, we determine the endianness at compile
   time, not at run time; I don't see much benefit to selecting
   endianness at run time.  */

/* A union which permits us to convert between a double and two 32 bit
   ints.  */

/*
 * The ARM ports are little endian except for the FPA word order which is
 * big endian.
 */

#if (BYTE_ORDER == BIG_ENDIAN) || (defined(__arm__) && !defined(__VFP_FP__))

typedef union
{
  double value;
  struct
  {
    uint32_t msw;
    uint32_t lsw;
  } parts;
} ieee_double_shape_type;

#endif

#if (BYTE_ORDER == LITTLE_ENDIAN) && \
    !(defined(__arm__) && !defined(__VFP_FP__))

typedef union
{
  double value;
  struct
  {
    uint32_t lsw;
    uint32_t msw;
  } parts;
} ieee_double_shape_type;

#endif

/* Get two 32 bit ints from a double.  */

#define EXTRACT_WORDS(ix0,ix1,d)				\
do {								\
  ieee_double_shape_type ew_u;					\
  ew_u.value = (d);						\
  (ix0) = ew_u.parts.msw;					\
  (ix1) = ew_u.parts.lsw;					\
} while (/*CONSTCOND*/0)

/* Get the more significant 32 bit int from a double.  */

#define GET_HIGH_WORD(i,d)					\
do {								\
  ieee_double_shape_type gh_u;					\
  gh_u.value = (d);						\
  (i) = gh_u.parts.msw;						\
} while (/*CONSTCOND*/0)

/* Get the less significant 32 bit int from a double.  */

#define GET_LOW_WORD(i,d)					\
do {								\
  ieee_double_shape_type gl_u;					\
  gl_u.value = (d);						\
  (i) = gl_u.parts.lsw;						\
} while (/*CONSTCOND*/0)

/* Set a double from two 32 bit ints.  */

#define INSERT_WORDS(d,ix0,ix1)					\
do {								\
  ieee_double_shape_type iw_u;					\
  iw_u.parts.msw = (ix0);					\
  iw_u.parts.lsw = (ix1);					\
  (d) = iw_u.value;						\
} while (/*CONSTCOND*/0)

/* Set the more significant 32 bits of a double from an int.  */

#define SET_HIGH_WORD(d,v)					\
do {								\
  ieee_double_shape_type sh_u;					\
  sh_u.value = (d);						\
  sh_u.parts.msw = (v);						\
  (d) = sh_u.value;						\
} while (/*CONSTCOND*/0)

/* Set the less significant 32 bits of a double from an int.  */

#define SET_LOW_WORD(d,v)					\
do {								\
  ieee_double_shape_type sl_u;					\
  sl_u.value = (d);						\
  sl_u.parts.lsw = (v);						\
  (d) = sl_u.value;						\
} while (/*CONSTCOND*/0)

/* A union which permits us to convert between a float and a 32 bit
   int.  */

typedef union
{
  float value;
  uint32_t word;
} ieee_float_shape_type;

/* Get a 32 bit int from a float.  */

#define GET_FLOAT_WORD(i,d)					\
do {								\
  ieee_float_shape_type gf_u;					\
  gf_u.value = (d);						\
  (i) = gf_u.word;						\
} while (/*CONSTCOND*/0)

/* Set a float from a 32 bit int.  */

#define SET_FLOAT_WORD(d,i)					\
do {								\
  ieee_float_shape_type sf_u;					\
  sf_u.word = (i);						\
  (d) = sf_u.value;						\
} while (/*CONSTCOND*/0)

/*
 * Attempt to get strict C99 semantics for assignment with non-C99 compilers.
 */
#if FLT_EVAL_METHOD == 0 || __GNUC__ == 0
#define	STRICT_ASSIGN(type, lval, rval)	((lval) = (rval))
#else
#define	STRICT_ASSIGN(type, lval, rval) do {	\
	volatile type __lval;			\
						\
	if (sizeof(type) >= sizeof(double))	\
		(lval) = (rval);		\
	else {					\
		__lval = (rval);		\
		(lval) = __lval;		\
	}					\
} while (/*CONSTCOND*/0)
#endif

#ifdef	_INCLUDE_COMPLEX_H

/*
 * Quoting from ISO/IEC 9899:TC2:
 *
 * 6.2.5.13 Types
 * Each complex type has the same representation and alignment requirements as
 * an array type containing exactly two elements of the corresponding real type;
 * the first element is equal to the real part, and the second element to the
 * imaginary part, of the complex number.
 */
typedef union {
	float complex z;
	float parts[2];
} float_complex;

typedef union {
	double complex z;
	double parts[2];
} double_complex;

typedef union {
	long double complex z;
	long double parts[2];
} long_double_complex;

#define	REAL_PART(z)	((z).parts[0])
#define	IMAG_PART(z)	((z).parts[1])

#endif	/* _INCLUDE_COMPLEX_H */

/* ieee style elementary functions */
extern double __ieee754_sqrt(double);
extern double __ieee754_acos(double);
extern double __ieee754_acosh(double);
extern double __ieee754_log(double);
extern double __ieee754_atanh(double);
extern double __ieee754_asin(double);
extern double __ieee754_atan2(double, double);
extern double __ieee754_exp(double);
extern double __ieee754_cosh(double);
extern double __ieee754_fmod(double, double);
extern double __ieee754_pow(double, double);
extern double __ieee754_lgamma_r(double,int *);
extern double __ieee754_gamma_r(double,int *);
extern double __ieee754_lgamma(double);
extern double __ieee754_gamma(double);
extern double __ieee754_log10(double);
extern double __ieee754_log2(double);
extern double __ieee754_sinh(double);
extern double __ieee754_hypot(double, double);
extern double __ieee754_j0(double);
extern double __ieee754_j1(double);
extern double __ieee754_y0(double);
extern double __ieee754_y1(double);
extern double __ieee754_jn(int,double);
extern double __ieee754_yn(int,double);
extern double __ieee754_remainder(double, double);
extern int    __ieee754_rem_pio2(double,double*);
extern double __ieee754_scalb(double, double);

/* fdlibm kernel function */
extern double __kernel_standard(double,double,int);
extern double __kernel_sin(double,double,int);
extern double __kernel_cos(double, double);
extern double __kernel_tan(double,double,int);
extern int    __kernel_rem_pio2(double*,double*,int,int,int,const int*);


/* ieee style elementary float functions */
extern float __ieee754_sqrtf(float);
extern float __ieee754_acosf(float);
extern float __ieee754_acoshf(float);
extern float __ieee754_logf(float);
extern float __ieee754_atanhf(float);
extern float __ieee754_asinf(float);
extern float __ieee754_atan2f(float,float);
extern float __ieee754_expf(float);
extern float __ieee754_coshf(float);
extern float __ieee754_fmodf(float,float);
extern float __ieee754_powf(float,float);
extern float __ieee754_lgammaf_r(float,int *);
extern float __ieee754_gammaf_r(float,int *);
extern float __ieee754_lgammaf(float);
extern float __ieee754_gammaf(float);
extern float __ieee754_log10f(float);
extern float __ieee754_log2f(float);
extern float __ieee754_sinhf(float);
extern float __ieee754_hypotf(float,float);
extern float __ieee754_j0f(float);
extern float __ieee754_j1f(float);
extern float __ieee754_y0f(float);
extern float __ieee754_y1f(float);
extern float __ieee754_jnf(int,float);
extern float __ieee754_ynf(int,float);
extern float __ieee754_remainderf(float,float);
extern int   __ieee754_rem_pio2f(float,float*);
extern float __ieee754_scalbf(float,float);

/* float versions of fdlibm kernel functions */
extern float __kernel_sinf(float,float,int);
extern float __kernel_cosf(float,float);
extern float __kernel_tanf(float,float,int);
extern int   __kernel_rem_pio2f(float*,float*,int,int,int,const int*);

/*
 * TRUNC() is a macro that sets the trailing 27 bits in the mantissa of an
 * IEEE double variable to zero.  It must be expression-like for syntactic
 * reasons, and we implement this expression using an inline function
 * instead of a pure macro to avoid depending on the gcc feature of
 * statement-expressions.
 */
#define	TRUNC(d)	(_b_trunc(&(d)))

static __inline void
_b_trunc(volatile double *_dp)
{
	uint32_t _lw;

	GET_LOW_WORD(_lw, *_dp);
	SET_LOW_WORD(*_dp, _lw & 0xf8000000);
}

struct Double {
	double	a;
	double	b;
};

/*
 * Functions internal to the math package, yet not static.
 */
double	__exp__D(double, double);
struct Double __log__D(double);

#endif /* _MATH_PRIVATE_H_ */
