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
