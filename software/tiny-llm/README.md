# tiny-llm

The F2-ready tree is `llm/` at the repo root. This directory is the earlier Spike-side copy.



A decoder-only inference graph sized for Gemmini. It runs on the host CPU today. Every matrix multiply goes through `backend_gemm_i8`, which has a Gemmini implementation in `src/backend_gemmini.c`.

Dimensions are multiples of the default systolic array (`DIM = 16`): hidden 64, 4 heads of 16, FFN 128, 2 layers, context 32, vocab 64.

| Step | Where it runs |
| --- | --- |
| QKV, QK^T, AV, output proj, MLP, LM head | Gemmini weight-stationary matmul |
| MLP ReLU | Gemmini store activation `RELU` |
| RMSNorm, RoPE, residual, KV cache | Rocket / host |
| Softmax | Host by default. `ibertInferenceConfig` (`has_normalizations = true`) can fuse `SOFTMAX` and integer GELU on the store path |

Build the reference:

```shell
make test
```

The Gemmini backend is not in that build. It needs the Chipyard toolchain and the software submodules (`libgemmini`, `gemmini-rocc-tests`), which are empty in this checkout until `git submodule update --init`.

Weights are a fixed random int8 init so the graph and the causal mask can be checked. They are not a trained model. Swap `llm_init` for a loader when you have quantized weights.
