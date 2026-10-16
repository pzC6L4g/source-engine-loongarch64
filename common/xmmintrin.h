/*
 * xmmintrin.h shim for LoongArch.
 *
 * On x86 targets this header simply forwards to the real system header via
 * #include_next. On LoongArch there is no x86 SSE/MMX hardware, so pulling in
 * the real <xmmintrin.h> would make the compiler emit unsupported instructions
 * such as `emms` (from _mm_empty) and fail the build. Instead we redirect to
 * the SSE -> LoongArch LSX translation layer (sse2lsx.h), which provides the
 * same intrinsics in portable / LSX form.
 */
#if defined(__loongarch__)
#include "sse2lsx.h"
#else
#include_next <xmmintrin.h>
#endif
