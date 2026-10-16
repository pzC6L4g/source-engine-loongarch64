/*
 * mmintrin.h shim for LoongArch. See common/xmmintrin.h for rationale.
 * Redirects MMX intrinsics (e.g. _mm_empty) to the SSE -> LSX translation
 * layer so no x86 `emms` instruction is emitted.
 */
#if defined(__loongarch__)
#include "sse2lsx.h"
#else
#include_next <mmintrin.h>
#endif
