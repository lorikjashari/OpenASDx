#!/usr/bin/env bash
# The interactive demo on the FPGA: bash ~/fpga-chat.sh [ELF]      (default: llm-chat-firesim)
# Flashes the F2 (about a minute), starts the chip, and attaches this terminal to its console:
# type the start of a story and press Enter. Ctrl-D ends the program and the simulation.
# To leave the console without ending it: Ctrl-A then D (screen); come back with the last line
# this script prints. Needs a terminal: ssh -t.
set -euo pipefail
elf=${1:-$HOME/OpenASDx/llm/llm-chat-firesim}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p ~/fpga-runs
source ~/firesim-env.sh >/dev/null 2>&1
set -euo pipefail
source "$here/fpga-workload.sh" "$elf"
f2=$(awk '/run_farm_hosts_to_use:/{getline; gsub(/[-" :]|one_fpga_spec/, ""); print; exit}' deploy/config_runtime.yaml)

echo "flashing the F2 ($f2), about a minute"
firesim infrasetup > ~/fpga-runs/chat.infrasetup.log 2>&1
ssh -i ~/firesim.pem -o BatchMode=yes "ubuntu@$f2" 'rm -f ~/FIRESIM_RUNS_DIR/sim_slot_0/uartlog'
setsid nohup firesim runworkload > ~/fpga-runs/chat.runworkload.log 2>&1 < /dev/null &

echo "starting the chip"
until ssh -i ~/firesim.pem -o BatchMode=yes "ubuntu@$f2" \
    'grep -q "Type the start of a story" ~/FIRESIM_RUNS_DIR/sim_slot_0/uartlog 2>/dev/null'; do
  sleep 2
done
attach="ssh -t -i ~/firesim.pem ubuntu@$f2 'sudo screen -r fsim0 || screen -r fsim0'"
echo "attaching to the chip's console. Ctrl-D ends the demo; Ctrl-A D leaves it running."
echo "to come back later, on the manager: $attach"
sleep 1
eval "$attach"
