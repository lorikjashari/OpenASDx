#include "stories.h"
#include "backend.h"
#include "stories260k.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* The same computation as llm/tools/stories_int8.py in lean mode, in float32 on the CPU side. */

enum {
  KV_DIM = ST_KV_HEADS * ST_HEAD_DIM,
  KV_MUL = ST_HEADS / ST_KV_HEADS,
  MAX_K = ST_HIDDEN > ST_DIM ? ST_HIDDEN : ST_DIM
};

_Static_assert(ST_MAX_SEQ == STORIES_MAX_SEQ, "stories.h and the header disagree on the context");

static float kc[ST_LAYERS][ST_MAX_SEQ][KV_DIM];
static float vc[ST_LAYERS][ST_MAX_SEQ][KV_DIM];
static long gemm_count;

/* Optional cycle counter (stories_set_clock). Per position: all cycles, and those inside GEMMs. */
static unsigned long (*clock_fn)(void);
static unsigned long pos_cycles[ST_MAX_SEQ], pos_gemm_cycles[ST_MAX_SEQ];
static unsigned long gemm_cycles;

/* Every matmul goes through here: count it, and time it if a clock is set. */
static void gemm(const int8_t *A, const int8_t *B, float *C, int M, int N, int K, int trans_b,
                 float acc_scale, float out_scale) {
  gemm_count++;
  unsigned long t0 = clock_fn ? clock_fn() : 0;
  backend_gemm_i8_o8(A, B, C, M, N, K, trans_b, acc_scale, out_scale);
  if (clock_fn)
    gemm_cycles += clock_fn() - t0;
}

/* Symmetric int8 with one scale for the whole vector, as q8() in stories_int8.py. */
static float quantize(const float *x, int n, int8_t *q) {
  float amax = 0.f;
  for (int i = 0; i < n; i++)
    if (fabsf(x[i]) > amax)
      amax = fabsf(x[i]);
  float s = amax / 127.f;
  if (s == 0.f)
    s = 1.f;
  for (int i = 0; i < n; i++) {
    float r = nearbyintf(x[i] / s);
    q[i] = (int8_t)(r > 127.f ? 127.f : (r < -127.f ? -127.f : r));
  }
  return s;
}

/* y[N] = x[K] * W[K, N] on the accelerator; W is int8 with one scale. */
static void linear(const float *x, int K, const int8_t *w, float w_scale, int N, float out_scale,
                   float *y) {
  int8_t xq[MAX_K];
  float a_scale = quantize(x, K, xq);
  gemm(xq, w, y, 1, N, K, 0, a_scale * w_scale / out_scale, out_scale);
}

static void rmsnorm(float *out, const float *x, const float *w) {
  float ss = 0.f;
  for (int i = 0; i < ST_DIM; i++)
    ss += x[i] * x[i];
  float inv = 1.f / sqrtf(ss / ST_DIM + 1e-5f);
  for (int i = 0; i < ST_DIM; i++)
    out[i] = x[i] * inv * w[i];
}

/* llama2.c's RoPE: pairs (i, i+1) rotated by pos * 10000^(-(i mod head_dim) / head_dim). */
static void rope(float *v, int n, int pos) {
  for (int i = 0; i < n; i += 2) {
    float freq = 1.f / powf(10000.f, (float)(i % ST_HEAD_DIM) / (float)ST_HEAD_DIM);
    float c = cosf(pos * freq), s = sinf(pos * freq);
    float a = v[i], b = v[i + 1];
    v[i] = a * c - b * s;
    v[i + 1] = a * s + b * c;
  }
}

static void softmax(float *x, int n) {
  float mx = x[0];
  for (int i = 1; i < n; i++)
    if (x[i] > mx)
      mx = x[i];
  float sum = 0.f;
  for (int i = 0; i < n; i++) {
    x[i] = expf(x[i] - mx);
    sum += x[i];
  }
  for (int i = 0; i < n; i++)
    x[i] /= sum;
}

/* Both attention matmuls on the accelerator. Operands are activations with dynamic scales;
   only the output scale is static. */
static void attention(int l, int pos, const float *q, float *ctx) {
  static int8_t k8[ST_MAX_SEQ * ST_HEAD_DIM], v8[ST_MAX_SEQ * ST_HEAD_DIM];
  static float kpack[ST_MAX_SEQ * ST_HEAD_DIM], vpack[ST_MAX_SEQ * ST_HEAD_DIM];
  float score[ST_MAX_SEQ];
  int8_t q8[ST_HEAD_DIM], p8[ST_MAX_SEQ];
  const int len = pos + 1;

  for (int h = 0; h < ST_HEADS; h++) {
    const int g = h / KV_MUL;
    for (int t = 0; t < len; t++)
      for (int i = 0; i < ST_HEAD_DIM; i++) {
        kpack[t * ST_HEAD_DIM + i] = kc[l][t][g * ST_HEAD_DIM + i];
        vpack[t * ST_HEAD_DIM + i] = vc[l][t][g * ST_HEAD_DIM + i];
      }

    float qs = quantize(q + h * ST_HEAD_DIM, ST_HEAD_DIM, q8);
    float ks = quantize(kpack, len * ST_HEAD_DIM, k8);
    gemm(q8, k8, score, 1, len, ST_HEAD_DIM, 1, qs * ks / st_qk_out_scale[l], st_qk_out_scale[l]);
    for (int t = 0; t < len; t++)
      score[t] /= sqrtf((float)ST_HEAD_DIM);
    softmax(score, len);

    float ps = quantize(score, len, p8);
    float vs = quantize(vpack, len * ST_HEAD_DIM, v8);
    gemm(p8, v8, ctx + h * ST_HEAD_DIM, 1, ST_HEAD_DIM, len, 0, ps * vs / st_av_out_scale[l],
         st_av_out_scale[l]);
  }
}

static void forward(int tok, int pos, float *logits) {
  float x[ST_DIM], h[ST_DIM], q[ST_DIM], ctx[ST_DIM], o[ST_DIM];
  float a1[ST_HIDDEN], a3[ST_HIDDEN];

  memcpy(x, st_tok_emb + tok * ST_DIM, sizeof(x));
  for (int l = 0; l < ST_LAYERS; l++) {
    const int dd = ST_DIM * ST_DIM, dk = ST_DIM * KV_DIM, dh = ST_DIM * ST_HIDDEN;

    rmsnorm(h, x, st_rms_att + l * ST_DIM);
    linear(h, ST_DIM, st_wq + l * dd, st_wq_scale[l], ST_DIM, st_wq_out_scale[l], q);
    linear(h, ST_DIM, st_wk + l * dk, st_wk_scale[l], KV_DIM, st_wk_out_scale[l], kc[l][pos]);
    linear(h, ST_DIM, st_wv + l * dk, st_wv_scale[l], KV_DIM, st_wv_out_scale[l], vc[l][pos]);
    rope(q, ST_DIM, pos);
    rope(kc[l][pos], KV_DIM, pos);

    attention(l, pos, q, ctx);
    linear(ctx, ST_DIM, st_wo + l * dd, st_wo_scale[l], ST_DIM, st_wo_out_scale[l], o);
    for (int i = 0; i < ST_DIM; i++)
      x[i] += o[i];

    rmsnorm(h, x, st_rms_ffn + l * ST_DIM);
    linear(h, ST_DIM, st_w1 + l * dh, st_w1_scale[l], ST_HIDDEN, st_w1_out_scale[l], a1);
    linear(h, ST_DIM, st_w3 + l * dh, st_w3_scale[l], ST_HIDDEN, st_w3_out_scale[l], a3);
    for (int i = 0; i < ST_HIDDEN; i++)
      a1[i] = a1[i] / (1.f + expf(-a1[i])) * a3[i]; /* SwiGLU: silu(W1 x) * W3 x */
    linear(a1, ST_HIDDEN, st_w2 + l * dh, st_w2_scale[l], ST_DIM, st_w2_out_scale[l], o);
    for (int i = 0; i < ST_DIM; i++)
      x[i] += o[i];
  }
  rmsnorm(h, x, st_rms_final);
  linear(h, ST_DIM, st_wcls, st_wcls_scale[0], ST_VOCAB, st_wcls_out_scale[0], logits);
}

int stories_generate(int *tokens, int prompt_len, int max_new) {
  float logits[ST_VOCAB];
  int n = prompt_len;
  gemm_count = 0;
  for (int pos = 0; pos < n && n < prompt_len + max_new && n < ST_MAX_SEQ; pos++) {
    unsigned long t0 = clock_fn ? clock_fn() : 0;
    gemm_cycles = 0;
    forward(tokens[pos], pos, logits);
    if (pos == n - 1) {
      int best = 0;
      for (int i = 1; i < ST_VOCAB; i++)
        if (logits[i] > logits[best])
          best = i;
      tokens[n++] = best;
    }
    if (clock_fn) {
      pos_cycles[pos] = clock_fn() - t0;
      pos_gemm_cycles[pos] = gemm_cycles;
    }
  }
  return n;
}

const int *stories_prompt(int *n) {
  *n = ST_PROMPT_LEN;
  return st_prompt;
}

const int *stories_expected(int *n) {
  *n = ST_EXPECTED_LEN;
  return st_expected;
}

long stories_gemm_count(void) { return gemm_count; }

void stories_set_clock(unsigned long (*clock)(void)) { clock_fn = clock; }
unsigned long stories_position_cycles(int pos) { return pos_cycles[pos]; }
unsigned long stories_position_gemm_cycles(int pos) { return pos_gemm_cycles[pos]; }
