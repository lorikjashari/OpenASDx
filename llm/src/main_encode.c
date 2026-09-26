/* Print the tokens of each argument, for tools/check_encoder.py. */
#include "tokenizer.h"

#include <stdio.h>

int main(int argc, char **argv) {
  int toks[600];
  for (int a = 1; a < argc; a++) {
    int n = tok_encode(argv[a], toks, 600);
    for (int i = 0; i < n; i++)
      printf("%s%d", i ? " " : "", toks[i]);
    printf("\n");
  }
  return 0;
}
