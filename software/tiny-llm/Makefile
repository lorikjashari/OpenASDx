CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Iinclude -Isrc
LDFLAGS ?= -lm

SRCS = src/main.c src/model.c src/backend_cpu.c

.PHONY: all test clean

all: tiny-llm

tiny-llm: $(SRCS) include/llm.h src/backend.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

test: tiny-llm
	./tiny-llm

clean:
	rm -f tiny-llm
