/*
 * immintrin.h shim for LoongArch. See common/xmmintrin.h for rationale.
 * Note: AVX/AVX2/AVX512 intrinsics are not provided by the LSX layer; code
 * that unconditionally requires them would need a dedicated port. For the
 * server build path this simply avoids emitting unsupported x86 instructions.
 */
#if defined(__loongarch__)
#include "sse2lsx.h"
#else
#include_next <immintrin.h>
#endif
