#ifndef STORIES_H
#define STORIES_H

/* stories260K (karpathy/tinyllamas), frozen as int8 in weights/stories260k.h.
   Every matmul goes through backend_gemm_i8_o8, the Lean contract (#34): 116 GEMMs per token
   (per layer 7 projections plus 2 attention matmuls for each of the 8 heads, plus the LM head),
   with 46 static output scales (9 per layer, plus the LM head). */

enum { STORIES_MAX_SEQ = 512 };

/* Greedy decode. tokens[] holds the prompt and must have room for max_new more.
   Returns the total length. Starts a fresh KV cache. */
int stories_generate(int *tokens, int prompt_len, int max_new);

/* The example stored in the header: "Once upon a time" and its expected continuation. */
const int *stories_prompt(int *n);
const int *stories_expected(int *n);

/* Number of GEMM calls since the last stories_generate() started. */
long stories_gemm_count(void);

#endif
