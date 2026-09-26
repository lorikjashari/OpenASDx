/* The interactive demo: type the start of a story, and the chip continues it word by word, with
   every GEMM on Gemmini. Each prompt starts a new story. An empty line does nothing; Ctrl-D ends. */
#include "console.h"
#include "stories.h"
#include "backend.h"
#include "tokenizer.h"
#include "tok512.h"

#include <stdio.h>

enum { NEW_TOKENS = 100, MAX_LINE = 200, FPGA_HZ = 30000000 };

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

/* Read one line into buf. Returns its length, or -1 at the end of input (EOF or Ctrl-D). */
static int read_line(char *buf, int max) {
  int n = 0;
  for (;;) {
    int c = con_getc();
    if (c < 0 || c == 4)
      return n ? n : -1;
    if (c == '\r' || c == '\n') {
      if (con_echoes())
        con_put("\n");
      buf[n] = 0;
      return n;
    }
    if (c == 0x7f || c == '\b') { /* backspace */
      if (n > 0) {
        n--;
        if (con_echoes())
          con_put("\b \b");
      }
      continue;
    }
    if ((unsigned char)c < 0x20 || n + 1 >= max)
      continue;
    buf[n++] = (char)c;
    if (con_echoes()) {
      char s[2] = {(char)c, 0};
      con_put(s);
    }
  }
}

int main(void) {
  static int tokens[STORIES_MAX_SEQ];
  char line[MAX_LINE], msg[160];

  sprintf(msg, "\nstories260K, every matmul on %s. Type the start of a story; Ctrl-D ends.\n",
          backend_name());
  con_put(msg);
#ifdef BAREMETAL
  stories_set_clock(rdcycle);
#endif
  stories_set_on_token(on_token);

  for (;;) {
    con_put("\n> ");
    int len = read_line(line, MAX_LINE);
    if (len < 0)
      break;
    if (len == 0)
      continue;
    int np = tok_encode(line, tokens, STORIES_MAX_SEQ - NEW_TOKENS);
    if (np < 0) {
      con_put("(too long)\n");
      continue;
    }

    con_put("\n");
    con_new_story();
    for (int i = 1; i < np; i++) /* token 0 is BOS */
      con_put_token(tokens[i]);
    generated = 0;
    stories_generate(tokens, np, NEW_TOKENS);
    con_put("\n");

#ifdef BAREMETAL
    unsigned long tot = 0;
    for (int pos = np - 1; pos < np - 1 + generated; pos++)
      tot += stories_position_cycles(pos);
    unsigned long mean = generated ? tot / generated : 0;
    unsigned long tps10 = mean ? 10ul * FPGA_HZ / mean : 0; /* tokens/s, one decimal */
    sprintf(msg, "  (%d tokens, %lu cycles per token: %lu.%lu tokens/s at 30 MHz)\n", generated, mean,
            tps10 / 10, tps10 % 10);
#else
    sprintf(msg, "  (%d tokens)\n", generated);
#endif
    con_put(msg);
  }
  con_put("\nbye\n");
  con_drain();
  return 0;
}
