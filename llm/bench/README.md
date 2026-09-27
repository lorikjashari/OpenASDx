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

## Colab, or another Linux machine with an NVIDIA GPU

`linux-power.sh` runs the same benchmarks, plus PyTorch on CUDA, while `power_sampler.py` records every 200 ms:
- the GPU's board power, from `nvidia-smi`;
- the CPU package's energy counter (RAPL), where the VM exposes it. Colab's usually doesn't, and then there's no CPU power.

On Colab, the repo being private:
1. `llm/bench/colab-bundle.sh` packs `llm/` into `openasdx-llm.tar.gz`.
2. Open `colab.ipynb` in Colab (File → Upload notebook) with a GPU runtime, and run its cells. The first one asks for the tarball.
3. The last cell downloads `colab-GPU.zip`. Unzip it into `results/`.

Elsewhere: `llm/bench/linux-power.sh NAME` writes to `results/NAME/`.
