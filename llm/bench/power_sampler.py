#!/usr/bin/env python3
"""Sample a Linux machine's power every 200 ms, until killed, as CSV on stdout:
    t        wall clock (s)
    gpu_w    the NVIDIA GPU's board power (nvidia-smi power.draw), empty without a GPU
    rapl_uj  the CPU package's energy counter (RAPL, microjoules), empty where the VM hides it
llm/tools/report.py turns the RAPL counter into watts between samples.
"""
import shutil
import subprocess
import sys
import time

RAPL = "/sys/class/powercap/intel-rapl:0/energy_uj"
PERIOD_MS = 200


def rapl():
    try:
        with open(RAPL) as f:
            return f.read().strip()
    except OSError:
        return ""


def main():
    out = sys.stdout
    out.write("t,gpu_w,rapl_uj\n")
    if shutil.which("nvidia-smi"):
        smi = subprocess.Popen(["nvidia-smi", "--query-gpu=power.draw", "--format=csv,noheader,nounits",
                                f"-lms={PERIOD_MS}"], stdout=subprocess.PIPE, text=True)
        for line in smi.stdout:  # one line per sample: time it on arrival
            out.write(f"{time.time():.3f},{line.strip()},{rapl()}\n")
            out.flush()
    else:
        while True:
            out.write(f"{time.time():.3f},,{rapl()}\n")
            out.flush()
            time.sleep(PERIOD_MS / 1e3)


if __name__ == "__main__":
    main()
