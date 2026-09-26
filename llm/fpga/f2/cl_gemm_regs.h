#ifndef CL_GEMM_REGS_H
#define CL_GEMM_REGS_H

#include <stdint.h>

/* OCL AXI-lite map for the customer logic flashed onto an AWS F2 slot.
   The F2 Small Shell has no DMA engine, so this first image is a register
   GEMM: the host writes int8 tiles, sets M/N/K, and reads int32 results.
   Softmax, RMSNorm, RoPE, and the KV cache stay on the instance CPU. */

#define GEMM_OFF_CTRL   0x00u
#define GEMM_OFF_STATUS 0x04u
#define GEMM_OFF_M      0x08u
#define GEMM_OFF_N      0x0Cu
#define GEMM_OFF_K      0x10u
#define GEMM_OFF_FLAGS  0x14u
#define GEMM_OFF_DIM    0x18u /* read-only, systolic width, must read 16 */

#define GEMM_CTRL_START 1u

#define GEMM_STAT_BUSY 1u
#define GEMM_STAT_DONE 2u
#define GEMM_STAT_ERR  0x80000000u

#define GEMM_FLAG_RELU 1u

#define GEMM_A_BASE 0x1000u
#define GEMM_B_BASE 0x8000u
#define GEMM_C_BASE 0x10000u

#define GEMM_MAX_M 32u
#define GEMM_MAX_N 192u
#define GEMM_MAX_K 128u

#define GEMM_BAR_BYTES (GEMM_C_BASE + GEMM_MAX_M * GEMM_MAX_N * sizeof(int32_t))

#endif
