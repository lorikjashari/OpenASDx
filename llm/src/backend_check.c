/* Runs every Lean GEMM on Gemmini and on the CPU reference and compares the results bit for bit.
   Both backends are compiled into the same binary with renamed entry points (see the Makefile),
   so the comparison runs on one platform with one libm. The Gemmini result is what the model uses. */

#include "backend.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void gemmini_gemm_i8_o8(const int8_t *A, const int8_t *B, float *C, int M, int N, int K,
                        int trans_b, float acc_scale, float out_scale);
void cpu_gemm_i8_o8(const int8_t *A, const int8_t *B, float *C, int M, int N, int K,
                    int trans_b, float acc_scale, float out_scale);

enum { MAX_C = 512, MAX_REPORTS = 5 };

static long calls, bad_calls, bad_elems;

const char *backend_name(void) { return "gemmini, checked against cpu"; }

void backend_gemm_i8_o8(const int8_t *A, const int8_t *B, float *C,
                        int M, int N, int K, int trans_b,
                        float acc_scale, float out_scale) {
  static float ref[MAX_C];
  gemmini_gemm_i8_o8(A, B, C, M, N, K, trans_b, acc_scale, out_scale);
  cpu_gemm_i8_o8(A, B, ref, M, N, K, trans_b, acc_scale, out_scale);
  calls++;

  long bad = 0;
  int first = -1;
  for (int i = 0; i < M * N; i++)
    if (memcmp(&C[i], &ref[i], sizeof(float)) != 0) {
      if (first < 0)
        first = i;
      bad++;
    }
  if (bad) {
    bad_calls++;
    bad_elems += bad;
    if (bad_calls <= MAX_REPORTS)
      printf("  mismatch in GEMM %ld (M=%d N=%d K=%d trans_b=%d): %ld of %d elements, first at %d: "
             "gemmini q=%d cpu q=%d\n",
             calls, M, N, K, trans_b, bad, M * N, first, (int)(C[first] / out_scale),
             (int)(ref[first] / out_scale));
  }
}

long backend_mismatches(void) { return bad_elems; }
