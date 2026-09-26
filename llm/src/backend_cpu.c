#include "backend.h"

#include <math.h>
#include <stdint.h>

const char *backend_name(void) { return "cpu"; }

void backend_gemm_i8(const int8_t *A, float a_scale,
                     const int8_t *B, float b_scale,
                     float *C, int M, int N, int K,
                     int trans_b, int relu) {
  const float scale = a_scale * b_scale;
  for (int m = 0; m < M; m++) {
    for (int n = 0; n < N; n++) {
      int32_t acc = 0;
      for (int k = 0; k < K; k++) {
        int8_t b = trans_b ? B[n * K + k] : B[k * N + n];
        acc += (int32_t)A[m * K + k] * (int32_t)b;
      }
      float v = scale * (float)acc;
      if (relu && v < 0.f)
        v = 0.f;
      C[m * N + n] = v;
    }
  }
}

void backend_gemm_i8_o8(const int8_t *A, const int8_t *B, float *C,
                        int M, int N, int K, int trans_b,
                        float acc_scale, float out_scale) {
  for (int m = 0; m < M; m++) {
    for (int n = 0; n < N; n++) {
      int32_t acc = 0;
      for (int k = 0; k < K; k++) {
        int8_t b = trans_b ? B[n * K + k] : B[k * N + n];
        acc += (int32_t)A[m * K + k] * (int32_t)b;
      }
      /* float multiply, then round half to even (the default rounding mode), as ACC_SCALE does */
      float y = nearbyintf((float)acc * acc_scale);
      if (y > 127.f)
        y = 127.f;
      if (y < -128.f)
        y = -128.f;
      C[m * N + n] = y * out_scale;
    }
  }
}
