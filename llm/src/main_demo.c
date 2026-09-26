/* The demo: stories260K continues the prompt one word at a time, printing each word as soon as the
   chip has chosen it. On the FPGA every GEMM runs on Gemmini; on the host, on the CPU. */
#include "stories.h"
#include "backend.h"
#include "tok512.h"

#include <stdint.h>
#include <stdio.h>

enum { NEW_TOKENS = 100, FPGA_HZ = 30000000 };

#ifdef BAREMETAL
static unsigned long rdcycle(void) {
  unsigned long c;
  __asm__ volatile("rdcycle %0" : "=r"(c));
  return c;
}
#endif

#if defined(BAREMETAL) && defined(DEMO_UART)
/* FireSim: the chip's SiFive UART (Chipyard's WithUART, 0x10020000), which the UART bridge shows
   with little delay. An HTIF write (printstr) waits for the host, about 20M cycles per call. */
#define UART_TXFIFO (*(volatile int32_t *)0x10020000) /* bit 31 reads 1 while the FIFO is full */
#define UART_TXCTRL (*(volatile uint32_t *)0x10020008) /* bit 0 enables transmit */

static void put(const char *s) {
  UART_TXCTRL |= 1;
  for (; *s; s++) {
    if (*s == '\n') {
      while (UART_TXFIFO < 0)
        ;
      UART_TXFIFO = '\r';
    }
    while (UART_TXFIFO < 0)
      ;
    UART_TXFIFO = *s;
  }
}

/* Let the FIFO (8 characters) drain before the program exits. 20M cycles is generous. */
static void drain(void) {
  unsigned long t0 = rdcycle();
  while (rdcycle() - t0 < 20000000ul)
    ;
}
#elif defined(BAREMETAL)
void printstr(const char *s); /* riscv-tests syscalls.c: an unbuffered write to the console */

static void put(const char *s) { printstr(s); }
static void drain(void) {}
#else
static void put(const char *s) {
  fputs(s, stdout);
  fflush(stdout);
}
static void drain(void) {}
#endif

static int prev = TOK_BOS, generated;

/* Print one token's text. The first piece after BOS loses its leading space, as in decode(). */
static void put_token(int t) {
  const char *p = tok_pieces + tok_offsets[t];
  if (prev == TOK_BOS && *p == ' ')
    p++;
  put(p);
  prev = t;
}

static int on_token(int t) {
  if (t == TOK_BOS || t == TOK_EOS) /* the model starts a new story: stop here */
    return 1;
  put_token(t);
  generated++;
  return 0;
}

int main(void) {
  int tokens[STORIES_MAX_SEQ];
  int np;
  const int *prompt = stories_prompt(&np);

  char line[160];
  sprintf(line, "\nstories260K, every matmul on %s\n\n", backend_name());
  put(line);
  for (int i = 0; i < np; i++)
    tokens[i] = prompt[i];
#ifdef BAREMETAL
  stories_set_clock(rdcycle);
#endif
  stories_set_on_token(on_token);

  for (int i = 1; i < np; i++) /* token 0 is BOS */
    put_token(prompt[i]);
  int n = stories_generate(tokens, np, NEW_TOKENS);
  put("\n\n");

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
  put(line);
  drain();
  (void)n;
  return 0;
}
