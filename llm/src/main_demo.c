/* The demo: stories260K continues the prompt one word at a time, printing each word as soon as the
   chip has chosen it. On the FPGA every GEMM runs on Gemmini; on the host, on the CPU. */
#include "console.h"
#include "stories.h"
#include "backend.h"
#include "tok512.h"

#include <stdio.h>

enum { NEW_TOKENS = 100, FPGA_HZ = 30000000 };

#ifdef BAREMETAL
static unsigned long rdcycle(void) {
  unsigned long c;
  __asm__ volatile("rdcycle %0" : "=r"(c));
  return c;
}
#endif

static int generated;

static int on_token(int t) {
  if (t == TOK_BOS || t == TOK_EOS) /* the model starts a new story: stop here */
    return 1;
  con_put_token(t);
  generated++;
  return 0;
}

int main(void) {
  int tokens[STORIES_MAX_SEQ];
  int np;
  const int *prompt = stories_prompt(&np);
  char line[160];

  sprintf(line, "\nstories260K, every matmul on %s\n\n", backend_name());
  con_put(line);
  for (int i = 0; i < np; i++)
    tokens[i] = prompt[i];
#ifdef BAREMETAL
  stories_set_clock(rdcycle);
#endif
  stories_set_on_token(on_token);

  con_new_story();
  for (int i = 1; i < np; i++) /* token 0 is BOS */
    con_put_token(prompt[i]);
  stories_generate(tokens, np, NEW_TOKENS);
  con_put("\n\n");

#ifdef BAREMETAL
  unsigned long tot = 0;
  for (int pos = np - 1; pos < np - 1 + generated; pos++)
    tot += stories_position_cycles(pos);
  unsigned long mean = generated ? tot / generated : 0;
  unsigned long tps10 = mean ? 10ul * FPGA_HZ / mean : 0; /* tokens/s, one decimal */
  sprintf(line, "%d tokens, %ld GEMMs, %lu cycles per token on average: %lu.%lu tokens/s at 30 MHz\n",
          generated, stories_gemm_count(), mean, tps10 / 10, tps10 % 10);
#else
  sprintf(line, "%d tokens, %ld GEMMs\n", generated, stories_gemm_count());
#endif
  con_put(line);
  con_drain();
  return 0;
}
