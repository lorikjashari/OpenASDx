# Task: short story generation with stories260K

We use a pretrained model instead of training one, because we don't have the time to train (#38).

| | |
| --- | --- |
| Model | `stories260K` from [karpathy/tinyllamas](https://huggingface.co/karpathy/tinyllamas), trained on TinyStories. MIT license (llama2.c) |
| Shapes | dim 64, 5 layers, 8 query heads and 4 KV heads (head dim 8), FFN 172 (SwiGLU), vocab 512 (`tok512`), context 512 |
| Parameters | about 260K, a 1 MB float checkpoint |
| Quantization | int8 weights with one scale per tensor. int8 activations, scaled on the CPU before each GEMM. int32 accumulate. int8 GEMM output with one static scale per GEMM, calibrated from a float pass over 4 prompts (Gemmini Lean contract, #34) |

## Example

Prompt: `Once upon a time`, as token ids `1 403 407 261 378` (BOS first).

Expected greedy output, 30 new tokens, Lean contract:

> Once upon a time, there was a little girl named Lily. She loved to play outside in the sunshine.

The float model continues differently after "outside in the": `park. One day,`. Both are fluent, and int8 changes the continuation from that point on.

The expected token ids are `st_expected` in `stories260k.h`.

## Frozen model

`llm/weights/stories260k.h` is the frozen model: the int8 weights, one scale per weight tensor, and the 46 static output scales, calibrated once. It is committed, so the CPU, Spike and the FPGA all use the same bytes.

SHA-256: `d3554f4a1ecbcb50225c0679fa597767d31a56689329df0d5bef0c08f2f7eeab`

`make -C llm stories` re-exports it and prints the checksum. Do that only on purpose, since a different numpy or BLAS can change a scale in its last bit. The export parses the header back and checks the example before it finishes.

## Quality

From `llm/tools/stories_int8.py`, teacher-forced on a 125-token story that isn't in the calibration set:

| Mode | Perplexity | Top-1 = float |
| --- | --- | --- |
| float | 2.46 | 100% |
| int8, int32 out (full Gemmini config) | 2.49 | 96.8% |
| int8, int8 out (Lean, the FPGA image) | 2.51 | 95.2% |
