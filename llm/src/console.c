#include "console.h"
#include "tokenizer.h"
#include "tok512.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(BAREMETAL) && defined(CONSOLE_UART)
/* The chip's SiFive UART (Chipyard's WithUART, 0x10020000). Its baud divisor comes from reset and
   matches FireSim's UART bridge. */
#define UART_TXFIFO (*(volatile int32_t *)0x10020000) /* reads bit 31 set while full */
#define UART_RXFIFO (*(volatile int32_t *)0x10020004) /* reads bit 31 set while empty */
#define UART_TXCTRL (*(volatile uint32_t *)0x10020008) /* bit 0: transmit enable */
#define UART_RXCTRL (*(volatile uint32_t *)0x1002000c) /* bit 0: receive enable */

static void uart_putc(char c) {
  while (UART_TXFIFO < 0)
    ;
  UART_TXFIFO = (unsigned char)c;
}

void con_put(const char *s) {
  UART_TXCTRL |= 1;
  for (; *s; s++) {
    if (*s == '\n')
      uart_putc('\r');
    uart_putc(*s);
  }
}

int con_getc(void) {
  UART_RXCTRL |= 1;
  int32_t v;
  while ((v = UART_RXFIFO) < 0)
    ;
  return v & 0xff;
}

int con_echoes(void) { return 1; }

static unsigned long rdcycle(void) {
  unsigned long c;
  __asm__ volatile("rdcycle %0" : "=r"(c));
  return c;
}

/* The transmit FIFO holds 8 characters; 20M cycles is generous. */
void con_drain(void) {
  unsigned long t0 = rdcycle();
  while (rdcycle() - t0 < 20000000ul)
    ;
}
#elif defined(BAREMETAL)
void printstr(const char *s); /* riscv-tests syscalls.c: an unbuffered HTIF write */

void con_put(const char *s) { printstr(s); }
int con_getc(void) { return -1; }
int con_echoes(void) { return 0; }
void con_drain(void) {}
#else
void con_put(const char *s) {
  fputs(s, stdout);
  fflush(stdout);
}
int con_getc(void) {
  int c = getchar();
  return c == EOF ? -1 : c;
}
int con_echoes(void) { return 0; } /* the terminal echoes */
void con_drain(void) {}
#endif

enum { WRAP = 96 };
static int prev = TOK_BOS, column;

void con_new_story(void) {
  prev = TOK_BOS;
  column = 0;
}

/* The first piece after BOS loses its leading space, as in Tokenizer.decode(). A piece that
   starts a word and would cross WRAP starts a new line instead of its space. */
void con_put_token(int t) {
  const char *p = tok_piece(t);
  int len = (int)strlen(p);
  if (prev == TOK_BOS && *p == ' ') {
    p++;
    len--;
  } else if (*p == ' ' && column + len > WRAP) {
    con_put("\n");
    p++;
    len--;
    column = 0;
  }
  con_put(p);
  for (; *p; p++)
    column = *p == '\n' ? 0 : column + 1;
  prev = t;
}
