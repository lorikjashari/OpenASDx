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

CONSOLE = src/console.c src/tokenizer.c include/console.h include/tokenizer.h weights/tok512.h

llm-demo: $(STORIES) src/main_demo.c src/backend_cpu.c $(CONSOLE)
	$(CC) $(CFLAGS) -Iweights -o $@ src/main_demo.c src/stories.c src/backend_cpu.c src/console.c \
		src/tokenizer.c $(LDFLAGS)

# The interactive demo on the Mac: type the start of a story, with the CPU backend.
llm-chat: $(STORIES) src/main_chat.c src/backend_cpu.c $(CONSOLE)
	$(CC) $(CFLAGS) -Iweights -o $@ src/main_chat.c src/stories.c src/backend_cpu.c src/console.c \
		src/tokenizer.c $(LDFLAGS)

chat: llm-chat
	./llm-chat

demo: llm-demo
	./llm-demo

llm-encode: src/main_encode.c src/tokenizer.c include/tokenizer.h weights/tok512.h
	$(CC) $(CFLAGS) -Iweights -o $@ src/main_encode.c src/tokenizer.c $(LDFLAGS)

# Host benchmark (#54): the int8 model on one CPU core, the same code and prompt as on the FPGA.
llm-bench: $(STORIES) src/main_bench.c src/backend_cpu.c
	$(CC) $(CFLAGS) -Iweights -o $@ src/main_bench.c src/stories.c src/backend_cpu.c $(LDFLAGS)

# The profiling build (#56): the same, with every nanosecond charged to a phase of the forward pass.
# The clock reads cost time on the host, so this is only a check of the phases, not a benchmark.
llm-profile: $(STORIES) src/main_bench.c src/backend_cpu.c
	$(CC) $(CFLAGS) -DSTORIES_PROFILE -Iweights -o $@ src/main_bench.c src/stories.c src/backend_cpu.c $(LDFLAGS)

# The C encoder against Tokenizer.encode() in tools/stories_int8.py.
test-encoder: llm-encode $(PYTHON)
	cd tools && $(abspath $(PYTHON)) check_encoder.py

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

$(RV_BUILD)/%.o: src/%.c include/backend.h include/stories.h include/console.h include/tokenizer.h \
		weights/stories260k.h weights/tok512.h
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

# The same model with one backend each, for cycle counts on FireSim (#12): Gemmini alone, and the
# Rocket core alone as the baseline. Neither compares GEMM outputs; llm-stories-baremetal does.
llm-stories-gemmini-baremetal: $(RV_BUILD)/main_stories.o $(RV_BUILD)/stories.o \
		$(RV_BUILD)/backend_gemmini.o $(RV_BUILD)/baremetal.o
	$(RV_CC) $(RV_CFLAGS) $(RV_LDFLAGS) -o $@ $^ $(RV_COMMON)/syscalls.c $(RV_COMMON)/crt.S -lm -lgcc

llm-stories-cpu-baremetal: $(RV_BUILD)/main_stories.o $(RV_BUILD)/stories.o \
		$(RV_BUILD)/backend_cpu.o $(RV_BUILD)/baremetal.o
	$(RV_CC) $(RV_CFLAGS) $(RV_LDFLAGS) -o $@ $^ $(RV_COMMON)/syscalls.c $(RV_COMMON)/crt.S -lm -lgcc

# The profiling builds (#56): the cycles of each phase of the forward pass, one rdcycle per switch.
$(RV_BUILD)/stories_profile.o: src/stories.c include/backend.h include/stories.h weights/stories260k.h
	@mkdir -p $(RV_BUILD)
	$(RV_CC) $(RV_CFLAGS) -DSTORIES_PROFILE -c -o $@ $<

llm-profile-gemmini-baremetal: $(RV_BUILD)/main_stories.o $(RV_BUILD)/stories_profile.o \
		$(RV_BUILD)/backend_gemmini.o $(RV_BUILD)/baremetal.o
	$(RV_CC) $(RV_CFLAGS) $(RV_LDFLAGS) -o $@ $^ $(RV_COMMON)/syscalls.c $(RV_COMMON)/crt.S -lm -lgcc

llm-profile-cpu-baremetal: $(RV_BUILD)/main_stories.o $(RV_BUILD)/stories_profile.o \
		$(RV_BUILD)/backend_cpu.o $(RV_BUILD)/baremetal.o
	$(RV_CC) $(RV_CFLAGS) $(RV_LDFLAGS) -o $@ $^ $(RV_COMMON)/syscalls.c $(RV_COMMON)/crt.S -lm -lgcc

# The demos on bare metal (#7, #52). The Spike build prints through HTIF; the FireSim builds use
# the chip's UART (CONSOLE_UART), which shows each word at once and takes typed input.
$(RV_BUILD)/console_uart.o: src/console.c include/console.h include/tokenizer.h weights/tok512.h
	@mkdir -p $(RV_BUILD)
	$(RV_CC) $(RV_CFLAGS) -DCONSOLE_UART -c -o $@ $<

RV_DEMO = $(RV_BUILD)/stories.o $(RV_BUILD)/backend_gemmini.o $(RV_BUILD)/tokenizer.o $(RV_BUILD)/baremetal.o

llm-demo-baremetal: $(RV_BUILD)/main_demo.o $(RV_BUILD)/console.o $(RV_DEMO)
	$(RV_CC) $(RV_CFLAGS) $(RV_LDFLAGS) -o $@ $^ $(RV_COMMON)/syscalls.c $(RV_COMMON)/crt.S -lm -lgcc

llm-demo-firesim: $(RV_BUILD)/main_demo.o $(RV_BUILD)/console_uart.o $(RV_DEMO)
	$(RV_CC) $(RV_CFLAGS) $(RV_LDFLAGS) -o $@ $^ $(RV_COMMON)/syscalls.c $(RV_COMMON)/crt.S -lm -lgcc

llm-chat-firesim: $(RV_BUILD)/main_chat.o $(RV_BUILD)/console_uart.o $(RV_DEMO)
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
	rm -f llm-cpu llm-f2 llm-f2-sim llm-stories llm-stories-baremetal llm-stories-gemmini-baremetal \
		llm-stories-cpu-baremetal llm-demo llm-demo-baremetal llm-demo-firesim llm-encode llm-chat llm-chat-firesim llm-bench \
		llm-profile llm-profile-gemmini-baremetal llm-profile-cpu-baremetal
	rm -rf $(RV_BUILD)
