# The FPGA demo recording

`stories260k-fpga.gif` is a live recording of stories260K on the AWS F2 FPGA, with every matmul on Gemmini (FireSim, `FireSimLeanGemminiRocketConfig`). `stories260k-fpga.cast` is the same recording in asciinema's format: play it with `asciinema play`, or render it with [`agg`](https://github.com/asciinema/agg).

How it was made (`aws/manager/fpga-record.sh`, on the FireSim manager):

1. **Off camera:** the demo binary `llm-demo-firesim` is set up as a FireSim workload, and `firesim infrasetup` flashes the FPGA.
2. **On camera:** `firesim runworkload` starts the run. The chip's console on the F2 is followed live, and `aws/manager/uart-follow.py` filters it: it keeps the lines that name the loaded FPGA image and the program's output, and drops the driver's noise.
3. asciinema records the terminal, and pauses longer than 2 s (while the program loads) are shortened. Everything else plays in real time.

The demo prints through the chip's UART, which FireSim shows with little delay. `first-try-htif/` is the first attempt, which printed through HTIF (`printstr`), the host link the other tests use. Each HTIF write waits for the host, about 21M cycles or 0.7 s, so the words came about one per second, much slower than the chip computes them. The whole run then took 3.58G cycles, against 776M with the UART.

The run's full console log and FireSim's logs are in `llm/fpga/f2/results/demo/`.
