/*
 * emmintrin.h shim for LoongArch. See common/xmmintrin.h for rationale.
 * Redirects SSE2 intrinsics to the SSE -> LSX translation layer.
 */
#if defined(__loongarch__)
#include "sse2lsx.h"
#else
#include_next <emmintrin.h>
#endif
