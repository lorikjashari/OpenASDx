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

const char *backend_name(void) { return "gemmini"; }

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

/* Lean contract (backend.h): int8 out, scaled and saturated by Gemmini's ACC_SCALE on the way out
   of the accumulator. full_C stays false, since the Lean image cannot move int32 results out, and
   D is NULL (Lean hardcodes it to the garbage address). Operands are copied into aligned buffers,
   and a transposed B is transposed here, as in backend_gemm_i8 above. */
enum { O8_MAX_A = 512, O8_MAX_B = 64 * 512, O8_MAX_C = 512 };

static elem_t o8_a[O8_MAX_A] __attribute__((aligned(64)));
static elem_t o8_b[O8_MAX_B] __attribute__((aligned(64)));
static elem_t o8_c[O8_MAX_C] __attribute__((aligned(64)));
static int o8_ready;

void backend_gemm_i8_o8(const int8_t *A, const int8_t *B, float *C,
                        int M, int N, int K, int trans_b,
                        float acc_scale, float out_scale) {
  if (!o8_ready) {
    gemmini_flush(0);
    o8_ready = 1;
  }
  if (M * K > O8_MAX_A || K * N > O8_MAX_B || M * N > O8_MAX_C)
    abort();

  if (backend_part_hook)
    backend_part_hook(BACKEND_PART_COPY_IN);
  memcpy(o8_a, A, (size_t)M * (size_t)K);
  if (trans_b) {
    for (int n = 0; n < N; n++)
      for (int k = 0; k < K; k++)
        o8_b[k * N + n] = B[n * K + k];
  } else {
    memcpy(o8_b, B, (size_t)K * (size_t)N);
  }

  if (backend_part_hook)
    backend_part_hook(BACKEND_PART_MULTIPLY);
  tiled_matmul_auto((size_t)M, (size_t)N, (size_t)K,
                    o8_a, o8_b, NULL, o8_c,
                    (size_t)K, (size_t)N, 0, (size_t)N,
                    MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
                    NO_ACTIVATION, acc_scale, 0,
                    false, false, false, false, false, 0, WS);
  gemmini_fence();

  if (backend_part_hook)
    backend_part_hook(BACKEND_PART_COPY_OUT);
  for (int i = 0; i < M * N; i++)
    C[i] = (float)o8_c[i] * out_scale;
}

/* A single backend compares nothing (backend_check.c does). */
long backend_mismatches(void) { return -1; }
