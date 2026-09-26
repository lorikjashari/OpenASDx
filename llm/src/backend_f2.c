#include "backend.h"
#include "cl_gemm_regs.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef F2_SIM

static uint8_t bar[GEMM_BAR_BYTES];

static uint32_t rd32(uint32_t off) {
  uint32_t v;
  memcpy(&v, bar + off, sizeof(v));
  return v;
}

static void wr32(uint32_t off, uint32_t v) {
  memcpy(bar + off, &v, sizeof(v));
}

static void engine_run(void) {
  uint32_t M = rd32(GEMM_OFF_M);
  uint32_t N = rd32(GEMM_OFF_N);
  uint32_t K = rd32(GEMM_OFF_K);
  int relu = (rd32(GEMM_OFF_FLAGS) & GEMM_FLAG_RELU) != 0;
  const int8_t *A = (const int8_t *)(bar + GEMM_A_BASE);
  const int8_t *B = (const int8_t *)(bar + GEMM_B_BASE);

  if (M == 0 || N == 0 || K == 0 || M > GEMM_MAX_M || N > GEMM_MAX_N || K > GEMM_MAX_K) {
    wr32(GEMM_OFF_STATUS, GEMM_STAT_ERR);
    return;
  }

  wr32(GEMM_OFF_STATUS, GEMM_STAT_BUSY);
  for (uint32_t m = 0; m < M; m++) {
    for (uint32_t n = 0; n < N; n++) {
      int32_t acc = 0;
      for (uint32_t k = 0; k < K; k++)
        acc += (int32_t)A[m * K + k] * (int32_t)B[k * N + n];
      if (relu && acc < 0)
        acc = 0;
      memcpy(bar + GEMM_C_BASE + (m * N + n) * sizeof(int32_t), &acc, sizeof(acc));
    }
  }
  wr32(GEMM_OFF_DIM, 16u);
  wr32(GEMM_OFF_STATUS, GEMM_STAT_DONE);
}

static void hw_write(uint32_t off, const void *src, size_t n) {
  memcpy(bar + off, src, n);
  if (off == GEMM_OFF_CTRL) {
    uint32_t ctrl;
    memcpy(&ctrl, src, sizeof(ctrl));
    if (ctrl & GEMM_CTRL_START)
      engine_run();
  }
}

static void hw_read(uint32_t off, void *dst, size_t n) {
  memcpy(dst, bar + off, n);
}

const char *backend_name(void) { return "f2-sim"; }

#else

#include <fcntl.h>
#include <unistd.h>

static int bar_fd = -1;

static void hw_open(void) {
  if (bar_fd >= 0)
    return;
  const char *path = getenv("F2_BAR");
  if (!path || !path[0])
    path = "/sys/bus/pci/devices/0000:00:1e.0/resource0";
  bar_fd = open(path, O_RDWR);
  if (bar_fd < 0) {
    fprintf(stderr,
            "F2 GEMM BAR is not open (%s).\n"
            "Load the image first:  llm/fpga/f2/load_afi.sh\n"
            "Then set F2_BAR to the slot's resource0 path.\n",
            strerror(errno));
    exit(1);
  }
}

static void hw_write(uint32_t off, const void *src, size_t n) {
  hw_open();
  if (pwrite(bar_fd, src, n, (off_t)off) != (ssize_t)n) {
    fprintf(stderr, "F2 BAR write at 0x%x failed: %s\n", off, strerror(errno));
    exit(1);
  }
}

static void hw_read(uint32_t off, void *dst, size_t n) {
  hw_open();
  if (pread(bar_fd, dst, n, (off_t)off) != (ssize_t)n) {
    fprintf(stderr, "F2 BAR read at 0x%x failed: %s\n", off, strerror(errno));
    exit(1);
  }
}

const char *backend_name(void) { return "f2"; }

#endif

void backend_gemm_i8(const int8_t *A, float a_scale,
                     const int8_t *B, float b_scale,
                     float *C, int M, int N, int K,
                     int trans_b, int relu) {
  if (M < 1 || N < 1 || K < 1 ||
      (uint32_t)M > GEMM_MAX_M || (uint32_t)N > GEMM_MAX_N || (uint32_t)K > GEMM_MAX_K) {
    fprintf(stderr, "F2 GEMM tile %dx%dx%d exceeds the register window\n", M, N, K);
    exit(1);
  }

  int8_t packed_b[GEMM_MAX_K * GEMM_MAX_N];
  const int8_t *b_ptr = B;
  if (trans_b) {
    for (int n = 0; n < N; n++)
      for (int k = 0; k < K; k++)
        packed_b[k * N + n] = B[n * K + k];
    b_ptr = packed_b;
  }

  uint32_t zero = 0;
  uint32_t m = (uint32_t)M, n = (uint32_t)N, k = (uint32_t)K;
  uint32_t flags = relu ? GEMM_FLAG_RELU : 0;
  uint32_t start = GEMM_CTRL_START;

  hw_write(GEMM_OFF_STATUS, &zero, sizeof(zero));
  hw_write(GEMM_A_BASE, A, (size_t)M * (size_t)K);
  hw_write(GEMM_B_BASE, b_ptr, (size_t)K * (size_t)N);
  hw_write(GEMM_OFF_M, &m, sizeof(m));
  hw_write(GEMM_OFF_N, &n, sizeof(n));
  hw_write(GEMM_OFF_K, &k, sizeof(k));
  hw_write(GEMM_OFF_FLAGS, &flags, sizeof(flags));
  hw_write(GEMM_OFF_CTRL, &start, sizeof(start));

  uint32_t status = 0;
  for (int spin = 0; spin < 1000000; spin++) {
    hw_read(GEMM_OFF_STATUS, &status, sizeof(status));
    if (status & GEMM_STAT_ERR) {
      fprintf(stderr, "F2 GEMM engine returned an error\n");
      exit(1);
    }
    if (status & GEMM_STAT_DONE)
      break;
  }
  if ((status & GEMM_STAT_DONE) == 0) {
    fprintf(stderr, "F2 GEMM timed out waiting for DONE\n");
    exit(1);
  }

  const float scale = a_scale * b_scale;
  for (int i = 0; i < M * N; i++) {
    int32_t acc = 0;
    hw_read(GEMM_C_BASE + (uint32_t)i * sizeof(int32_t), &acc, sizeof(acc));
    float v = scale * (float)acc;
    if (relu && v < 0.f)
      v = 0.f;
    C[i] = v;
  }
}
