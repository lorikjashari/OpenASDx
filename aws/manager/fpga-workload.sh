#!/usr/bin/env bash
# Set up one bare-metal ELF as the FireSim workload: source ~/fpga-workload.sh ELF
# Run it in a shell where ~/firesim-env.sh is sourced (it uses $PWD/deploy). Sets $wl to the
# workload's name. The simulation ends when the program exits.
elf=$(realpath "${1:?usage: source fpga-workload.sh ELF}")
wl=openasdx-$(basename "$elf" | tr -c 'A-Za-z0-9_\n-' '-')
deploy=$PWD/deploy
mkdir -p "$deploy/workloads/$wl"
cp "$elf" "$deploy/workloads/$wl/program"
cat > "$deploy/workloads/$wl.json" <<JSON
{
  "benchmark_name": "$wl",
  "common_bootbinary": "program",
  "common_rootfs": null,
  "common_outputs": [],
  "common_simulation_outputs": ["uartlog"]
}
JSON
python3 - "$deploy/config_runtime.yaml" "$wl.json" <<'PY'
import re, sys
p, wl = sys.argv[1:]
s = open(p).read()
s = re.sub(r"(\nworkload:\n(?:    .*\n)*?    workload_name: )\S+", r"\g<1>" + wl, s)
s = re.sub(r"(\nworkload:\n(?:    .*\n)*?    terminate_on_completion: )\S+", r"\g<1>yes", s)
open(p, "w").write(s)
PY
