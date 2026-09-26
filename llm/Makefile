CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Iinclude -Ifpga/f2
LDFLAGS ?= -lm

MODEL = src/main.c src/model.c

PYTHON ?= tools/.venv/bin/python

.PHONY: all test test-f2 test-stories test-spike stories clean

all: llm-cpu

llm-cpu: $(MODEL) src/backend_cpu.c include/llm.h include/backend.h
	$(CC) $(CFLAGS) -o $@ $(MODEL) src/backend_cpu.c $(LDFLAGS)

llm-f2-sim: $(MODEL) src/backend_f2.c include/llm.h include/backend.h fpga/f2/cl_gemm_regs.h
	$(CC) $(CFLAGS) -DF2_SIM -o $@ $(MODEL) src/backend_f2.c $(LDFLAGS)

llm-f2: $(MODEL) src/backend_f2.c include/llm.h include/backend.h fpga/f2/cl_gemm_regs.h
	$(CC) $(CFLAGS) -o $@ $(MODEL) src/backend_f2.c $(LDFLAGS)

STORIES = src/main_stories.c src/stories.c include/stories.h include/backend.h weights/stories260k.h

llm-stories: $(STORIES) src/backend_cpu.c
	$(CC) $(CFLAGS) -Iweights -o $@ src/main_stories.c src/stories.c src/backend_cpu.c $(LDFLAGS)

# Bare-metal RISC-V build for Spike and FireSim (#33), inside the Chipyard environment
# (docker/dev.sh shell, then source /work/vol/chipyard/env.sh). Every GEMM runs on Gemmini and on the
# CPU reference in the same binary; the flags are Gemmini's bare-metal ones, without -ffast-math.
ROCC ?= ../software/gemmini-rocc-tests
RV_CC ?= riscv64-unknown-elf-gcc
RV_COMMON = $(ROCC)/riscv-tests/benchmarks/common
RV_CFLAGS = -DBAREMETAL=1 -DPREALLOCATE=1 -mcmodel=medany -std=gnu11 -O2 -fno-common -fno-math-errno \
	-fno-builtin-printf -fno-tree-loop-distribute-patterns -march=rv64gc -Wa,-march=rv64gc \
	-Iinclude -Iweights -I$(ROCC) -I$(ROCC)/include -I$(ROCC)/riscv-tests -I$(ROCC)/riscv-tests/env \
	-I$(RV_COMMON)
RV_LDFLAGS = -nostdlib -nostartfiles -static -T $(RV_COMMON)/test.ld
RV_BUILD = build-riscv

$(RV_BUILD)/%.o: src/%.c include/backend.h include/stories.h weights/stories260k.h
	@mkdir -p $(RV_BUILD)
	$(RV_CC) $(RV_CFLAGS) -c -o $@ $<

$(RV_BUILD)/backend_cpu_ref.o: src/backend_cpu.c include/backend.h
	@mkdir -p $(RV_BUILD)
	$(RV_CC) $(RV_CFLAGS) -Dbackend_gemm_i8_o8=cpu_gemm_i8_o8 -Dbackend_gemm_i8=cpu_gemm_i8 \
		-Dbackend_name=cpu_backend_name -Dbackend_mismatches=cpu_backend_mismatches -c -o $@ $<

$(RV_BUILD)/backend_gemmini_dut.o: src/backend_gemmini.c include/backend.h
	@mkdir -p $(RV_BUILD)
	$(RV_CC) $(RV_CFLAGS) -Dbackend_gemm_i8_o8=gemmini_gemm_i8_o8 -Dbackend_gemm_i8=gemmini_gemm_i8 \
		-Dbackend_name=gemmini_backend_name -Dbackend_mismatches=gemmini_backend_mismatches -c -o $@ $<

llm-stories-baremetal: $(RV_BUILD)/main_stories.o $(RV_BUILD)/stories.o $(RV_BUILD)/backend_check.o \
		$(RV_BUILD)/backend_cpu_ref.o $(RV_BUILD)/backend_gemmini_dut.o $(RV_BUILD)/baremetal.o
	$(RV_CC) $(RV_CFLAGS) $(RV_LDFLAGS) -o $@ $^ $(RV_COMMON)/syscalls.c $(RV_COMMON)/crt.S -lm -lgcc

test-spike: llm-stories-baremetal
	spike --extension=gemmini ./llm-stories-baremetal

test: llm-cpu
	./llm-cpu

test-f2: llm-f2-sim
	./llm-f2-sim

test-stories: llm-stories
	./llm-stories

# weights/stories260k.h is the frozen model (committed, SHA-256 in weights/TASK.md).
# Re-export it only on purpose: another numpy or BLAS can change a scale in its last bit.
stories: $(PYTHON)
	cd tools && $(abspath $(PYTHON)) export_stories.py
	shasum -a 256 weights/stories260k.h

tools/.venv/bin/python:
	python3 -m venv tools/.venv
	tools/.venv/bin/pip install -q numpy

clean:
	rm -f llm-cpu llm-f2 llm-f2-sim llm-stories llm-stories-baremetal
	rm -rf $(RV_BUILD)
