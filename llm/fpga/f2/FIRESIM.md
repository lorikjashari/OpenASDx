# Gemmini on the F2 FPGA

On 2026-09-26 we ran stories260K on a real FPGA, with every GEMM on Gemmini, and checked each GEMM bit for bit against the CPU. It runs on an AWS F2 through FireSim.

The binary is the same one that runs on Spike (`SPIKE.md`), and it produces the same tokens there, on the FPGA, and on the Mac.

## Setup

- **FPGA:** an f2.6xlarge in `eu-central-1b`, driven by a FireSim manager. See `aws/README.md` for how both machines were set up.
- **Image:** the public FireSim image `agfi-0f567000cb21cb06d` (`firesim_gemmini_rocket_singlecore_no_nic`):
  - `FireSimLeanGemminiRocketConfig`: one Rocket core with Gemmini, at 30 MHz;
  - a 16×16 array, a 256 KB scratchpad and a 64 KB accumulator;
  - weight-stationary dataflow only;
  - int8 read-out only (see "FPGA path" in `llm/README.md`).
- **Software:** Chipyard 1.14.0 (`0acc1e1`) and `gemmini-rocc-tests` `7c540b3`, as Chipyard pins them.

## Results

The full console output of each run is in `results/`. All cycle counts are target cycles of the RTL, read with `rdcycle`. They don't depend on the FPGA clock: at 30 MHz, 30M cycles take one second.

### stories260K: cycles per token

Each variant is the same model and the same prompt, and gives the same 30 tokens:

> Once upon a time, there was a little girl named Lily. She loved to play outside in the park. One day,

| build | GEMMs on | cycles per generated token (mean) | min to max | inside GEMM calls | tokens/s at 30 MHz |
|---|---|---:|---:|---:|---:|
| `llm-stories-cpu-baremetal` | Rocket core | 7,568,014 | 5.9M to 9.2M | 63% | 4.0 |
| `llm-stories-gemmini-baremetal` | Gemmini | 3,328,496 | 1.8M to 4.8M | 16% | 9.0 |

- **End to end, Gemmini makes the model 2.3× faster. The GEMMs alone run about 9× faster:** 4.77M cycles per token on the core against 0.53M on Gemmini.
- **The rest now dominates.** The Gemmini build spends 84% of its cycles on the core. It re-quantizes the whole K and V cache for every head at every position, and does the softmax, RMSNorm and RoPE. That's why a token costs about 100k more cycles at each position. Keeping the cache in int8 would remove most of it. It's the next optimization.
- A token's cost grows with its position, so the mean is over positions 5 to 34. `cycles per position` in the logs lists every position.
- The numbers are in `results/stories-gemmini.uartlog` and `results/stories-cpu.uartlog`.

`llm-stories-baremetal` runs every GEMM on Gemmini and on the core, and compares the two (`results/stories260k.uartlog`). **All 3,944 GEMM outputs are identical bit for bit** (34 positions: the 5-token prompt, then 30 generated). All three builds match the Python reference on the first 16 generated tokens, and on 23 of the 30; `llm/weights/TASK.md` explains the rest.

### Gemmini's tests

We ran 20 of Gemmini's bare-metal tests on the FPGA. **Every test within the Lean config's features passes (15).** The other 5 fail, and each needs a feature the Lean image doesn't have:
- output-stationary mode;
- a bias matrix D, which Lean wires to the garbage address;
- int32 read-out (`acc_read_full_width=false`).

The model uses none of these: it runs weight-stationary, passes no D, and reads out int8 (#33).

Tests with cycle counts:

| test | Gemmini cycles | notes |
|---|---:|---|
| `tiled_matmul_ws` | 2,597 | 64×64×64. The same matmul on the Rocket core: 3,240,505 |
| `tiled_matmul_ws_perf` | 39,815 | 128×256×256, 8.4M MACs, 82% of the ideal 32,768 cycles |
| `conv_perf` | 1,862,456 | batch 4, 224×224×3 to 32 channels, 3×3, stride 2 |
| `conv_dw_perf` | 2,529,093 | depthwise, batch 3, 112×112×17, 3×3, stride 2 |

They pass, and the console logs are in `results/`.

Other tests that pass (`results/sweep-summary.txt`):
- `mvin_mvout`, `mvin_mvout_acc`, `mvin_scale`;
- `tiled_matmul_ws_At`, `tiled_matmul_ws_Bt`, `tiled_matmul_ws_low_D`;
- `conv`, `conv_with_pool`, `conv_dw`;
- `resadd`, `global_average`.

Tests that fail, because they need a feature Lean doesn't have:

| test | needs |
|---|---|
| `matmul_ws` | bias D. Only the non-saturated entries are wrong |
| `tiled_matmul_ws_full_C` | int32 read-out |
| `transpose` | output-stationary mode |
| `padded` | output-stationary mode (it fails at `dataflow == 0`) and bias D |
| `raw_hazard` | output-stationary mode and bias D |

The `*** PASSED *** after N cycles` line covers the whole simulation (about 40M to 500M cycles here), including loading the program and the test's own setup. Use the `Cycles taken` or `took` lines instead.

## Reproduce

These steps assume the manager and the F2 are running, and that `firesim infrasetup` has worked once (`aws/README.md`). On the manager:

```shell
cd ~/OpenASDx/llm     # a copy of this repo's llm/
make llm-stories-baremetal llm-stories-gemmini-baremetal llm-stories-cpu-baremetal \
  ROCC=$HOME/chipyard/generators/gemmini/software/gemmini-rocc-tests
bash ~/fpga-batch.sh llm-stories-baremetal llm-stories-gemmini-baremetal llm-stories-cpu-baremetal
cat ~/fpga-runs/summary.txt
```

`fpga-batch.sh` runs the ELFs one after another. Each full log goes to `~/fpga-runs/NAME.log`, and `summary.txt` gets one line per run.

`aws/manager/fpga-run.sh` wraps the ELF as a FireSim workload and prints the console output at the end. Before each run it calls `firesim infrasetup`, which copies the workload to the F2 and reflashes it. A whole run takes about 1–1.5 minutes.

Stop the F2 afterwards with `aws/down.sh ogsa-firesim-f2`.

## Still to do

- **#11:** about 35 of Gemmini's bare-metal tests haven't run on the FPGA yet: the other convolution variants and the `mvin_mvout_*` stride tests, among others. `bash ~/fpga-batch.sh` runs them.
- **#10 (optional):** the full `GemminiRocketConfig` image would bring back output-stationary mode, bias D and int32 read-out, and with them the 5 failing tests.
- **Speed:** keep the KV cache in int8, so that the core stops re-quantizing it at every position.
