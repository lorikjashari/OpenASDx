/* Bare-metal support. The riscv-tests environment links no C library, so provide the few symbols
   our code needs. newlib's libm sets errno through __errno(). */
int *__errno(void) {
  static int err;
  return &err;
}

/* riscv-tests' syscalls.c has memcpy, memset and strlen, but not these two (tokenizer.c uses them). */
#include <stddef.h>

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (size_t i = 0; i < n; i++)
    if (x[i] != y[i])
      return x[i] - y[i];
  return 0;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d < s)
    for (size_t i = 0; i < n; i++)
      d[i] = s[i];
  else
    for (size_t i = n; i > 0; i--)
      d[i - 1] = s[i - 1];
  return dst;
}
