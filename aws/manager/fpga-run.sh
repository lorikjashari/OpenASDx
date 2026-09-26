#!/usr/bin/env bash
# Run one bare-metal RISC-V ELF on the F2 through FireSim, and print its output.
# On the manager, after firesim infrasetup:   bash ~/fpga-run.sh path/to/program-baremetal
# The ELF is the same one Spike runs (HTIF console). Output: the run's uartlog, printed at the end
# and kept under deploy/results-workload/.
set -euo pipefail
elf=$(realpath "${1:?usage: fpga-run.sh ELF}")

source ~/firesim-env.sh >/dev/null 2>&1
set -euo pipefail
deploy=$PWD/deploy

source "$(dirname "$0")/fpga-workload.sh" "$elf"
name=$wl
grep -A3 "^workload:" "$deploy/config_runtime.yaml"

start=$(date +%s)
# infrasetup copies the workload's files to the F2 (and reflashes it), so it runs before every run.
firesim infrasetup
firesim runworkload
echo "infrasetup and runworkload took $(( $(date +%s) - start )) s"

log=$(ls -td "$deploy"/results-workload/*"$name"*/*/uartlog | head -1)
echo "=== $log"
cat "$log"
