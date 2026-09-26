CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Iinclude -Ifpga/f2
LDFLAGS ?= -lm

MODEL = src/main.c src/model.c

.PHONY: all test test-f2 clean

all: llm-cpu

llm-cpu: $(MODEL) src/backend_cpu.c include/llm.h include/backend.h
	$(CC) $(CFLAGS) -o $@ $(MODEL) src/backend_cpu.c $(LDFLAGS)

llm-f2-sim: $(MODEL) src/backend_f2.c include/llm.h include/backend.h fpga/f2/cl_gemm_regs.h
	$(CC) $(CFLAGS) -DF2_SIM -o $@ $(MODEL) src/backend_f2.c $(LDFLAGS)

llm-f2: $(MODEL) src/backend_f2.c include/llm.h include/backend.h fpga/f2/cl_gemm_regs.h
	$(CC) $(CFLAGS) -o $@ $(MODEL) src/backend_f2.c $(LDFLAGS)

test: llm-cpu
	./llm-cpu

test-f2: llm-f2-sim
	./llm-f2-sim

clean:
	rm -f llm-cpu llm-f2 llm-f2-sim
