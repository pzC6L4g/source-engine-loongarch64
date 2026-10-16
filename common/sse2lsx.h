/*
 * sse2lsx.h - SSE (x86 SIMD) -> LoongArch LSX/ASX translation layer.
 *
 * This header is the LoongArch counterpart of common/sse2neon.h (ARM NEON).
 * It lets the existing x86-optimized SSE math code (mathlib/sse.cpp,
 * sseconst.cpp, powsse.cpp, randsse.cpp, ssenoise.cpp, simdvectormatrix.cpp,
 * vector SIMD helpers, etc.) compile and run with near-native performance on
 * LoongArch (LA64) by mapping Intel SSE intrinsics onto LoongArch LSX
 * intrinsics (GCC/Clang lsxintrin.h).
 *
 * Target toolchain requirements:
 *   -mlsx   (128-bit LSX, mandatory)
 *   -mlasx  (256-bit LASX, optional; enables future widening optimizations)
 *
 * Design notes:
 *   * lsxintrin.h already provides the exact SSE vector types __m128, __m128i,
 *     __m128d, so we do NOT re-typedef them (unlike sse2neon.h, which must
 *     create float32x4_t -> __m128 aliases on ARM).
 *   * Functions marked "PORTABLE" use a store-to-local-buffer + scalar path so
 *     they are correct on any toolchain; functions using a single LSX builtin
 *     are flagged "LSX" and are the performance-critical ones.
 *   * For maximum portability the header does not depend on the old 3DNow!/MMX
 *     paths; those are excluded on LoongArch (see mathlib/3dnow.cpp).
 *
 * Landing order (see LoongArch_SSE_translation_framework.md):
 *   1) scalar-correct fallback (this file)  -> compiles, runs correctly
 *   2) minimal LSX subset used by mathlib   -> near-native perf for math
 *   3) full intrinsic coverage + LASX widening -> parity with x86
 */

#ifndef SSE2LSX_H
#define SSE2LSX_H

#if !defined(__loongarch__)
#error "sse2lsx.h should only be included on LoongArch targets."
#endif

#if !defined(__loongarch_sx)
#error "sse2lsx.h requires the LSX SIMD ISA. Build with -mlsx (add -mlasx for LASX widening)."
#endif

#include <lsxintrin.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Portability macros                                                 */
/* ------------------------------------------------------------------ */
#ifndef SSE2LSX_ALIGN
#define SSE2LSX_ALIGN(x)      __attribute__((aligned(x)))
#endif
#ifndef FORCE_INLINE
#define FORCE_INLINE          static inline __attribute__((always_inline))
#endif
#ifndef SSE2LSX_INLINE
#define SSE2LSX_INLINE        static inline
#endif

#ifndef _MM_SHUFFLE
#define _MM_SHUFFLE(fp3, fp2, fp1, fp0) \
    (((fp3) << 6) | ((fp2) << 4) | ((fp1) << 2) | (fp0))
#endif

/* Small helpers to move a full 128-bit register through a 16-byte stack slot.
 * Used by the PORTABLE implementations below. LSX vector loads/stores are
 * unaligned-capable, but we align the temp buffers defensively. */
typedef SSE2LSX_ALIGN(16) float  _lsx_f32x4[4];
typedef SSE2LSX_ALIGN(16) int32_t _lsx_i32x4[4];

FORCE_INLINE void   _lsx_store_f(__m128 a, _lsx_f32x4 o) { __lsx_vst((__m128i)a, (void *)o, 0); }
FORCE_INLINE __m128 _lsx_load_f(const _lsx_f32x4 i)     { return (__m128)__lsx_vld((void *)i, 0); }
FORCE_INLINE float  _lsx_lane0_f(__m128 a) { _lsx_f32x4 t; _lsx_store_f(a, t); return t[0]; }
FORCE_INLINE int    _lsx_lane0_i(__m128i a) { _lsx_i32x4 t; __lsx_vst(a, (void *)t, 0); return t[0]; }

/* ================================================================== */
/* 1. Constants / zero / splat                                        */
/* ================================================================== */

/* LSX: replicate the GPR 0 into all 4 words, then bit-cast to float. */
FORCE_INLINE __m128 _mm_setzero_ps(void) {
    return (__m128)__lsx_vreplgr2vr_w(0);
}

/* Portable broadcast (avoids any toolchain-specific vldrepl naming). */
FORCE_INLINE __m128 _mm_set1_ps(float v) {
    _lsx_f32x4 t = { v, v, v, v };
    return _lsx_load_f(t);
}

/* _mm_set_ps(a,b,c,d): result = { d, c, b, a } */
FORCE_INLINE __m128 _mm_set_ps(float a, float b, float c, float d) {
    _lsx_f32x4 t = { d, c, b, a };
    return _lsx_load_f(t);
}

/* _mm_setr_ps(a,b,c,d): result = { a, b, c, d } */
FORCE_INLINE __m128 _mm_setr_ps(float a, float b, float c, float d) {
    _lsx_f32x4 t = { a, b, c, d };
    return _lsx_load_f(t);
}

/* _mm_set_ss(a): lane0 = a, lanes 1..3 = 0 */
FORCE_INLINE __m128 _mm_set_ss(float a) {
    _lsx_i32x4 t = { *(const int32_t *)&a, 0, 0, 0 };
    return (__m128)__lsx_vld((void *)t, 0);
}

FORCE_INLINE __m128i _mm_setzero_si128(void) {
    return __lsx_vreplgr2vr_w(0);
}

FORCE_INLINE __m128i _mm_set1_epi32(int v) {
    return __lsx_vreplgr2vr_w(v);
}

/* _mm_set_epi32(a,b,c,d): result = { d, c, b, a } (int) */
FORCE_INLINE __m128i _mm_set_epi32(int a, int b, int c, int d) {
    _lsx_i32x4 t = { d, c, b, a };
    return __lsx_vld((void *)t, 0);
}

/* ================================================================== */
/* 2. Load / store                                                    */
/* ================================================================== */

/* LSX vector loads/stores tolerate unaligned addresses. */
FORCE_INLINE __m128  _mm_load_ps(const float *p)   { return (__m128)__lsx_vld((void *)p, 0); }
FORCE_INLINE __m128  _mm_loadu_ps(const float *p)  { return (__m128)__lsx_vld((void *)p, 0); }
FORCE_INLINE __m128i _mm_load_si128(const __m128i *p)  { return __lsx_vld((void *)p, 0); }
FORCE_INLINE __m128i _mm_loadu_si128(const __m128i *p) { return __lsx_vld((void *)p, 0); }

FORCE_INLINE void _mm_store_ps(float *p, __m128 a)   { __lsx_vst((__m128i)a, (void *)p, 0); }
FORCE_INLINE void _mm_storeu_ps(float *p, __m128 a)  { __lsx_vst((__m128i)a, (void *)p, 0); }
FORCE_INLINE void _mm_store_si128(__m128i *p, __m128i a) { __lsx_vst(a, (void *)p, 0); }
FORCE_INLINE void _mm_storeu_si128(__m128i *p, __m128i a){ __lsx_vst(a, (void *)p, 0); }

/* _mm_load_ss: lane0 = *p, lanes 1..3 = 0 */
FORCE_INLINE __m128 _mm_load_ss(const float *p) {
    return (__m128)__lsx_vinsgr2vr_w(_mm_setzero_si128(), *(const int32_t *)p, 0);
}

/* _mm_store_ss: write lane0 only */
FORCE_INLINE void _mm_store_ss(float *p, __m128 a) {
    int32_t v = _lsx_lane0_i((__m128i)a);
    memcpy(p, &v, sizeof(float));
}

/* ================================================================== */
/* 3. Arithmetic (LSX)                                                */
/* ================================================================== */

FORCE_INLINE __m128 _mm_add_ps(__m128 a, __m128 b)  { return (__m128)__lsx_vfadd_s(a, b); }
FORCE_INLINE __m128 _mm_sub_ps(__m128 a, __m128 b)  { return (__m128)__lsx_vfsub_s(a, b); }
FORCE_INLINE __m128 _mm_mul_ps(__m128 a, __m128 b)  { return (__m128)__lsx_vfmul_s(a, b); }
FORCE_INLINE __m128 _mm_div_ps(__m128 a, __m128 b)  { return (__m128)__lsx_vfdiv_s(a, b); }

FORCE_INLINE __m128 _mm_add_ss(__m128 a, __m128 b) {
    _lsx_f32x4 A, B, R; _lsx_store_f(a, A); _lsx_store_f(b, B);
    R[0] = A[0] + B[0]; R[1] = A[1]; R[2] = A[2]; R[3] = A[3];
    return _lsx_load_f(R);
}
FORCE_INLINE __m128 _mm_sub_ss(__m128 a, __m128 b) {
    _lsx_f32x4 A, B, R; _lsx_store_f(a, A); _lsx_store_f(b, B);
    R[0] = A[0] - B[0]; R[1] = A[1]; R[2] = A[2]; R[3] = A[3];
    return _lsx_load_f(R);
}
FORCE_INLINE __m128 _mm_mul_ss(__m128 a, __m128 b) {
    _lsx_f32x4 A, B, R; _lsx_store_f(a, A); _lsx_store_f(b, B);
    R[0] = A[0] * B[0]; R[1] = A[1]; R[2] = A[2]; R[3] = A[3];
    return _lsx_load_f(R);
}

FORCE_INLINE __m128 _mm_sqrt_ps(__m128 a) { return (__m128)__lsx_vfsqrt_s(a); }
FORCE_INLINE __m128 _mm_sqrt_ss(__m128 a) { return (__m128)__lsx_vfsqrt_s(a); }

/* Reciprocal / reciprocal-sqrt estimates (1 Newton iteration in caller). */
FORCE_INLINE __m128 _mm_rcp_ps(__m128 a)   { return (__m128)__lsx_vfrecip_s(a); }
FORCE_INLINE __m128 _mm_rcp_ss(__m128 a)   { return (__m128)__lsx_vfrecip_s(a); }
FORCE_INLINE __m128 _mm_rsqrt_ps(__m128 a){ return (__m128)__lsx_vfrsqrt_s(a); }
FORCE_INLINE __m128 _mm_rsqrt_ss(__m128 a){ return (__m128)__lsx_vfrsqrt_s(a); }

FORCE_INLINE __m128 _mm_max_ps(__m128 a, __m128 b) { return (__m128)__lsx_vfmax_s(a, b); }
FORCE_INLINE __m128 _mm_min_ps(__m128 a, __m128 b) { return (__m128)__lsx_vfmin_s(a, b); }

/* ================================================================== */
/* 4. Bitwise (LSX vector logical ops)                               */
/* ================================================================== */

FORCE_INLINE __m128 _mm_and_ps(__m128 a, __m128 b)     { return (__m128)__lsx_vand_v((__m128i)a, (__m128i)b); }
FORCE_INLINE __m128 _mm_andnot_ps(__m128 a, __m128 b)  { return (__m128)__lsx_vandn_v((__m128i)a, (__m128i)b); }
FORCE_INLINE __m128 _mm_or_ps(__m128 a, __m128 b)      { return (__m128)__lsx_vor_v((__m128i)a, (__m128i)b); }
FORCE_INLINE __m128 _mm_xor_ps(__m128 a, __m128 b)     { return (__m128)__lsx_vxor_v((__m128i)a, (__m128i)b); }

FORCE_INLINE __m128i _mm_and_si128(__m128i a, __m128i b)   { return __lsx_vand_v(a, b); }
FORCE_INLINE __m128i _mm_andnot_si128(__m128i a, __m128i b){ return __lsx_vandn_v(a, b); }
FORCE_INLINE __m128i _mm_or_si128(__m128i a, __m128i b)    { return __lsx_vor_v(a, b); }
FORCE_INLINE __m128i _mm_xor_si128(__m128i a, __m128i b)   { return __lsx_vxor_v(a, b); }

/* ================================================================== */
/* 5. Integer arithmetic (LSX)                                       */
/* ================================================================== */

FORCE_INLINE __m128i _mm_add_epi32(__m128i a, __m128i b) { return __lsx_vadd_w(a, b); }
FORCE_INLINE __m128i _mm_sub_epi32(__m128i a, __m128i b) { return __lsx_vsub_w(a, b); }
FORCE_INLINE __m128i _mm_mullo_epi32(__m128i a, __m128i b){ return __lsx_vmul_w(a, b); }

/* ================================================================== */
/* 6. Comparisons -> mask (LSX floating compare -> all-ones/zeros)   */
/* ================================================================== */

FORCE_INLINE __m128 _mm_cmplt_ps(__m128 a, __m128 b) { return (__m128)__lsx_vfcmp_clt_s(a, b); }
FORCE_INLINE __m128 _mm_cmple_ps(__m128 a, __m128 b) { return (__m128)__lsx_vfcmp_cle_s(a, b); }
FORCE_INLINE __m128 _mm_cmpeq_ps(__m128 a, __m128 b) { return (__m128)__lsx_vfcmp_ceq_s(a, b); }
FORCE_INLINE __m128 _mm_cmpneq_ps(__m128 a, __m128 b){ return (__m128)__lsx_vfcmp_cne_s(a, b); }
/* SSE gt/ge: a>b <=> b<a */
FORCE_INLINE __m128 _mm_cmpgt_ps(__m128 a, __m128 b) { return (__m128)__lsx_vfcmp_clt_s(b, a); }
FORCE_INLINE __m128 _mm_cmpge_ps(__m128 a, __m128 b) { return (__m128)__lsx_vfcmp_cle_s(b, a); }

/* Scalar comparisons -> int flag (used by mathlib.h Floor2Int/Ceil2Int). */
FORCE_INLINE int _mm_comigt_ss(__m128 a, __m128 b) { return _lsx_lane0_f(a) >  _lsx_lane0_f(b) ? 1 : 0; }
FORCE_INLINE int _mm_comilt_ss(__m128 a, __m128 b) { return _lsx_lane0_f(a) <  _lsx_lane0_f(b) ? 1 : 0; }
FORCE_INLINE int _mm_comieq_ss(__m128 a, __m128 b) { return _lsx_lane0_f(a) == _lsx_lane0_f(b) ? 1 : 0; }
FORCE_INLINE int _mm_comige_ss(__m128 a, __m128 b) { return _lsx_lane0_f(a) >= _lsx_lane0_f(b) ? 1 : 0; }
FORCE_INLINE int _mm_comile_ss(__m128 a, __m128 b) { return _lsx_lane0_f(a) <= _lsx_lane0_f(b) ? 1 : 0; }
FORCE_INLINE int _mm_comine_ss(__m128 a, __m128 b) { return _lsx_lane0_f(a) != _lsx_lane0_f(b) ? 1 : 0; }
/* ucomi* are unordered variants; for framework correctness treat identically. */
FORCE_INLINE int _mm_ucomigt_ss(__m128 a, __m128 b){ return _mm_comigt_ss(a, b); }
FORCE_INLINE int _mm_ucomilt_ss(__m128 a, __m128 b){ return _mm_comilt_ss(a, b); }
FORCE_INLINE int _mm_ucomige_ss(__m128 a, __m128 b){ return _mm_comige_ss(a, b); }
FORCE_INLINE int _mm_ucomile_ss(__m128 a, __m128 b){ return _mm_comile_ss(a, b); }
FORCE_INLINE int _mm_ucomieq_ss(__m128 a, __m128 b){ return _mm_comieq_ss(a, b); }
FORCE_INLINE int _mm_ucomine_ss(__m128 a, __m128 b){ return _mm_comine_ss(a, b); }

/* ================================================================== */
/* 7. Shuffle / pack / unpack (PORTABLE; optimize with vshuf.w later)*/
/* ================================================================== */

/* _mm_shuffle_ps(a,b,imm):
 *   res[0]=a[imm&3] res[1]=a[(imm>>2)&3] res[2]=b[(imm>>4)&3] res[3]=b[(imm>>6)&3] */
FORCE_INLINE __m128 _mm_shuffle_ps(__m128 a, __m128 b, int imm) {
    _lsx_f32x4 A, B, R;
    _lsx_store_f(a, A); _lsx_store_f(b, B);
    R[0] = A[(imm >> 0) & 3];
    R[1] = A[(imm >> 2) & 3];
    R[2] = B[(imm >> 4) & 3];
    R[3] = B[(imm >> 6) & 3];
    return _lsx_load_f(R);
}

FORCE_INLINE __m128 _mm_unpacklo_ps(__m128 a, __m128 b) {
    _lsx_f32x4 A, B, R;
    _lsx_store_f(a, A); _lsx_store_f(b, B);
    R[0] = A[0]; R[1] = B[0]; R[2] = A[1]; R[3] = B[1];
    return _lsx_load_f(R);
}
FORCE_INLINE __m128 _mm_unpackhi_ps(__m128 a, __m128 b) {
    _lsx_f32x4 A, B, R;
    _lsx_store_f(a, A); _lsx_store_f(b, B);
    R[0] = A[2]; R[1] = B[2]; R[2] = A[3]; R[3] = B[3];
    return _lsx_load_f(R);
}
FORCE_INLINE __m128 _mm_movehl_ps(__m128 a, __m128 b) {
    _lsx_f32x4 A, B, R;
    _lsx_store_f(a, A); _lsx_store_f(b, B);
    // SSE: r0=b2, r1=b3, r2=a2, r3=a3
    R[0] = B[2]; R[1] = B[3]; R[2] = A[2]; R[3] = A[3];
    return _lsx_load_f(R);
}
FORCE_INLINE __m128 _mm_movelh_ps(__m128 a, __m128 b) {
    _lsx_f32x4 A, B, R;
    _lsx_store_f(a, A); _lsx_store_f(b, B);
    R[0] = A[0]; R[1] = A[1]; R[2] = B[0]; R[3] = B[1];
    return _lsx_load_f(R);
}

/* ================================================================== */
/* 8. Movemask (LSX vmskltz.w returns the exact 4-bit sign mask)      */
/* ================================================================== */

FORCE_INLINE int _mm_movemask_ps(__m128 a) {
    // SSE movemask_ps collects the *sign bit* (bit 31) of each 32-bit lane.
    // This MUST be a raw bit test, not a floating-point comparison: the masks
    // produced by the _mm_cmp*_ps intrinsics are all-ones (0xFFFFFFFF), which
    // as a float is a NaN -- NaN < 0.0f is false, so a value-based test would
    // report 0 for comparison masks and silently break TestSignSIMD()/IsAnyNegative()/
    // IsAllZeros() in mathlib, and hence IsBoxIntersectingRay() and friends.
    // vmskltz.w extracts the sign bit of each word into the low 4 bits, which
    // matches SSE semantics exactly (this is also what sse2neon.h does).
    return (int)__lsx_vpickve2gr_wu((__m128i)__lsx_vmskltz_w((__m128i)a), 0);
}

/* ================================================================== */
/* 9. Conversion                                                      */
/* ================================================================== */

/* Extract lane 0 as a float. */
FORCE_INLINE float _mm_cvtss_f32(__m128 a) { return _lsx_lane0_f(a); }

/* float -> int (round to nearest, current rounding mode) */
FORCE_INLINE int _mm_cvtss_si32(__m128 a) {
    _lsx_i32x4 t; __lsx_vst(__lsx_vftint_w_s(a), (void *)t, 0); return t[0];
}
FORCE_INLINE int _mm_cvt_ss2si(__m128 a)   { return _mm_cvtss_si32(a); }
/* float -> int (truncate) */
FORCE_INLINE int _mm_cvtt_ss2si(__m128 a) {
    _lsx_i32x4 t; __lsx_vst(__lsx_vftintrz_w_s(a), (void *)t, 0); return t[0];
}
FORCE_INLINE int _mm_cvttss_si32(__m128 a) { return _mm_cvtt_ss2si(a); }

/* Packed float<->int */
FORCE_INLINE __m128i _mm_cvtps_epi32(__m128 a)  { return __lsx_vftint_w_s(a); }
FORCE_INLINE __m128i _mm_cvttps_epi32(__m128 a) { return __lsx_vftintrz_w_s(a); }
FORCE_INLINE __m128  _mm_cvtepi32_ps(__m128i a) { return (__m128)__lsx_vffint_s_w(a); }

/* _mm_cvt_si2ss(a, i): result[0] = (float)i, rest copied from a */
FORCE_INLINE __m128 _mm_cvt_si2ss(__m128 a, int i) {
    _lsx_f32x4 A, R; _lsx_store_f(a, A);
    R[0] = (float)i; R[1] = A[1]; R[2] = A[2]; R[3] = A[3];
    return _lsx_load_f(R);
}

/* ================================================================== */
/* 10. Reinterpret casts (no-op bit casts)                           */
/* ================================================================== */

#define _mm_castps_si128(a)  ((__m128i)(a))
#define _mm_castsi128_ps(a)  ((__m128)(a))
#define _mm_castps_pd(a)     ((__m128d)(a))
#define _mm_castpd_ps(a)     ((__m128)(a))
#define _mm_castpd_si128(a)  ((__m128i)(a))
#define _mm_castsi128_pd(a)  ((__m128d)(a))

/* ================================================================== */
/* 11. Misc                                                           */
/* ================================================================== */

/* Non-temporal hint: on LSX just store normally. */
FORCE_INLINE void _mm_stream_ps(float *p, __m128 a) { __lsx_vst((__m128i)a, (void *)p, 0); }

/* Prefetch: LSX has prefetch; map to a no-op builtin for now. */
FORCE_INLINE void _mm_prefetch(const char *p, int hint) {
    (void)p; (void)hint;
    /* __builtin_prefetch(p, 0, 3);  // enable once validated */
}

/* ================================================================== */
/* 12. Transpose macro (SSE _MM_TRANSPOSE4_PS)                        */
/* ================================================================== */

#ifndef _MM_TRANSPOSE4_PS
#define _MM_TRANSPOSE4_PS(r0, r1, r2, r3) do {                            \
        float _t0[4], _t1[4], _t2[4], _t3[4];                             \
        _lsx_store_f(r0, _t0); _lsx_store_f(r1, _t1);                     \
        _lsx_store_f(r2, _t2); _lsx_store_f(r3, _t3);                     \
        float _o0[4] = { _t0[0], _t1[0], _t2[0], _t3[0] };                \
        float _o1[4] = { _t0[1], _t1[1], _t2[1], _t3[1] };                \
        float _o2[4] = { _t0[2], _t1[2], _t2[2], _t3[2] };                \
        float _o3[4] = { _t0[3], _t1[3], _t2[3], _t3[3] };                \
        r0 = _lsx_load_f(_o0); r1 = _lsx_load_f(_o1);                     \
        r2 = _lsx_load_f(_o2); r3 = _lsx_load_f(_o3);                     \
    } while (0)
#endif

/* ================================================================== */
/* 13. MMX fallback (no native MMX on LoongArch)                      */
/*                                                                     */
/* The Cephes transcendental routines in sse.cpp use the legacy 64-bit */
/* MMX intrinsics, which operate on two packed int32 lanes. LoongArch  */
/* has no MMX, so we emulate __m64 as a plain 64-bit integer and       */
/* implement each intrinsic as integer arithmetic on its two lanes.    */
/* This is fully compatible with the xmm_mm_union used by             */
/* COPY_MM_TO_XMM(), which simply bit-copies two __m64 into one __m128.*/
/* ================================================================== */

typedef int64_t __m64;

typedef union { int64_t i64; int32_t i32[2]; } __lsx_m64_u;
static inline void _lsx_m64_unpack(__m64 a, int32_t *lo, int32_t *hi) {
    __lsx_m64_u u; u.i64 = a; *lo = u.i32[0]; *hi = u.i32[1];
}
static inline __m64 _lsx_m64_pack(int32_t lo, int32_t hi) {
    __lsx_m64_u u; u.i32[0] = lo; u.i32[1] = hi; return u.i64;
}

/* Convert the low two floats of a to two int32 packed into 64 bits. */
FORCE_INLINE __m64 _mm_cvttps_pi32(__m128 a) {
    typedef float __lsx_v4f __attribute__((vector_size(16)));
    __lsx_v4f v = (__lsx_v4f)a;
    return _lsx_m64_pack((int32_t)v[0], (int32_t)v[1]);
}

FORCE_INLINE __m64 _mm_add_pi32(__m64 a, __m64 b) {
    int32_t a0,a1,b0,b1;
    _lsx_m64_unpack(a,&a0,&a1); _lsx_m64_unpack(b,&b0,&b1);
    return _lsx_m64_pack(a0+b0, a1+b1);
}
FORCE_INLINE __m64 _mm_sub_pi32(__m64 a, __m64 b) {
    int32_t a0,a1,b0,b1;
    _lsx_m64_unpack(a,&a0,&a1); _lsx_m64_unpack(b,&b0,&b1);
    return _lsx_m64_pack(a0-b0, a1-b1);
}
FORCE_INLINE __m64 _mm_and_si64(__m64 a, __m64 b) {
    return (__m64)((uint64_t)a & (uint64_t)b);
}
FORCE_INLINE __m64 _mm_andnot_si64(__m64 a, __m64 b) {
    return (__m64)((~(uint64_t)a) & (uint64_t)b);
}
FORCE_INLINE __m64 _mm_slli_pi32(__m64 a, int imm) {
    int32_t a0,a1;
    _lsx_m64_unpack(a,&a0,&a1);
    return _lsx_m64_pack(a0 << imm, a1 << imm);
}
FORCE_INLINE __m64 _mm_setzero_si64(void) {
    return (__m64)0;
}
FORCE_INLINE __m64 _mm_cmpeq_pi32(__m64 a, __m64 b) {
    int32_t a0,a1,b0,b1;
    _lsx_m64_unpack(a,&a0,&a1); _lsx_m64_unpack(b,&b0,&b1);
    int32_t r0 = (a0 == b0) ? (int32_t)0xFFFFFFFF : 0;
    int32_t r1 = (a1 == b1) ? (int32_t)0xFFFFFFFF : 0;
    return _lsx_m64_pack(r0, r1);
}

/* Convert four int32 (two from each __m64) to a __m128 of floats. */
FORCE_INLINE __m128 _mm_cvtpi32x2_ps(__m64 a, __m64 b) {
    int32_t a0,a1,b0,b1;
    _lsx_m64_unpack(a,&a0,&a1); _lsx_m64_unpack(b,&b0,&b1);
    typedef float __lsx_v4f __attribute__((vector_size(16)));
    __lsx_v4f v = { (float)a0, (float)a1, (float)b0, (float)b1 };
    return (__m128)v;
}

/* MMX state clear; a no-op on LoongArch. */
FORCE_INLINE void _mm_empty(void) { }

#endif /* SSE2LSX_H */
