#include "llm.h"
#include "backend.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

static void expect(int cond, const char *msg) {
  if (cond) {
    printf("  ok   %s\n", msg);
  } else {
    printf("  FAIL %s\n", msg);
    g_fail = 1;
  }
}

static void test_gemm(void) {
  int8_t A[4] = {1, 2, 3, 4};
  int8_t B[4] = {1, 0, 0, 1};
  float C[4];
  backend_gemm_i8(A, 1.f, B, 1.f, C, 2, 2, 2, 0, 0);
  expect(C[0] == 1.f && C[1] == 2.f && C[2] == 3.f && C[3] == 4.f, "int8 gemm identity");

  int8_t Bt[4] = {1, 3, 2, 4}; /* B stored as [N, K] = [[1, 3], [2, 4]] */
  backend_gemm_i8(A, 1.f, Bt, 1.f, C, 2, 2, 2, 1, 0);
  /* A * [[1, 2], [3, 4]] = [[7, 10], [15, 22]] */
  expect(C[0] == 7.f && C[1] == 10.f && C[2] == 15.f && C[3] == 22.f, "int8 gemm transpose B");

  backend_gemm_i8(A, 1.f, B, 1.f, C, 2, 2, 2, 0, 1);
  expect(C[0] == 1.f && C[1] == 2.f && C[2] == 3.f && C[3] == 4.f, "relu keeps positives");
}

static void test_causal(void) {
  Llm model;
  KvCache a, b;
  float logits[LLM_VOCAB];
  int seq_a[3] = {4, 1, 2};
  int seq_b[3] = {4, 9, 8};

  llm_init(&model, 7);
  llm_forward(&model, &a, seq_a, 3, logits);
  llm_forward(&model, &b, seq_b, 3, logits);

  int same = memcmp(a.k[0][0], b.k[0][0], sizeof(a.k[0][0])) == 0 &&
             memcmp(a.k[1][0], b.k[1][0], sizeof(a.k[1][0])) == 0 &&
             memcmp(a.v[0][0], b.v[0][0], sizeof(a.v[0][0])) == 0;
  int diff = memcmp(a.k[0][2], b.k[0][2], sizeof(a.k[0][2])) != 0;
  expect(same, "position 0 does not see future tokens");
  expect(diff, "a later token changes its own key");
  llm_free(&model);
}

static void print_schedule(void) {
  printf("llm  backend=%s  dim=%d heads=%d head_dim=%d layers=%d ffn=%d vocab=%d seq<=%d\n",
         backend_name(), LLM_DIM, LLM_HEADS, LLM_HEAD_DIM, LLM_LAYERS, LLM_FFN, LLM_VOCAB, LLM_MAX_SEQ);
  printf("each matmul dimension is a multiple of Gemmini DIM=16\n\n");
  printf("per token:\n");
  printf("  embed              host          lookup int8 row\n");
  printf("  rmsnorm            host          (or LAYERNORM on ibertInferenceConfig)\n");
  printf("  qkv proj           GEMM          [%d,%d] x [%d,%d]\n", 1, LLM_DIM, LLM_DIM, 3 * LLM_DIM);
  printf("  rope               host\n");
  printf("  kv cache write     host          scratchpad / DRAM, length = current pos\n");
  printf("  scores QK^T        GEMM          [1,%d] x [%d, seq]^T\n", LLM_HEAD_DIM, LLM_HEAD_DIM);
  printf("  softmax            host          (or SOFTMAX store op if normalizations are on)\n");
  printf("  context AV         GEMM          [1, seq] x [seq, %d]\n", LLM_HEAD_DIM);
  printf("  out proj           GEMM          [1,%d] x [%d,%d]\n", LLM_DIM, LLM_DIM, LLM_DIM);
  printf("  residual           host\n");
  printf("  mlp up + relu      GEMM          [1,%d] x [%d,%d]  act=RELU\n", LLM_DIM, LLM_DIM, LLM_FFN);
  printf("  mlp down           GEMM          [1,%d] x [%d,%d]\n", LLM_FFN, LLM_FFN, LLM_DIM);
  printf("  residual           host\n");
  printf("  final rmsnorm      host\n");
  printf("  lm head            GEMM          [1,%d] x [%d,%d]\n\n", LLM_DIM, LLM_DIM, LLM_VOCAB);
}

int main(void) {
  print_schedule();
  printf("checks:\n");
  test_gemm();
  test_causal();

  Llm model;
  int tokens[LLM_MAX_SEQ] = {1, 5, 9};
  llm_init(&model, 7);
  int n = llm_generate(&model, tokens, 3, 5);
  printf("\ngreedy tokens:");
  for (int i = 0; i < n; i++)
    printf(" %d", tokens[i]);
  printf("\n");
  llm_free(&model);

  if (g_fail) {
    printf("FAILED\n");
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
