CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Iinclude -Ifpga/f2
LDFLAGS ?= -lm

MODEL = src/main.c src/model.c

PYTHON ?= tools/.venv/bin/python

.PHONY: all test test-f2 test-stories stories clean

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
	rm -f llm-cpu llm-f2 llm-f2-sim llm-stories
