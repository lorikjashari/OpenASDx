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

| Step | Where it runs on the F2 image (FireSim, see below) |
| --- | --- |
| QKV, QKᵀ, attention×V, output proj, MLP, LM head | Gemmini, the 16×16 systolic array on the FPGA |
| MLP ReLU | Gemmini, the RELU activation on the store |
| Embed, RMSNorm, RoPE, softmax, residual, KV cache | the Rocket RISC-V core next to Gemmini, also on the FPGA |

## The model: stories260K

The demo model is the pretrained `stories260K` (tinyllamas, llama2.c), not the random-weight decoder in `src/model.c`. `weights/TASK.md` has the job, the example and the quality numbers.

The frozen model is `weights/stories260k.h`: int8 weights and calibrated scales, committed, with its SHA-256 in `weights/TASK.md`. `make stories` re-exports it, but only run that on purpose.

`tools/stories_int8.py` compares the model in float and int8 under Gemmini's GEMM contracts. `tools/export_stories.py` writes the C header, then checks it by reading it back. Both need only numpy, and `make stories` creates `tools/.venv` for them. `make test-stories` runs it in C on the Mac (CPU backend). `make test-spike`, run in the Chipyard container, runs it bare-metal on Spike with every GEMM on Gemmini, checked bit for bit against the CPU reference (see `fpga/f2/SPIKE.md`).

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

## FPGA path: FireSim with Gemmini on F2

Decided in #34. The FPGA runs a whole Chipyard chip, Rocket and Gemmini, built by FireSim, the same Gemmini we test on Spike (`fpga/f2/SPIKE.md`). The model is one RISC-V binary that runs on Spike, in Verilator, and on the FPGA.

- **Image:** the public FireSim image `agfi-0f567000cb21cb06d` (`firesim_gemmini_rocket_singlecore_no_nic`), built from `FireSimLeanGemminiRocketConfig` at 30 MHz. It is prebuilt, so no bitstream build is needed.
- **Region:** `eu-central-1` (Frankfurt), availability zone `eu-central-1b`. We keep data in the EU. The image is available there.
- **Lean config:** 16×16 array, 256 KB scratchpad and 64 KB accumulator, WS dataflow only. Gemmini accumulates in int32, but it cannot move int32 results out (`acc_read_full_width = false`). Each GEMM result is scaled to int8 on Gemmini before it leaves the accelerator, and the CPU reference applies the same scaling so the backends stay bit-exact (#33).
- **Optional later:** building `firesim_rocket_singlecore_gemmini_no_nic_l2_llc4mb_ddr3` (the full `GemminiRocketConfig`, 110 MHz) brings back int32 read-out. That is a bitstream build of several hours (#10).

**Done on 2026-09-26:** stories260K runs on the F2 with all 3,944 GEMMs on Gemmini, bit for bit equal to the CPU, and gives the same tokens as Spike and the Mac. Gemmini's `tiled_matmul_ws` test passes too. See `fpga/f2/FIRESIM.md` for the results and how to reproduce them, and `aws/README.md` for the machines.

## Backup: HDK register GEMM

`src/backend_f2.c`, `fpga/f2/cl_gemm_regs.h` and `fpga/f2/load_afi.sh` are the earlier plan: a small register GEMM in the AWS HDK customer logic, driven from the x86 host. It stays as the fallback in case the FireSim path fails (#8, #9).

The bitstream would be customer logic on the F2 Small Shell (no shell DMA), built from the AWS FPGA HDK. Once `describe-fpga-images` shows its AFI as available:

```shell
cp fpga/f2/afi.env.example fpga/f2/afi.env   # set AGFI_ID to the agfi-... id
fpga/f2/load_afi.sh
make llm-f2
export F2_BAR=/sys/bus/pci/devices/<bdf>/resource0
./llm-f2
```

`load_afi.sh` uses `fpga-load-local-image -I agfi-...`. The regional `afi-...` id is for the EC2 API. The load tool wants the global id.

That image must implement `fpga/f2/cl_gemm_regs.h`: int8 A and B in, int32 C out, width 16.
