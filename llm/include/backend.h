#ifndef TINY_LLM_BACKEND_H
#define TINY_LLM_BACKEND_H

#include <stdint.h>

/* C[M, N] = dequant(A[M, K] * B[K, N]), row-major.
   trans_b: B is stored as [N, K] (used for QK^T against the KV cache).
   relu:    clamp C at 0. On Gemmini this is the RELU store activation.

   cpu:     reference, used before the FPGA is flashed.
   gemmini: Spike / RoCC systolic array.
   f2:      same matmul through the AWS F2 OCL register block after the AFI load.
*/
const char *backend_name(void);

void backend_gemm_i8(const int8_t *A, float a_scale,
                     const int8_t *B, float b_scale,
                     float *C, int M, int N, int K,
                     int trans_b, int relu);

#endif
