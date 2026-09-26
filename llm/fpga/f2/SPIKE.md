# Spike run: functional simulator

**This run is not the FPGA.** Spike is the RISC-V instruction-set simulator. With `--extension=gemmini` it loads a C++ model of Gemmini (`software/libgemmini`) that executes Gemmini's RoCC instructions. It checks that the results are correct, but it does not simulate Gemmini's RTL or its timing, so the cycle counts below are not Gemmini's speed. The FPGA run is S3b (#12).

## Command

Set up the container first as described in [`docker/README.md`](../../../docker/README.md). Then:

```shell
docker/dev.sh shell
cd /work/vol/chipyard && source env.sh
cd generators/gemmini/software/gemmini-rocc-tests/build/bareMetalC
spike --extension=gemmini tiled_matmul_ws-baremetal
```

`docker/dev.sh check` builds this test and runs it, together with the rest of Gemmini's bare-metal tests.

## Output

Recorded 2026-09-26: Chipyard 1.14.0, Spike from its toolchain, `libgemmini` from this repo.

```
MAT_DIM_I: 64
MAT_DIM_J: 64
MAT_DIM_K: 64
Starting gemmini matmul
Cycles taken: 120
Starting slow CPU matmul
Cycles taken: 2130524
Gemmini extension configured with:
    dim = 16
```

Exit code 0. The test multiplies two 64×64 int8 matrices on Gemmini (weight-stationary dataflow, `tiled_matmul_auto`), computes the same product on the CPU, and exits 1 if any element differs.

The whole bare-metal suite passes as well: 54 of 54 tests. `matmul_spad` and `mvin_mvout_spad` are left out because they need an external-scratchpad config, and Gemmini's own Makefile skips them too.

## Configuration

Spike's model and the tests are built with `software/libgemmini/gemmini_params.h`, which is identical to `software/gemmini-rocc-tests/include/gemmini_params.h`. This is Gemmini's default config:

| Parameter | Value |
|---|---|
| Array | `DIM 16`, a 16×16 systolic array |
| Inputs / accumulator | `int8_t` / `int32_t` |
| Scratchpad | 4 banks × 4096 rows × 16 bytes = 256 KB |
| Accumulator | 1024 rows × 16 × 4 bytes = 64 KB |
| Result read-out | `ACC_READ_FULL_WIDTH`: int32 results can be moved out |

The FPGA uses the prebuilt `FireSimLeanGemminiRocketConfig` image instead (#34). It has the same array, scratchpad and accumulator, but WS dataflow only and no int32 read-out (`acc_read_full_width = false`). Spike runs for the model should use the Lean `gemmini_params.h`, so that Spike matches the FPGA (#33).

## The model on Spike (#33)

`stories260K` (`weights/TASK.md`) as a bare-metal RISC-V binary. Every GEMM runs on Gemmini with the Lean contract: int8 out, `full_C = false`, `D = NULL`, WS. The same binary also runs every GEMM on the CPU reference (`src/backend_check.c`) and compares the two bit for bit.

```shell
docker/dev.sh shell
cd /work/vol/chipyard && source env.sh
cd /work/OpenASDx/llm
make test-spike
```

Recorded 2026-09-26:

```
stories260K  backend=gemmini, checked against cpu  contract: int8 in, int32 accumulate, int8 out (Gemmini Lean)

  ok   o8 rounds half to even
  ok   o8 accumulates in int32
  ok   o8 saturates to int8, then scales
  ok   o8 transpose B
  23 of 30 generated tokens match the Python reference (weights/TASK.md)
  ok   the first 16 generated tokens match the Python reference
  ok   no GEMM output differs between backends

3944 GEMMs on the backend for 34 positions
```

All 3944 GEMM outputs are identical between Gemmini and the CPU reference, and the tokens are the same as the Mac run (`make test-stories`). The rounding tests run through the check backend too, so they hold on Gemmini as well.

The default and Lean configs produce the same `gemmini_params.h` except for `ACC_READ_FULL_WIDTH`, which neither `gemmini.h` nor Spike's model uses. So Spike can't enforce Lean's missing int32 read-out; the backend avoids it by never setting `full_C`.
