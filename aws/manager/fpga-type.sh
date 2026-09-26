#!/usr/bin/env bash
# On the F2: type prompts into the running simulation's console, for a recording of the interactive
# demo. bash ~/fpga-type.sh PROMPTS_FILE   (one prompt per line; runs until the last one is answered)
# Waits for the chip's first "> ", types each prompt at about 12 characters a second, waits for the
# next "> " (the story is done), and ends with Ctrl-D.
set -euo pipefail
log=~/FIRESIM_RUNS_DIR/sim_slot_0/uartlog
prompts() { grep -ac '^> ' "$log" 2>/dev/null || true; }
until grep -q "Type the start of a story" "$log" 2>/dev/null; do sleep 0.5; done
sleep 1.5
seen=$(prompts)
while IFS= read -r line; do
  for ((i = 0; i < ${#line}; i++)); do
    screen -S fsim0 -X stuff "${line:i:1}"
    sleep 0.08
  done
  sleep 0.6
  screen -S fsim0 -X stuff $'\r'
  until [[ $(prompts) -gt $seen ]]; do sleep 0.3; done
  seen=$(prompts)
  sleep 2
done < "$1"
screen -S fsim0 -X stuff $'\004'
