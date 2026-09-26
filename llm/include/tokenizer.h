#ifndef TOKENIZER_H
#define TOKENIZER_H

/* stories260K's tok512 tokenizer (weights/tok512.h): llama2.c's BPE, the same as
   Tokenizer.encode() in tools/stories_int8.py. */

/* Encode text into BOS followed by its tokens. Returns the number of tokens written to out
   (at most max), or -1 if the text doesn't fit. */
int tok_encode(const char *text, int *out, int max);

/* The text of one token. BOS and EOS are empty. */
const char *tok_piece(int token);

#endif
