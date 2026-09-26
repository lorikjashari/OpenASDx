#!/usr/bin/env bash
# Record a live FPGA run as a terminal video: bash ~/fpga-record.sh ELF [NAME]
# Off camera: the workload and firesim infrasetup (copying and flashing, about a minute).
# On camera: firesim runworkload, with the chip's console followed live (uart-follow.py).
# Writes ~/recordings/NAME.cast (asciinema) and NAME.gif (agg). Needs ~/rec-venv (asciinema) and ~/agg.
set -euo pipefail
elf=$(realpath "${1:?usage: fpga-record.sh ELF [NAME]}")
name=${2:-$(basename "$elf")}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p ~/recordings

source ~/firesim-env.sh >/dev/null 2>&1
set -euo pipefail
deploy=$PWD/deploy
f2=$(awk '/run_farm_hosts_to_use:/{getline; gsub(/[-" :]|one_fpga_spec/, ""); print; exit}' "$deploy/config_runtime.yaml")

# The workload and the flash, exactly as fpga-run.sh does them, but off camera.
wl=openasdx-$(basename "$elf" | tr -c 'A-Za-z0-9_\n-' '-')
mkdir -p "$deploy/workloads/$wl"
cp "$elf" "$deploy/workloads/$wl/program"
printf '{"benchmark_name": "%s", "common_bootbinary": "program", "common_rootfs": null, "common_outputs": [], "common_simulation_outputs": ["uartlog"]}\n' \
  "$wl" > "$deploy/workloads/$wl.json"
python3 - "$deploy/config_runtime.yaml" "$wl.json" <<'EOF'
import re, sys
p, wl = sys.argv[1:]
s = open(p).read()
s = re.sub(r"(\nworkload:\n(?:    .*\n)*?    workload_name: )\S+", r"\g<1>" + wl, s)
s = re.sub(r"(\nworkload:\n(?:    .*\n)*?    terminate_on_completion: )\S+", r"\g<1>yes", s)
open(p, "w").write(s)
EOF
echo "flashing the F2 ($f2), off camera"
firesim infrasetup > ~/recordings/"$name".infrasetup.log 2>&1
ssh -i ~/firesim.pem -o BatchMode=yes "ubuntu@$f2" 'rm -f ~/FIRESIM_RUNS_DIR/sim_slot_0/uartlog'

# What the camera sees.
cat > ~/recordings/show.sh <<EOF
#!/usr/bin/env bash
printf '\033[1m# stories260K on Gemmini, on an AWS F2 FPGA (FireSim, FireSimLeanGemminiRocketConfig)\033[0m\n'
printf '\$ firesim runworkload\n\n'
source ~/firesim-env.sh >/dev/null 2>&1
firesim runworkload > ~/recordings/$name.runworkload.log 2>&1 &
ssh -i ~/firesim.pem -o BatchMode=yes ubuntu@$f2 'tail -s 0.05 -c +1 -F ~/FIRESIM_RUNS_DIR/sim_slot_0/uartlog 2>/dev/null' \
  | python3 -u $here/uart-follow.py
wait
EOF
~/rec-venv/bin/asciinema rec --overwrite --cols 100 --rows 17 --idle-time-limit 2 \
  -c "bash ~/recordings/show.sh" ~/recordings/"$name".cast
~/agg --font-size 16 ~/recordings/"$name".cast ~/recordings/"$name".gif
ls -la ~/recordings/"$name".cast ~/recordings/"$name".gif
