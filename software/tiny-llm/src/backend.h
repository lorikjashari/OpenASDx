#ifndef TINY_LLM_BACKEND_H
#define TINY_LLM_BACKEND_H

#include <stdint.h>

/* C[M, N] = dequant(A[M, K] * B[K, N]), row-major.
   trans_b: B is stored as [N, K] (used for QK^T against the KV cache).
   relu:    clamp C at 0. On Gemmini this is the RELU store activation.

   CPU is the reference. The Gemmini file is the same contract, executed as
   one weight-stationary tiled_matmul_auto that returns int32 accumulators. */
void backend_gemm_i8(const int8_t *A, float a_scale,
                     const int8_t *B, float b_scale,
                     float *C, int M, int N, int K,
                     int trans_b, int relu);

#endif
