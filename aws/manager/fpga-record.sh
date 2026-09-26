#!/usr/bin/env bash
# Record a live FPGA run as a terminal video: bash ~/fpga-record.sh ELF [NAME [PROMPTS_FILE]]
# With PROMPTS_FILE, the interactive demo: fpga-type.sh on the F2 types each prompt into the chip.
# Off camera: the workload and firesim infrasetup (copying and flashing, about a minute).
# On camera: firesim runworkload, with the chip's console followed live (uart-follow.py).
# Writes ~/recordings/NAME.cast (asciinema) and NAME.gif (agg). Needs ~/rec-venv (asciinema) and ~/agg.
set -euo pipefail
elf=$(realpath "${1:?usage: fpga-record.sh ELF [NAME]}")
name=${2:-$(basename "$elf")}
prompts=${3:-}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p ~/recordings

source ~/firesim-env.sh >/dev/null 2>&1
set -euo pipefail
deploy=$PWD/deploy
f2=$(awk '/run_farm_hosts_to_use:/{getline; gsub(/[-" :]|one_fpga_spec/, ""); print; exit}' "$deploy/config_runtime.yaml")

# The workload and the flash, off camera.
source "$here/fpga-workload.sh" "$elf"
echo "flashing the F2 ($f2), off camera"
firesim infrasetup > ~/recordings/"$name".infrasetup.log 2>&1
ssh -i ~/firesim.pem -o BatchMode=yes "ubuntu@$f2" 'rm -f ~/FIRESIM_RUNS_DIR/sim_slot_0/uartlog'
typist=""
if [[ -n "$prompts" ]]; then
  scp -q -i ~/firesim.pem "$here/fpga-type.sh" "$prompts" "ubuntu@$f2:~/"
  typist="ssh -i ~/firesim.pem -o BatchMode=yes ubuntu@$f2 'bash ~/fpga-type.sh ~/$(basename "$prompts")' &"
fi

# What the camera sees.
cat > ~/recordings/show.sh <<EOF
#!/usr/bin/env bash
printf '\033[1m# stories260K on Gemmini, on an AWS F2 FPGA (FireSim, FireSimLeanGemminiRocketConfig)\033[0m\n'
printf '\$ firesim runworkload\n\n'
source ~/firesim-env.sh >/dev/null 2>&1
firesim runworkload > ~/recordings/$name.runworkload.log 2>&1 &
$typist
ssh -i ~/firesim.pem -o BatchMode=yes ubuntu@$f2 'tail -s 0.05 -c +1 -F ~/FIRESIM_RUNS_DIR/sim_slot_0/uartlog 2>/dev/null' \
  | python3 -u $here/uart-follow.py
wait
EOF
~/rec-venv/bin/asciinema rec --overwrite --cols 100 --rows 17 --idle-time-limit 2 \
  -c "bash ~/recordings/show.sh" ~/recordings/"$name".cast
~/agg --font-size 16 ~/recordings/"$name".cast ~/recordings/"$name".gif
ls -la ~/recordings/"$name".cast ~/recordings/"$name".gif
