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

The full console output of each run is in `results/`.

**Gemmini's `tiled_matmul_ws` test** (`results/tiled_matmul_ws.uartlog`) passes. For a 64×64 by 64×64 int8 matmul:

| | cycles |
|---|---:|
| Gemmini | 2,597 |
| Rocket CPU | 3,240,505 |

**stories260K** (`results/stories260k.uartlog`) passes every check:
- Gemmini and the CPU reference give identical results for all 3,944 GEMMs (34 positions: the 5-token prompt, then 30 generated).
- The first 16 generated tokens match the Python reference. It's 23 of 30 overall; `llm/weights/TASK.md` explains why the rest differ.

The tokens decode to:

> Once upon a time, there was a little girl named Lily. She loved to play outside in the park. One day,

| | |
|---|---:|
| target cycles | 864,053,717 |
| target clock | 29.8 MHz |
| time on the FPGA | 29.0 s |

The cycle count covers the whole program, including the CPU reference and the check of every GEMM, so it is not the speed of Gemmini alone.

## Reproduce

These steps assume the manager and the F2 are running, and that `firesim infrasetup` has worked once (`aws/README.md`). On the manager:

```shell
cd ~/OpenASDx/llm     # a copy of this repo's llm/
make llm-stories-baremetal ROCC=$HOME/chipyard/generators/gemmini/software/gemmini-rocc-tests
bash ~/fpga-run.sh llm-stories-baremetal
```

`aws/manager/fpga-run.sh` wraps the ELF as a FireSim workload and prints the console output at the end. Before each run it calls `firesim infrasetup`, which copies the workload to the F2 and reflashes it. A whole run takes about 1–1.5 minutes.

Stop the F2 afterwards with `aws/down.sh ogsa-firesim-f2`.

## Still to do

- **#11:** run the rest of Gemmini's bare-metal tests, and record the cycle counts of the `*_perf` tests. Only `tiled_matmul_ws` has run so far. The Lean image supports weight-stationary mode only, so tests that need output-stationary mode don't apply.
- **#12:** measure cycles per token with Gemmini alone. This needs a build without the CPU check, with a cycle counter around each token. The 864M cycles above include the CPU reference and the comparison of every GEMM.
