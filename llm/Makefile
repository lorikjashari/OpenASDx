CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Iinclude -Ifpga/f2
LDFLAGS ?= -lm

MODEL = src/main.c src/model.c

PYTHON ?= tools/.venv/bin/python

.PHONY: all test test-f2 stories clean

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

# Pretrained stories260K as int8 with calibrated static output scales (#5). Needs numpy.
stories: weights/stories260k.h

tools/.venv/bin/python:
	python3 -m venv tools/.venv
	tools/.venv/bin/pip install -q numpy

weights/stories260k.h: tools/export_stories.py tools/stories_int8.py $(PYTHON)
	cd tools && $(abspath $(PYTHON)) export_stories.py

clean:
	rm -f llm-cpu llm-f2 llm-f2-sim
