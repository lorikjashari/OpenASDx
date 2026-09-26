#!/usr/bin/env bash
# Runs inside the openasdx-dev container after setup-chipyard.sh. Checks the RISC-V toolchain and
# Spike with the Gemmini extension: a hello world on the proxy kernel, then Gemmini's bare-metal tests.
set -euo pipefail

CY=/work/vol/chipyard
set +u  # env.sh reads unset variables
source "$CY/env.sh" >/dev/null
set -u

echo "== hello world on spike pk"
printf '#include <stdio.h>\nint main(void) { printf("hello from spike\\n"); return 0; }\n' > /tmp/hello.c
riscv64-unknown-elf-gcc -O2 -o /tmp/hello /tmp/hello.c
spike pk /tmp/hello

echo "== Gemmini bare-metal tests on spike --extension=gemmini"
cd "$CY/generators/gemmini/software/gemmini-rocc-tests"
./build.sh bareMetalC > /tmp/rocc-build.log 2>&1 || { tail -20 /tmp/rocc-build.log; exit 1; }
cd build/bareMetalC

# matmul_spad and mvin_mvout_spad need a config with an external scratchpad; Gemmini's own
# Makefile skips them too.
rm -rf /tmp/spike-runs && mkdir -p /tmp/spike-runs
ls *-baremetal | grep -vE '^(matmul_spad|mvin_mvout_spad)-baremetal$' \
    | xargs -P "$(nproc)" -I{} bash -c 'timeout 300 spike --extension=gemmini {} > /tmp/spike-runs/{}.log 2>&1; echo "$? {}"' \
    | sort -k2 > /tmp/spike-results.txt

pass=$(awk '$1 == 0' /tmp/spike-results.txt | wc -l)
fail=$(awk '$1 != 0' /tmp/spike-results.txt | wc -l)
echo "passed $pass, failed $fail (logs in /tmp/spike-runs)"
awk '$1 != 0 {print "FAILED (exit " $1 "): " $2}' /tmp/spike-results.txt
[[ "$fail" -eq 0 ]]
