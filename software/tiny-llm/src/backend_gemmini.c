/* RISC-V / Spike / FireSim build only.
   Host gcc does not see gemmini.h; keep this file out of the default Makefile.

   Requires the software submodules and a generated gemmini_params.h:
     git submodule update --init software/libgemmini software/gemmini-rocc-tests
   Use GemminiCustomConfigs.ibertInferenceConfig if you fuse SOFTMAX / IGELU
   on the store path. The default config can still run these matmuls; softmax
   and RMSNorm stay on the Rocket core. */

#include "backend.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gemmini.h"

enum { GEMM_MAX = 192 * 128 };

static int8_t pad_a[GEMM_MAX];
static int8_t pad_b[GEMM_MAX];
static acc_t acc[GEMM_MAX];

void backend_gemm_i8(const int8_t *A, float a_scale,
                     const int8_t *B, float b_scale,
                     float *C, int M, int N, int K,
                     int trans_b, int relu) {
  /* WS allows transpose_B and full int32 move-out. Identity input scales:
     the MAC sees raw int8 products, matching backend_cpu.c. */
  const int8_t *b_ptr = B;
  if (trans_b) {
    for (int n = 0; n < N; n++)
      for (int k = 0; k < K; k++)
        pad_b[k * N + n] = B[n * K + k];
    b_ptr = pad_b;
  }

  memcpy(pad_a, A, (size_t)M * (size_t)K);

  tiled_matmul_auto((size_t)M, (size_t)N, (size_t)K,
                    pad_a, b_ptr, NULL, acc,
                    (size_t)K, (size_t)N, 0, (size_t)N,
                    MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
                    relu ? RELU : NO_ACTIVATION, ACC_SCALE_IDENTITY, 0,
                    false, false, false, true, false, 0, WS);

  const float scale = a_scale * b_scale;
  for (int i = 0; i < M * N; i++) {
    float v = scale * (float)acc[i];
    if (relu && v < 0.f)
      v = 0.f;
    C[i] = v;
  }
}
