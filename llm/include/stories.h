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

/* Optional cycle counter, e.g. rdcycle on bare metal. When set, stories_generate() records, for
   each position, the cycles of the whole forward pass (and the argmax) and the part inside GEMM
   calls. Positions before the last prompt token produce no new token. */
void stories_set_clock(unsigned long (*clock)(void));
unsigned long stories_position_cycles(int pos);
unsigned long stories_position_gemm_cycles(int pos);

/* Built with -DSTORIES_PROFILE (and a clock): the cycles of the generated tokens' positions, split
   by phase of the forward pass, since the last stories_generate() started. Without it,
   stories_phases() is 0. The phases add up to the positions' cycles. */
int stories_phases(void);
const char *stories_phase_name(int i);
unsigned long stories_phase_cycles(int i);

/* Optional callback, called with each new token as soon as it is chosen (after its position's
   cycles are recorded). A nonzero return stops the generation there. */
void stories_set_on_token(int (*fn)(int token));

#endif
