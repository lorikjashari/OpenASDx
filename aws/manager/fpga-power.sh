#!/usr/bin/env bash
# On the F2: sample the FPGA's core power (Vccint) once a second, until stopped.
#   setsid nohup bash ~/fpga-power.sh ~/power.csv &     then: pkill -f fpga-power.sh
# Each line: unix time, watts (fpga-describe-local-image -M, "Last measured").
out=${1:-$HOME/power.csv}
echo "time,watts" > "$out"
while true; do
  w=$(sudo timeout 5 fpga-describe-local-image -S 0 -M 2>/dev/null | awk '/Power consumption/{p=1} p && /Last measured/{print $3; exit}')
  echo "$(date +%s.%N | cut -c1-14),${w:-}" >> "$out"
  sleep 1
done
