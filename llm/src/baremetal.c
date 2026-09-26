/* Bare-metal support for newlib's libm, which sets errno through __errno(). The riscv-tests
   environment links no C library, so provide the one symbol it needs. */
int *__errno(void) {
  static int err;
  return &err;
}
