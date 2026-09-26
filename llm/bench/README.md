# Benchmarks on a laptop

The FPGA's stories260K run, compared with this Mac (#54). `REPORT.md`, "Against a laptop", has the results.

```shell
make -C llm llm-bench && llm/llm-bench 20                  # C int8 (the FPGA's code), one core, 20 s
llm/tools/.venv/bin/pip install torch                       # once
cd llm/tools && ../tools/.venv/bin/python bench_torch.py --check   # PyTorch against the numpy float reference
sudo llm/bench/mac-power.sh                                 # every benchmark, 20 s each, with powermetrics
```

`mac-power.sh` measures 15 s idle first, then runs each benchmark as your user while `powermetrics` samples the CPU and GPU power every 200 ms. It writes to `results/mac/`:
- `NAME.json`: the benchmark's result, including its timed window (`t_start`, `t_end`);
- `NAME.power.txt.gz`: the power samples.

`llm/tools/report.py` keeps only the samples inside each timed window.

For Colab or another machine, run the same benchmarks and add a results folder next to `mac/`.
