# llm

Decoder used by this project. Matrix multiplies are the only work that moves onto the accelerator. Everything else stays on the CPU of whatever machine is hosting it.

```
llm/
  include/          model API and the GEMM backend contract
  src/model.c       embed, RMSNorm, RoPE, attention, MLP, generate
  src/backend_cpu.c reference GEMM, runs here before the FPGA exists
  src/backend_gemmini.c
                    Spike / Gemmini RoCC build
  src/backend_f2.c  same GEMM through the F2 register window
  fpga/f2/          flash script and the register map the bitstream must implement
  weights/          drop quantized int8 weights here when they exist
```

Dimensions match a 16-wide systolic array: hidden 64, 4 heads of 16, FFN 128, 2 layers, context 32.

| Step | Where it runs after the F2 image is loaded |
| --- | --- |
| QKV, QKᵀ, attention×V, output proj, MLP, LM head | FPGA GEMM in the AFI |
| MLP ReLU | FPGA, `GEMM_FLAG_RELU` |
| Embed, RMSNorm, RoPE, softmax, residual, KV cache | x86 CPU on the F2 instance |

## Every matmul goes through one GEMM call

Each matmul in `src/model.c` calls `backend_gemm_i8` (`include/backend.h`), either directly or through `linear()`, which calls it at line 123. No matmul bypasses the backend. Shapes are `C[M, N] = A[M, K] · B[K, N]` for one token at position `pos`, with `len = pos + 1`.

| Matmul | Call site | M × N × K | Flags | Per token |
| --- | --- | --- | --- | --- |
| QKV (fused Q, K, V projection) | `block_forward`, line 187 | 1 × 192 × 64 | | once per layer |
| QKᵀ (attention scores) | `attention`, line 166 | 1 × len × 16 | `trans_b` | per layer and head |
| attention × V | `attention`, line 173 | 1 × 16 × len | | per layer and head |
| Output projection | `block_forward`, line 198 | 1 × 64 × 64 | | once per layer |
| MLP up | `block_forward`, line 204 | 1 × 128 × 64 | `relu` | once per layer |
| MLP down | `block_forward`, line 205 | 1 × 64 × 128 | | once per layer |
| LM head | `llm_forward`, line 221 | 1 × 64 × 64 | | once, after the last token |

Host-only steps, none of which is a matmul:

| Step | Where |
| --- | --- |
| Embed (a row lookup in `tok_emb`) | `embed`, line 215 |
| RMSNorm | `rmsnorm`, lines 186, 203, 220 |
| RoPE | `rope`, lines 192–193 |
| Softmax | `softmax`, line 169 |
| Residual add | `add_vec`, lines 200, 207 |
| KV cache write | `block_forward`, lines 194–195 |
| Activation quantization to int8 | `quantize`, before each GEMM call |

## Today

```shell
make test       # CPU reference
make test-f2    # same model, GEMM goes through the F2 register protocol in software
```

`test-f2` does not need an FPGA. It writes the OCL register block defined in `fpga/f2/cl_gemm_regs.h` and a stand-in engine performs the int8 mac. That is the host path you will keep when the real image is loaded.

## Flash on the F2 instance

The bitstream is customer logic on the F2 Small Shell (no shell DMA). Build it from the AWS FPGA HDK, upload the DCP, and create an AFI. When `describe-fpga-images` shows the image as available:

```shell
cp fpga/f2/afi.env.example fpga/f2/afi.env   # set AGFI_ID to the agfi-... id
fpga/f2/load_afi.sh
make llm-f2
export F2_BAR=/sys/bus/pci/devices/<bdf>/resource0
./llm-f2
```

`load_afi.sh` uses `fpga-load-local-image -I agfi-...`. The regional `afi-...` id is for the EC2 API. The load tool wants the global id.

The image must implement `fpga/f2/cl_gemm_regs.h`: int8 A and B in, int32 C out, width 16. Start from the HDK `cl_axil_reg_access` example for the first register bring-up.
