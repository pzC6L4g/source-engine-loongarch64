/*
 * smmintrin.h shim for LoongArch. See common/xmmintrin.h for rationale.
 */
#if defined(__loongarch__)
#include "sse2lsx.h"
#else
#include_next <smmintrin.h>
#endif
