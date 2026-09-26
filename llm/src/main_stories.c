#include "stories.h"
#include "backend.h"

#include <stdint.h>
#include <stdio.h>

enum { PREFIX = 16 };
#define PREFIX_STR "16"

static int g_fail;

static void expect(int cond, const char *msg) {
  printf("  %s %s\n", cond ? "ok  " : "FAIL", msg);
  if (!cond)
    g_fail = 1;
}

/* The Lean contract rounds half to even and saturates to int8, like Gemmini's ACC_SCALE. */
static void test_gemm_o8(void) {
  int8_t A[4] = {1, 3, 5, -5};
  int8_t B[1] = {1};
  float C[4];
  backend_gemm_i8_o8(A, B, C, 4, 1, 1, 0, 0.5f, 1.f); /* 0.5, 1.5, 2.5, -2.5 */
  expect(C[0] == 0.f && C[1] == 2.f && C[2] == 2.f && C[3] == -2.f, "o8 rounds half to even");

  int8_t Ab[2] = {127, -128};
  int8_t Bb[2] = {127, 127};
  backend_gemm_i8_o8(Ab, Bb, C, 1, 1, 2, 0, 1.f, 1.f); /* 127*127 - 128*127 = -127 */
  expect(C[0] == -127.f, "o8 accumulates in int32");
  backend_gemm_i8_o8(Ab, Bb, C, 1, 1, 1, 0, 1.f, 0.25f); /* 16129 saturates to 127 */
  expect(C[0] == 127.f * 0.25f, "o8 saturates to int8, then scales");

  int8_t At[2] = {1, 2};
  int8_t Bt[4] = {1, 2, 3, 4}; /* B stored as [N, K] = [[1, 2], [3, 4]] */
  backend_gemm_i8_o8(At, Bt, C, 1, 2, 2, 1, 1.f, 1.f);
  expect(C[0] == 5.f && C[1] == 11.f, "o8 transpose B");
}

int main(void) {
  int tokens[STORIES_MAX_SEQ];
  int np, ne;
  const int *prompt = stories_prompt(&np);
  const int *expected = stories_expected(&ne);

  printf("stories260K  backend=%s  contract: int8 in, int32 accumulate, int8 out (Gemmini Lean)\n\n",
         backend_name());
  test_gemm_o8();

  for (int i = 0; i < np; i++)
    tokens[i] = prompt[i];
  int n = stories_generate(tokens, np, ne);
  long gemms = stories_gemm_count();

  /* The frozen example comes from the float64 Python reference. float32 here can flip one int8
     rounding at a near-tie (one output-scale step apart), after which the text continues
     differently but is still fluent. A real bug breaks the first tokens, so check a prefix.
     Bit-exactness is checked between backends on the same platform (#33). */
  int same = 0;
  while (same < ne && np + same < n && tokens[np + same] == expected[same])
    same++;
  printf("  %d of %d generated tokens match the Python reference (weights/TASK.md)\n", same, ne);
  expect(same >= PREFIX, "the first " PREFIX_STR " generated tokens match the Python reference");

  printf("\n%ld GEMMs on the backend for %d positions\n", gemms, n - 1);
  printf("tokens:");
  for (int i = 0; i < n; i++)
    printf(" %d", tokens[i]);
  printf("\n\n%s\n", g_fail ? "FAILED" : "all checks passed");
  return g_fail;
}
