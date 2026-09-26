# llm

The first target is an open inference accelerator built around one width-16 matrix engine with int8 inputs, int32 accumulation, ReLU on the store path, and host-readable results. Attention maps to two matmuls on that same engine, while the host keeps softmax and the KV cache.

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
