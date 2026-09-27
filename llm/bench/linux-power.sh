#!/usr/bin/env bash
# Speed and power of stories260K on a Linux machine with an NVIDIA GPU, such as Colab (#54): the
# same benchmarks as mac-power.sh, with nvidia-smi for the GPU and RAPL for the CPU, where readable.
#   llm/bench/linux-power.sh [NAME]      (from the repo root; about 3 minutes; NAME defaults to colab)
# Results: llm/bench/results/NAME/ (NAME.json from the benchmark, NAME.power.csv.gz from the sampler).
set -euo pipefail
llm=$(cd "$(dirname "$0")/.." && pwd)
out=$llm/bench/results/${1:-colab}
SECONDS_EACH=${SECONDS_EACH:-20}
py=${PYTHON:-python3}

mkdir -p "$out"
make -s -C "$llm" llm-bench

sample() {  # NAME: start the sampler, writing NAME.power.csv
  python3 "$llm/bench/power_sampler.py" > "$out/$1.power.csv" &
  pm=$!
}

measure() {  # NAME COMMAND...: sample power while COMMAND runs
  local name=$1; shift
  echo "== $name"
  sample "$name"
  sleep 2
  (cd "$llm/tools" && "$@") > "$out/$name.json"
  sleep 1
  kill "$pm"; wait "$pm" 2>/dev/null || true
  pkill -f "nvidia-smi --query-gpu=power.draw" 2>/dev/null || true
  tail -c 200 "$out/$name.json"; echo
}

echo "== idle (15 s)"
date +%s > "$out/idle.start"
sample idle
sleep 15
kill "$pm"; wait "$pm" 2>/dev/null || true
pkill -f "nvidia-smi --query-gpu=power.draw" 2>/dev/null || true
date +%s > "$out/idle.end"

measure c-int8-1core        "$llm/llm-bench" "$SECONDS_EACH"
measure torch-cpu-1t-b1     "$py" bench_torch.py --device cpu --batch 1 --threads 1 --seconds "$SECONDS_EACH"
measure torch-cpu-b64       "$py" bench_torch.py --device cpu --batch 64 --seconds "$SECONDS_EACH"
if command -v nvidia-smi > /dev/null; then
  measure torch-cuda-b1     "$py" bench_torch.py --device cuda --batch 1 --seconds "$SECONDS_EACH"
  measure torch-cuda-b64    "$py" bench_torch.py --device cuda --batch 64 --seconds "$SECONDS_EACH"
  measure torch-cuda-b1024  "$py" bench_torch.py --device cuda --batch 1024 --seconds "$SECONDS_EACH"
  measure torch-cuda-b16384 "$py" bench_torch.py --device cuda --batch 16384 --seconds "$SECONDS_EACH"
fi

gzip -9f "$out"/*.power.csv
{
  grep -m1 "model name" /proc/cpuinfo | cut -d: -f2- | sed 's/^ //'
  echo "$(nproc) vCPUs"
  nvidia-smi --query-gpu=name,power.limit --format=csv,noheader 2>/dev/null || echo "no NVIDIA GPU"
  if [[ -r /sys/class/powercap/intel-rapl:0/energy_uj ]]; then echo "RAPL readable"; else echo "RAPL not readable"; fi
  (cd "$llm/tools" && "$py" -c "import torch; print('torch', torch.__version__)")
} > "$out/machine.txt"
cat "$out/machine.txt"
echo "done: $out"
