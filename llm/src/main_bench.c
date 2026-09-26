/* Host benchmark: stories260K's int8 model (the Lean contract, the same code as on the FPGA) on one
   CPU core. The same prompt and 30 new tokens as llm-stories.
     ./llm-bench              ten generations, the mean time per generated token
     ./llm-bench SECONDS      generations back to back for SECONDS, for a power measurement
   Prints one JSON line. */
#include "stories.h"
#include "backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static unsigned long now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (unsigned long)t.tv_sec * 1000000000ul + (unsigned long)t.tv_nsec;
}

static int np, ne;

/* One generation; returns the nanoseconds of the generated tokens' positions. */
static unsigned long generate_once(int *tokens, int *n_out) {
  const int *prompt = stories_prompt(&np);
  stories_expected(&ne);
  for (int i = 0; i < np; i++)
    tokens[i] = prompt[i];
  int n = stories_generate(tokens, np, ne);
  unsigned long ns = 0;
  for (int pos = np - 1; pos < n - 1; pos++)
    ns += stories_position_cycles(pos); /* the clock is in nanoseconds here */
  *n_out = n - np;
  return ns;
}

int main(int argc, char **argv) {
  static int tokens[STORIES_MAX_SEQ];
  double seconds = argc > 1 ? atof(argv[1]) : 0;
  stories_set_clock(now_ns);

  int n;
  generate_once(tokens, &n); /* warm up */
  long t_start = (long)time(NULL); /* wall clock, to match power samples */
  unsigned long gen_ns = 0, start = now_ns();
  long gens = 0, toks = 0;
  do {
    gen_ns += generate_once(tokens, &n);
    toks += n;
    gens++;
  } while (seconds > 0 ? (now_ns() - start) < seconds * 1e9 : gens < 10);
  double wall = (now_ns() - start) / 1e9;
  long t_end = (long)time(NULL);

  printf("{\"system\": \"cpu-c-int8\", \"backend\": \"%s\", \"threads\": 1, \"batch\": 1, "
         "\"generations\": %ld, \"tokens\": %ld, \"wall_s\": %.3f, \"ns_per_token\": %.0f, "
         "\"tokens_per_s\": %.1f, \"t_start\": %ld, \"t_end\": %ld, \"first_tokens\": [",
         backend_name(), gens, toks, wall, (double)gen_ns / toks, toks / (gen_ns / 1e9), t_start, t_end);
  for (int i = 0; i < np + n; i++)
    printf("%s%d", i ? ", " : "", tokens[i]);
  printf("]");
  if (stories_phases()) { /* the profiling build (#56): the last generation's ns per token, by phase */
    printf(", \"ns_per_token_by_phase\": {");
    for (int i = 0; i < stories_phases(); i++)
      printf("%s\"%s\": %.0f", i ? ", " : "", stories_phase_name(i), (double)stories_phase_cycles(i) / n);
    printf("}");
  }
  printf("}\n");
  return 0;
}
