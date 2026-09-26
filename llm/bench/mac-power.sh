#!/usr/bin/env bash
# Speed and power of stories260K on this Mac, for the comparison with the FPGA (#54).
#   sudo llm/bench/mac-power.sh            (from the repo root; about 3 minutes)
# powermetrics samples the CPU and GPU power every 200 ms while each benchmark runs for SECONDS.
# The benchmarks run as you, not as root. Results: llm/bench/results/mac/ (NAME.json from the
# benchmark, NAME.power.txt from powermetrics). Close other busy apps first: they add to the power.
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo "run it with sudo: powermetrics needs root" >&2; exit 1; }
user=${SUDO_USER:?run it with sudo from your own account}
llm=$(cd "$(dirname "$0")/.." && pwd)
out=$llm/bench/results/mac
SECONDS_EACH=${SECONDS_EACH:-20}
py=$llm/tools/.venv/bin/python
as_user() { sudo -u "$user" "$@"; }

mkdir -p "$out"
as_user make -s -C "$llm" llm-bench

measure() {  # NAME COMMAND...: sample power while COMMAND runs
  local name=$1; shift
  echo "== $name"
  powermetrics --samplers cpu_power,gpu_power -i 200 > "$out/$name.power.txt" 2>/dev/null &
  local pm=$!
  sleep 2
  (cd "$llm/tools" && as_user "$@") > "$out/$name.json"
  sleep 1
  kill "$pm"; wait "$pm" 2>/dev/null || true
  tail -c 200 "$out/$name.json"; echo
}

echo "== idle (15 s): don't touch the Mac"
date +%s > "$out/idle.start"
powermetrics --samplers cpu_power,gpu_power -i 200 -n 75 > "$out/idle.power.txt" 2>/dev/null
date +%s > "$out/idle.end"

measure c-int8-1core       "$llm/llm-bench" "$SECONDS_EACH"
measure torch-cpu-1t-b1    "$py" bench_torch.py --device cpu --batch 1 --threads 1 --seconds "$SECONDS_EACH"
measure torch-cpu-b64      "$py" bench_torch.py --device cpu --batch 64 --seconds "$SECONDS_EACH"
measure torch-mps-b1       "$py" bench_torch.py --device mps --batch 1 --seconds "$SECONDS_EACH"
measure torch-mps-b64      "$py" bench_torch.py --device mps --batch 64 --seconds "$SECONDS_EACH"
measure torch-mps-b1024    "$py" bench_torch.py --device mps --batch 1024 --seconds "$SECONDS_EACH"

sysctl -n machdep.cpu.brand_string > "$out/machine.txt"
sw_vers >> "$out/machine.txt"
chown -R "$user" "$out"
echo "done: $out"
