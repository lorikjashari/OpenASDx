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

/* Lean contract (FireSimLeanGemminiRocketConfig, the prebuilt F2 image): the int32 accumulator
   cannot leave Gemmini, so each result is scaled and saturated to int8 on the accelerator:
     q = clamp(round_half_even((float)acc * acc_scale), -128, 127),  C = q * out_scale
   This is Gemmini's ACC_SCALE with a float acc_scale_t. The caller computes
   acc_scale = a_scale * b_scale / out_scale once, so every backend gets the same float. */
void backend_gemm_i8_o8(const int8_t *A, const int8_t *B, float *C,
                        int M, int N, int K, int trans_b,
                        float acc_scale, float out_scale);

#endif
