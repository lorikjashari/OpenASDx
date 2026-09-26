#include "llm.h"
#include "backend.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng_state;

static int8_t rnd_i8(void) {
  rng_state = rng_state * 1664525u + 1013904223u;
  return (int8_t)((int)(rng_state >> 27) - 8); /* -8 .. 7 */
}

static TensorI8 alloc_w(int rows, int cols, float scale) {
  TensorI8 t;
  t.rows = rows;
  t.cols = cols;
  t.scale = scale;
  t.data = (int8_t *)malloc((size_t)rows * (size_t)cols);
  for (int i = 0; i < rows * cols; i++)
    t.data[i] = rnd_i8();
  return t;
}

static void free_w(TensorI8 *t) {
  free(t->data);
  t->data = NULL;
}

void llm_init(Llm *m, uint32_t seed) {
  rng_state = seed ? seed : 1u;
  m->tok_emb = alloc_w(LLM_VOCAB, LLM_DIM, 0.05f);
  m->lm_head = alloc_w(LLM_DIM, LLM_VOCAB, 0.05f);
  for (int i = 0; i < LLM_DIM; i++)
    m->rms_f[i] = 1.f;
  for (int l = 0; l < LLM_LAYERS; l++) {
    Block *b = &m->blocks[l];
    b->wqkv = alloc_w(LLM_DIM, 3 * LLM_DIM, 0.05f);
    b->wo = alloc_w(LLM_DIM, LLM_DIM, 0.05f);
    b->w_up = alloc_w(LLM_DIM, LLM_FFN, 0.05f);
    b->w_dn = alloc_w(LLM_FFN, LLM_DIM, 0.05f);
    for (int i = 0; i < LLM_DIM; i++) {
      b->rms1[i] = 1.f;
      b->rms2[i] = 1.f;
    }
  }
}

void llm_free(Llm *m) {
  free_w(&m->tok_emb);
  free_w(&m->lm_head);
  for (int l = 0; l < LLM_LAYERS; l++) {
    free_w(&m->blocks[l].wqkv);
    free_w(&m->blocks[l].wo);
    free_w(&m->blocks[l].w_up);
    free_w(&m->blocks[l].w_dn);
  }
}

static float quantize(const float *x, int n, int8_t *q) {
  float maxv = 0.f;
  for (int i = 0; i < n; i++) {
    float a = fabsf(x[i]);
    if (a > maxv)
      maxv = a;
  }
  if (maxv < 1e-8f) {
    memset(q, 0, (size_t)n);
    return 1.f;
  }
  float scale = maxv / 127.f;
  for (int i = 0; i < n; i++) {
    float r = x[i] / scale;
    if (r > 127.f)
      r = 127.f;
    if (r < -127.f)
      r = -127.f;
    q[i] = (int8_t)lrintf(r);
  }
  return scale;
}

static void rmsnorm(float *x, const float *w) {
  float ss = 0.f;
  for (int i = 0; i < LLM_DIM; i++)
    ss += x[i] * x[i];
  float inv = 1.f / sqrtf(ss / (float)LLM_DIM + 1e-5f);
  for (int i = 0; i < LLM_DIM; i++)
    x[i] = x[i] * inv * w[i];
}

static void rope(float *x, int pos) {
  for (int h = 0; h < LLM_HEADS; h++) {
    float *head = x + h * LLM_HEAD_DIM;
    for (int i = 0; i < LLM_HEAD_DIM / 2; i++) {
      float freq = powf(10000.f, -2.f * (float)i / (float)LLM_HEAD_DIM);
      float ang = (float)pos * freq;
      float c = cosf(ang);
      float s = sinf(ang);
      float x0 = head[i];
      float x1 = head[i + LLM_HEAD_DIM / 2];
      head[i] = x0 * c - x1 * s;
      head[i + LLM_HEAD_DIM / 2] = x0 * s + x1 * c;
    }
  }
}

static void add_vec(float *dst, const float *src) {
  for (int i = 0; i < LLM_DIM; i++)
    dst[i] += src[i];
}

static void embed(const TensorI8 *w, int token, float *x) {
  const int8_t *row = w->data + token * w->cols;
  for (int i = 0; i < LLM_DIM; i++)
    x[i] = w->scale * (float)row[i];
}

static void linear(const float *x, int rows, const TensorI8 *w, float *y, int relu,
                   int8_t *qa) {
  float a_scale = quantize(x, rows * w->rows, qa);
  backend_gemm_i8(qa, a_scale, w->data, w->scale, y, rows, w->cols, w->rows, 0, relu);
}

static void softmax(float *x, int n) {
  float maxv = x[0];
  for (int i = 1; i < n; i++)
    if (x[i] > maxv)
      maxv = x[i];
  float sum = 0.f;
  for (int i = 0; i < n; i++) {
    x[i] = expf(x[i] - maxv);
    sum += x[i];
  }
  float inv = 1.f / sum;
  for (int i = 0; i < n; i++)
    x[i] *= inv;
}

/* One head of causal attention, both matmuls on the same GEMM backend.
   QK^T uses trans_b because the cache row is [seq, head_dim].
   Future keys are simply not in the cache yet, so the mask is the length. */
static void attention(const KvCache *cache, int layer, int pos, const float *q, float *ctx) {
  const float inv_dim = 1.f / sqrtf((float)LLM_HEAD_DIM);
  const int len = pos + 1;

  for (int h = 0; h < LLM_HEADS; h++) {
    float kpack[LLM_MAX_SEQ * LLM_HEAD_DIM];
    float vpack[LLM_MAX_SEQ * LLM_HEAD_DIM];
    float score[LLM_MAX_SEQ];
    int8_t q8[LLM_HEAD_DIM];
    int8_t k8[LLM_MAX_SEQ * LLM_HEAD_DIM];
    int8_t p8[LLM_MAX_SEQ];
    int8_t v8[LLM_MAX_SEQ * LLM_HEAD_DIM];

    for (int t = 0; t < len; t++) {
      for (int i = 0; i < LLM_HEAD_DIM; i++) {
        kpack[t * LLM_HEAD_DIM + i] = cache->k[layer][t][h * LLM_HEAD_DIM + i];
        vpack[t * LLM_HEAD_DIM + i] = cache->v[layer][t][h * LLM_HEAD_DIM + i];
      }
    }

    float qs = quantize(q + h * LLM_HEAD_DIM, LLM_HEAD_DIM, q8);
    float ks = quantize(kpack, len * LLM_HEAD_DIM, k8);
    backend_gemm_i8(q8, qs, k8, ks, score, 1, len, LLM_HEAD_DIM, 1, 0);
    for (int t = 0; t < len; t++)
      score[t] *= inv_dim;
    softmax(score, len);

    float ps = quantize(score, len, p8);
    float vs = quantize(vpack, len * LLM_HEAD_DIM, v8);
    backend_gemm_i8(p8, ps, v8, vs, ctx + h * LLM_HEAD_DIM, 1, LLM_HEAD_DIM, len, 0, 0);
  }
}

static void block_forward(const Block *b, KvCache *cache, int layer, int pos, float *x) {
  float residual[LLM_DIM];
  float qkv[3 * LLM_DIM];
  float ctx[LLM_DIM];
  float proj[LLM_DIM];
  float hidden[LLM_FFN];
  int8_t qa[3 * LLM_DIM];

  memcpy(residual, x, sizeof(residual));
  rmsnorm(x, b->rms1);
  linear(x, 1, &b->wqkv, qkv, 0, qa);

  float *q = qkv;
  float *k = qkv + LLM_DIM;
  float *v = qkv + 2 * LLM_DIM;
  rope(q, pos);
  rope(k, pos);
  memcpy(cache->k[layer][pos], k, sizeof(float) * LLM_DIM);
  memcpy(cache->v[layer][pos], v, sizeof(float) * LLM_DIM);

  attention(cache, layer, pos, q, ctx);
  linear(ctx, 1, &b->wo, proj, 0, qa);
  memcpy(x, residual, sizeof(residual));
  add_vec(x, proj);

  memcpy(residual, x, sizeof(residual));
  rmsnorm(x, b->rms2);
  linear(x, 1, &b->w_up, hidden, 1, qa);
  linear(hidden, 1, &b->w_dn, proj, 0, qa);
  memcpy(x, residual, sizeof(residual));
  add_vec(x, proj);
}

void llm_forward(const Llm *m, KvCache *cache, const int *tokens, int n, float *logits) {
  cache->n = 0;
  float x[LLM_DIM];
  int8_t qa[LLM_DIM];
  for (int pos = 0; pos < n; pos++) {
    embed(&m->tok_emb, tokens[pos], x);
    for (int l = 0; l < LLM_LAYERS; l++)
      block_forward(&m->blocks[l], cache, l, pos, x);
    cache->n = pos + 1;
  }
  rmsnorm(x, m->rms_f);
  linear(x, 1, &m->lm_head, logits, 0, qa);
}

int llm_generate(const Llm *m, int *tokens, int prompt_len, int max_new) {
  KvCache cache;
  float logits[LLM_VOCAB];
  int n = prompt_len;
  for (int t = 0; t < max_new; t++) {
    llm_forward(m, &cache, tokens, n, logits);
    int best = 0;
    for (int i = 1; i < LLM_VOCAB; i++)
      if (logits[i] > logits[best])
        best = i;
    tokens[n++] = best;
  }
  return n;
}
