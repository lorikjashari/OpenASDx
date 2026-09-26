#ifndef TINY_LLM_H
#define TINY_LLM_H

#include <stdint.h>

/* Sized for the default Gemmini systolic array (DIM = 16).
   Every matmul dimension is a multiple of 16 so tiles need no extra padding
   beyond what tiled_matmul_auto already adds. */
enum {
  LLM_DIM = 64,
  LLM_HEADS = 4,
  LLM_HEAD_DIM = 16,
  LLM_LAYERS = 2,
  LLM_FFN = 128,
  LLM_VOCAB = 64,
  LLM_MAX_SEQ = 32
};

typedef struct {
  int8_t *data;
  int rows;
  int cols;
  float scale;
} TensorI8;

typedef struct {
  TensorI8 wqkv; /* [DIM, 3*DIM]  Q, K, V packed */
  TensorI8 wo;   /* [DIM, DIM] */
  TensorI8 w_up; /* [DIM, FFN]  ReLU fused on the accelerator */
  TensorI8 w_dn; /* [FFN, DIM] */
  float rms1[LLM_DIM];
  float rms2[LLM_DIM];
} Block;

typedef struct {
  TensorI8 tok_emb; /* [VOCAB, DIM] */
  Block blocks[LLM_LAYERS];
  float rms_f[LLM_DIM];
  TensorI8 lm_head; /* [DIM, VOCAB] */
} Llm;

typedef struct {
  float k[LLM_LAYERS][LLM_MAX_SEQ][LLM_DIM];
  float v[LLM_LAYERS][LLM_MAX_SEQ][LLM_DIM];
  int n;
} KvCache;

void llm_init(Llm *m, uint32_t seed);
void llm_free(Llm *m);

/* Run tokens[0..n) causally. Writes the last-position logits[VOCAB]. */
void llm_forward(const Llm *m, KvCache *cache, const int *tokens, int n, float *logits);

/* Greedy decode. tokens[] must hold the prompt and have room for max_new more. */
int llm_generate(const Llm *m, int *tokens, int prompt_len, int max_new);

#endif
