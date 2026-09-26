#!/usr/bin/env bash
# Run ELFs on the F2 one at a time: bash ~/fpga-batch.sh ELF...
# Each run's full log goes to ~/fpga-runs/NAME.log; one line per run goes to ~/fpga-runs/summary.txt.
mkdir -p ~/fpga-runs
for elf in "$@"; do
  name=$(basename "$elf")
  log=~/fpga-runs/$name.log
  start=$(date +%s)
  bash -l ~/fpga-run.sh "$elf" > "$log" 2>&1 < /dev/null
  rc=$?
  verdict=$(grep -aoE '\*\*\* (PASSED|FAILED) \*\*\*.*' "$log" | tail -1)
  [[ -n "$verdict" ]] || verdict="no verdict (exit $rc)"
  printf '%-45s %4ss  %s\n' "$name" "$(( $(date +%s) - start ))" "$verdict" >> ~/fpga-runs/summary.txt
done
echo "BATCH DONE" >> ~/fpga-runs/summary.txt
