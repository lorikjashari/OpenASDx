#!/usr/bin/env bash
# Run one bare-metal RISC-V ELF on the F2 through FireSim, and print its output.
# On the manager, after firesim infrasetup:   bash ~/fpga-run.sh path/to/program-baremetal
# The ELF is the same one Spike runs (HTIF console). Output: the run's uartlog, printed at the end
# and kept under deploy/results-workload/.
set -euo pipefail
elf=$(realpath "${1:?usage: fpga-run.sh ELF}")
name=openasdx-$(basename "$elf" | tr -c 'A-Za-z0-9_\n-' '-')

source ~/firesim-env.sh >/dev/null 2>&1
set -euo pipefail
deploy=$PWD/deploy

mkdir -p "$deploy/workloads/$name"
cp "$elf" "$deploy/workloads/$name/program"
cat > "$deploy/workloads/$name.json" <<EOF
{
  "benchmark_name": "$name",
  "common_bootbinary": "program",
  "common_rootfs": null,
  "common_outputs": [],
  "common_simulation_outputs": ["uartlog"]
}
EOF

# Select the workload, and end the simulation when the program exits.
python3 - "$deploy/config_runtime.yaml" "$name.json" <<'EOF'
import re, sys
p, wl = sys.argv[1:]
s = open(p).read()
s = re.sub(r"(\nworkload:\n(?:    .*\n)*?    workload_name: )\S+", r"\g<1>" + wl, s)
s = re.sub(r"(\nworkload:\n(?:    .*\n)*?    terminate_on_completion: )\S+", r"\g<1>yes", s)
open(p, "w").write(s)
EOF
grep -A3 "^workload:" "$deploy/config_runtime.yaml"

start=$(date +%s)
# infrasetup copies the workload's files to the F2 (and reflashes it), so it runs before every run.
firesim infrasetup
firesim runworkload
echo "infrasetup and runworkload took $(( $(date +%s) - start )) s"

log=$(ls -td "$deploy"/results-workload/*"$name"*/*/uartlog | head -1)
echo "=== $log"
cat "$log"
