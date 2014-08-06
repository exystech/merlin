.include <sortix/sys.mk>

LIBRARY=libm

.ifdef CPU_IS_X86
  ARCH_SUBDIR=arch/i387
  ARCH_MACHINE_HEADERS=$(ARCH_SUBDIR)/machine/npx.h
.endif
.ifdef CPU_IS_X64
  ARCH_SUBDIR=arch/x86_64
  ARCH_MACHINE_HEADERS=$(ARCH_SUBDIR)/machine/fpu.h
.endif
ARCH_MACHINE_HEADERS:=$(ARCH_MACHINE_HEADERS) $(ARCH_SUBDIR)/machine/fenv.h

ARCH_OBJS=\
e_acos.o \
e_asin.o \
e_atan2.o \
e_expf.o \
e_exp.o \
e_fmod.o \
e_log10f.o \
e_log10.o \
e_log2f.o \
e_log2.o \
e_logf.o \
e_log.o \
e_remainderf.o \
e_remainder.o \
e_scalbf.o \
e_scalb.o \
e_sqrtf.o \
e_sqrt.o \
fabs.o \
fenv.o \
flt_rounds.o \
fpgetmask.o \
fpgetprec.o \
fpgetround.o \
fpgetsticky.o \
fpsetmask.o \
fpsetprec.o \
fpsetround.o \
fpsetsticky.o \
lrint.o \
s_atanf.o \
s_atan.o \
s_ceilf.o \
s_ceil.o \
s_copysignf.o \
s_copysign.o \
s_cosf.o \
s_cos.o \
s_finitef.o \
s_finite.o \
s_floorf.o \
s_floor.o \
s_ilogbf.o \
s_ilogbl.o \
s_ilogb.o \
s_log1pf.o \
s_log1p.o \
s_logbf.o \
s_logbl.o \
s_logb.o \
s_rintf.o \
s_rint.o \
s_scalbnf.o \
s_scalbn.o \
s_significandf.o \
s_significand.o \
s_sinf.o \
s_sin.o \
s_tanf.o \
s_tan.o \

COMMON_OBJS+=\
b_exp.o \
b_log.o \
b_tgamma.o \
compat_frexp_ieee754.o \
compat_ldexp_ieee754.o \
e_acos.o \
e_acosf.o \
e_acosh.o \
e_acoshf.o \
e_asin.o \
e_asinf.o \
e_atan2.o \
e_atan2f.o \
e_atanh.o \
e_atanhf.o \
e_cosh.o \
e_coshf.o \
e_exp.o \
e_expf.o \
e_fmod.o \
e_fmodf.o \
e_hypot.o \
e_hypotf.o \
e_j0.o \
e_j0f.o \
e_j1.o \
e_j1f.o \
e_jn.o \
e_jnf.o \
e_lgammaf_r.o \
e_lgamma_r.o \
e_log10.o \
e_log10f.o \
e_log2.o \
e_log2f.o \
e_log.o \
e_logf.o \
e_pow.o \
e_powf.o \
e_remainder.o \
e_remainderf.o \
e_rem_pio2.o \
e_rem_pio2f.o \
e_scalb.o \
e_scalbf.o \
e_sinh.o \
e_sinhf.o \
e_sqrt.o \
e_sqrtf.o \
fpclassifyd_ieee754.o \
fpclassifyf_ieee754.o \
fpclassifyl.o \
fpclassifyl_ieee754.o \
isfinited_ieee754.o \
isfinitef_ieee754.o \
isfinitel.o \
isfinitel_ieee754.o \
isinfd_ieee754.o \
isinff_ieee754.o \
isinfl.o \
isinfl_ieee754.o \
isnand_ieee754.o \
isnanf_ieee754.o \
isnanl.o \
isnanl_ieee754.o \
k_cos.o \
k_cosf.o \
k_rem_pio2.o \
k_rem_pio2f.o \
k_sin.o \
k_sinf.o \
k_standard.o \
k_tan.o \
k_tanf.o \
llrint.o \
llrintf.o \
llround.o \
llroundf.o \
lrint.o \
lrintf.o \
lround.o \
lroundf.o \
modf_ieee754.o \
nan.o \
nanf.o \
nanl.o \
s_asinh.o \
s_asinhf.o \
s_atan.o \
s_atanf.o \
s_cbrt.o \
s_cbrtf.o \
s_ceil.o \
s_ceilf.o \
s_copysign.o \
s_copysignf.o \
s_copysignl.o \
s_cos.o \
s_cosf.o \
s_erf.o \
s_erff.o \
s_exp2.o \
s_exp2f.o \
s_expm1.o \
s_expm1f.o \
s_fabsf.o \
s_fabsl.o \
s_fdim.o \
s_finite.o \
s_finitef.o \
s_floor.o \
s_floorf.o \
s_fmax.o \
s_fmaxf.o \
s_fmaxl.o \
s_fmin.o \
s_fminf.o \
s_fminl.o \
s_frexp.o \
s_frexpf.o \
signbitd_ieee754.o \
signbitf_ieee754.o \
signbitl.o \
s_ilogb.o \
s_ilogbf.o \
s_ilogbl.o \
s_isinff.o \
s_isnanf.o \
s_ldexp.o \
s_ldexpf.o \
s_lib_version.o \
s_log1p.o \
s_log1pf.o \
s_logb.o \
s_logbf.o \
s_logbl.o \
s_matherr.o \
s_modf.o \
s_modff.o \
s_nextafter.o \
s_nextafterf.o \
s_nextafterl.o \
s_nexttoward.o \
s_remquo.o \
s_remquof.o \
s_rint.o \
s_rintf.o \
s_round.o \
s_roundf.o \
s_scalbn.o \
s_scalbnf.o \
s_scalbnl.o \
s_signgam.o \
s_significand.o \
s_significandf.o \
s_sin.o \
s_sinf.o \
s_tan.o \
s_tanf.o \
s_tanh.o \
s_tanhf.o \
s_tgammaf.o \
s_trunc.o \
s_truncf.o \
w_acos.o \
w_acosf.o \
w_acosh.o \
w_acoshf.o \
w_asin.o \
w_asinf.o \
w_atan2.o \
w_atan2f.o \
w_atanh.o \
w_atanhf.o \
w_cosh.o \
w_coshf.o \
w_drem.o \
w_dremf.o \
w_exp.o \
w_expf.o \
w_fmod.o \
w_fmodf.o \
w_gamma.o \
w_gammaf.o \
w_gammaf_r.o \
w_gamma_r.o \
w_hypot.o \
w_hypotf.o \
w_j0.o \
w_j0f.o \
w_j1.o \
w_j1f.o \
w_jn.o \
w_jnf.o \
w_lgamma.o \
w_lgammaf.o \
w_lgammaf_r.o \
w_lgamma_r.o \
w_log10.o \
w_log10f.o \
w_log2.o \
w_log2f.o \
w_log.o \
w_logf.o \
w_pow.o \
w_powf.o \
w_remainder.o \
w_remainderf.o \
w_scalb.o \
w_scalbf.o \
w_sinh.o \
w_sinhf.o \
w_sqrt.o \
w_sqrtf.o \

COMPLEX_OBJS+=\
cabs.o \
cabsf.o \
cacos.o \
cacosf.o \
cacosh.o \
cacoshf.o \
carg.o \
cargf.o \
casin.o \
casinf.o \
casinh.o \
casinhf.o \
catan.o \
catanf.o \
catanh.o \
catanhf.o \
ccos.o \
ccosf.o \
ccosh.o \
ccoshf.o \
cephes_subr.o \
cephes_subrf.o \
cexp.o \
cexpf.o \
cimag.o \
cimagf.o \
cimagl.o \
clog.o \
clogf.o \
conj.o \
conjf.o \
conjl.o \
cpow.o \
cpowf.o \
cproj.o \
cprojf.o \
cprojl.o \
creal.o \
crealf.o \
creall.o \
csin.o \
csinf.o \
csinh.o \
csinhf.o \
csqrt.o \
csqrtf.o \
ctan.o \
ctanf.o \
ctanh.o \
ctanhf.o \

LIBRARY_OBJS=
.for obj in $(ARCH_OBJS)
LIBRARY_OBJS+:=$(ARCH_SUBDIR)/$(obj)
.endfor
.for obj in $(COMMON_OBJS)
LIBRARY_OBJS+:=src/$(obj)
.endfor
.for obj in $(COMPLEX_OBJS)
LIBRARY_OBJS+:=complex/$(obj)
.endfor

# TODO: Do not pick up the i387 asm version, it is incorrect
LIBRARY_OBJS:=$(LIBRARY_OBJS:arch/i387/s_modf.o=src/s_modf.o)

CPPFLAGS+=-Isrc -I$(ARCH_SUBDIR)
CPPFLAGS+=-D__is_sortix_libm -D_MULTI_LIBM -D_POSIX_MODE

.include <template/dependencies.mk>
.include <template/headers.mk>
.include <template/library.mk>

.PHONY: install-headers install-arch-headers

install-headers: install-arch-headers

install-arch-headers:
	mkdir -p $(DESTDIR)$(INCLUDEDIR)/machine
.for header in $(ARCH_MACHINE_HEADERS)
	cp --preserve=timestamps $(header) $(DESTDIR)$(INCLUDEDIR)/machine
.endfor
