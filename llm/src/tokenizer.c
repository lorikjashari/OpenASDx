#include "tokenizer.h"
#include "tok512.h"

#include <string.h>

/* Byte-fallback tokens <0x00> to <0xFF> are ids 3 to 258. In tok512.h they are stored as their
   byte, so they must never match a lookup: Tokenizer.encode() looks pieces up by their spelling,
   which for these is "<0xXX>". */
enum { BYTE_FIRST = 3, BYTE_LAST = 258, MAX_TEXT = 512 };

const char *tok_piece(int token) { return tok_pieces + tok_offsets[token]; }

/* The token whose piece is exactly s[0..n), or -1. */
static int lookup(const char *s, int n) {
  for (int t = BYTE_LAST + 1; t < TOK_VOCAB; t++) {
    const char *p = tok_piece(t);
    if ((int)strlen(p) == n && memcmp(p, s, n) == 0)
      return t;
  }
  return -1;
}

int tok_encode(const char *text, int *out, int max) {
  char buf[MAX_TEXT + 2];
  int toks[MAX_TEXT + 1], n = 0;
  int len = (int)strlen(text);
  if (len > MAX_TEXT || max < 1)
    return -1;
  buf[0] = ' '; /* sentencepiece's dummy prefix */
  memcpy(buf + 1, text, len + 1);
  len++;

  /* One token per character (a whole UTF-8 sequence), or its bytes if no piece matches. */
  for (int i = 0; i < len;) {
    int clen = 1;
    unsigned char c = (unsigned char)buf[i];
    if (c >= 0xf0) clen = 4;
    else if (c >= 0xe0) clen = 3;
    else if (c >= 0xc0) clen = 2;
    if (i + clen > len)
      clen = len - i;
    int t = lookup(buf + i, clen);
    if (t >= 0)
      toks[n++] = t;
    else
      for (int k = 0; k < clen; k++)
        toks[n++] = (unsigned char)buf[i + k] + BYTE_FIRST;
    i += clen;
  }

  /* Merge the adjacent pair whose joined piece scores highest, until no pair joins. */
  for (;;) {
    float best = -1e10f;
    int best_i = -1, best_t = -1;
    for (int i = 0; i + 1 < n; i++) {
      if (toks[i] >= BYTE_FIRST && toks[i] <= BYTE_LAST)
        continue; /* "<0xXX>" + anything is never a piece */
      if (toks[i + 1] >= BYTE_FIRST && toks[i + 1] <= BYTE_LAST)
        continue;
      char pair[64];
      const char *a = tok_piece(toks[i]), *b = tok_piece(toks[i + 1]);
      int la = (int)strlen(a), lb = (int)strlen(b);
      if (la + lb >= (int)sizeof pair)
        continue;
      memcpy(pair, a, la);
      memcpy(pair + la, b, lb);
      int t = lookup(pair, la + lb);
      if (t >= 0 && tok_scores[t] > best) {
        best = tok_scores[t];
        best_i = i;
        best_t = t;
      }
    }
    if (best_i < 0)
      break;
    toks[best_i] = best_t;
    memmove(toks + best_i + 1, toks + best_i + 2, (n - best_i - 2) * sizeof *toks);
    n--;
  }

  if (n + 1 > max)
    return -1;
  out[0] = TOK_BOS;
  memcpy(out + 1, toks, n * sizeof *toks);
  return n + 1;
}
