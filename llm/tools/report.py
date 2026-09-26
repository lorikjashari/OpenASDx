"""Render REPORT.md from REPORT.md.in, with every number computed from the committed data.

    llm/tools/.venv/bin/pip install numpy matplotlib
    llm/tools/.venv/bin/python llm/tools/report.py

Sources:
  llm/fpga/f2/results/*.uartlog          console output of the FPGA runs (cycles, tokens, tests)
  llm/fpga/f2/results/sweep-summary.txt  one line per FPGA run of Gemmini's tests
  llm/fpga/f2/results/stories-gemmini.{size,symbols}.txt   the ELF's sections and symbols
  llm/weights/stories260k.h              the model's shape
  stories_int8.py                        quality of the int8 model (recomputed here, in numpy)

Writes REPORT.md, the charts in img/report/, and the summary between the report:summary markers
in README.md. {{name}} in the template is a value from values().
"""
import re
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stories_int8 as si  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / "llm/fpga/f2/results"
HEADER = ROOT / "llm/weights/stories260k.h"
OUT = ROOT / "img/report"
PEAK_MACS_PER_CYCLE = 16 * 16  # Gemmini Lean: a 16x16 array, one int8 MAC per PE per cycle
PROJECT_HZ = 1e9  # the projection column: the same cycle counts at a typical chip clock

# Why each failing Gemmini test fails: the features the Lean image leaves out (llm/fpga/f2/FIRESIM.md).
# The script stops if a test fails that is not listed here.
LEAN_LIMITS = {
    "matmul_ws": ["a bias matrix D"],
    "tiled_matmul_ws_full_C": ["int32 read-out"],
    "transpose": ["output-stationary dataflow"],
    "padded": ["output-stationary dataflow", "a bias matrix D"],
    "raw_hazard": ["output-stationary dataflow", "a bias matrix D"],
}

GEMMINI, CPU = "#1f77b4", "#d62728"


# ---------------------------------------------------------------- parsing

def read(name):
    return (RESULTS / name).read_text(errors="replace")


def num(pattern, text, cast=int):
    m = re.search(pattern, text)
    if not m:
        raise ValueError(f"not found: {pattern}")
    return cast(m.group(1))


def stories_run(log):
    s = read(log)
    r = {
        "positions": [int(x) for x in re.search(r"cycles per position:([ \d]+)", s).group(1).split()]
        if "cycles per position" in s else None,
        "tokens": [int(x) for x in re.search(r"tokens:([ \d]+)", s).group(1).split()],
        "same": num(r"(\d+) of \d+ generated tokens match", s),
        "expected": num(r"\d+ of (\d+) generated tokens match", s),
        "gemms": num(r"(\d+) GEMMs on the backend", s),
        "npos": num(r"GEMMs on the backend for (\d+) positions", s),
        "wall": num(r"Wallclock Time Elapsed: ([\d.]+) s", s, float),
        "mhz": num(r"Effective Target Frequency: ([\d.]+) MHz", s, float),
        "passed": "*** PASSED ***" in s and "all checks passed" in s,
        "compared": "ok   no GEMM output differs between backends" in s,
    }
    if r["positions"]:
        m = re.search(r"mean (\d+), min (\d+), max (\d+); in GEMM calls (\d+)%", s)
        r["mean"], r["min"], r["max"], r["gemm_pct"] = map(int, m.groups())
    return r


def header_dims():
    s = HEADER.read_text()
    return {k: int(v) for k, v in re.findall(r"ST_([A-Z_]+) = (\d+)", s)}


def elf():
    sections = {}
    for line in read("stories-gemmini.size.txt").splitlines():
        f = line.split()
        if len(f) >= 2 and f[1].isdigit():
            sections[f[0]] = int(f[1])
    symbols = {}
    for line in read("stories-gemmini.symbols.txt").splitlines():
        addr, size, kind, name = line.split()
        symbols[name] = int(size, 16)
    return sections, symbols


def gemmini_tests():
    """(name, passed) for every Gemmini test run on the FPGA."""
    runs = [("tiled_matmul_ws", "*** PASSED ***" in read("tiled_matmul_ws.uartlog"))]
    for line in read("sweep-summary.txt").splitlines():
        name = line.split()[0].removesuffix("-baremetal")
        if not name.startswith("llm-stories"):
            runs.append((name, "*** PASSED ***" in line))
    return runs


def quality():
    """Perplexity and top-1 agreement, as stories_int8.py main() computes them."""
    m, tok = si.load()
    scales = si.calibrate(m, tok, "max")
    ev = tok.encode(si.EVAL_TEXT)
    ref = si.forward(m, si.Gemm("float"), ev)
    out = {"eval_tokens": len(ev), "calib_prompts": len(si.CALIB_PROMPTS), "scales": len(scales)}
    for mode in ("float", "int32", "lean"):
        lg = ref if mode == "float" else si.forward(m, si.Gemm(mode, scales), ev)
        lg = lg - lg.max(axis=1, keepdims=True)
        logp = lg - np.log(np.exp(lg).sum(axis=1, keepdims=True))
        nll = -np.mean([logp[i, ev[i + 1]] for i in range(len(ev) - 1)])
        out[f"ppl_{mode}"] = float(np.exp(nll))
        out[f"top1_{mode}"] = float(np.mean(lg.argmax(axis=1)[:-1] == ref.argmax(axis=1)[:-1]) * 100)
    # Each array once: the classifier is the embedding table itself (a shared checkpoint).
    arrays = {id(a): a.size for a in vars(m).values() if isinstance(a, np.ndarray)}
    return out, tok, sum(arrays.values())


# ---------------------------------------------------------------- values

def values():
    d = header_dims()
    dim, hidden, layers, heads, kv_heads, head_dim, vocab, max_seq, prompt = (
        d["DIM"], d["HIDDEN"], d["LAYERS"], d["HEADS"], d["KV_HEADS"], d["HEAD_DIM"], d["VOCAB"],
        d["MAX_SEQ"], d["PROMPT_LEN"])
    kv_dim = kv_heads * head_dim

    g, c, chk = (stories_run(f) for f in
                 ("stories-gemmini.uartlog", "stories-cpu.uartlog", "stories260k.uartlog"))
    assert g["passed"] and c["passed"] and chk["passed"] and chk["compared"]
    assert g["tokens"] == c["tokens"] == chk["tokens"], "the three FPGA runs disagree on the tokens"
    gen_pos = range(prompt - 1, len(g["positions"]))
    ngen = len(gen_pos)

    # int8 MACs of one forward pass at position p: the fixed projections and the LM head, plus
    # the two attention GEMMs of every head over the p+1 cached positions.
    proj = dim * dim * 2 + dim * kv_dim * 2 + dim * hidden * 3
    fixed = layers * proj + dim * vocab
    attn_per_pos = layers * heads * head_dim * 2
    macs = np.mean([fixed + attn_per_pos * (p + 1) for p in gen_pos])
    # Parameters: the embedding table (shared with the classifier), the GEMM weights, the norms.
    params = vocab * dim + layers * (proj + 2 * dim) + dim
    gemms_per_token = layers * (7 + 2 * heads) + 1

    g_gemm, c_gemm = g["mean"] * g["gemm_pct"] / 100, c["mean"] * c["gemm_pct"] / 100
    hz = g["mhz"] * 1e6
    slope = lambda r: (r["positions"][-1] - r["positions"][prompt - 1]) / (len(r["positions"]) - prompt)

    q, tok, ckpt_params = quality()
    assert params == ckpt_params, f"parameters: {params} from the header, {ckpt_params} in the checkpoint"
    run_s = next(int(l.split()[1].rstrip("s")) for l in read("sweep-summary.txt").splitlines()
                 if l.startswith("llm-stories-gemmini-baremetal"))
    text = tok.decode(g["tokens"][1:])
    prompt_text = tok.decode(g["tokens"][1:prompt])

    sections, symbols = elf()
    weights = sum(symbols[k] for k in symbols if re.fullmatch(r"st_w(q|k|v|o|1|2|3|cls)", k))
    assert weights == fixed, "the int8 weights in the ELF should equal the fixed MACs"
    floats = sum(v for k, v in symbols.items() if k in ("st_tok_emb",) or k.startswith("st_rms"))
    kv_alloc = symbols["kc"] + symbols["vc"]
    kv_used = 2 * layers * g["npos"] * kv_dim * 4
    staging = sum(symbols[k] for k in ("pad_a", "pad_b", "acc", "o8_a", "o8_b", "o8_c") if k in symbols)
    code = sum(v for k, v in sections.items() if k.startswith(".text"))
    rodata = sum(v for k, v in sections.items() if k.startswith(".rodata"))
    bss = sections.get(".bss", 0) + sections.get(".sbss", 0)

    wm = read("tiled_matmul_ws.uartlog")
    tm_g, tm_c = [int(x) for x in re.findall(r"Cycles taken: (\d+)", wm)][:2]
    perf = read("tiled_matmul_ws_perf.uartlog")
    perf_cycles, perf_ideal = num(r"Cycles taken: (\d+)", perf), num(r"Ideal cycles: (\d+)", perf)
    perf_dims = re.search(r"I: (\d+), J: (\d+), K: (\d+)", perf).groups()
    conv = num(r"Gemmini conv took (\d+) cycles", read("conv_perf.uartlog"))
    conv_dw = num(r"Gemmini conv took (\d+) cycles", read("conv_dw_perf.uartlog"))

    tests = gemmini_tests()
    failed = [n for n, ok in tests if not ok]
    unexplained = [n for n in failed if n not in LEAN_LIMITS]
    assert not unexplained, f"failures without a known Lean limit: {unexplained}"
    by_limit = {}
    for n in failed:
        for lim in LEAN_LIMITS[n]:
            by_limit.setdefault(lim, []).append(n)

    util_perf = 100 * perf_ideal / perf_cycles
    util_llm = 100 * macs / g_gemm / PEAK_MACS_PER_CYCLE

    charts(g, c, prompt, g_gemm, c_gemm, hz, util_perf, util_llm, perf_dims)

    f = lambda x: f"{x:,.0f}"
    M = lambda x: f"{x / 1e6:.2f}M"
    v = {
        "prompt": prompt_text, "text": text, "params": f(params), "run_s": run_s,
        "layers": layers, "dim": dim, "hidden": hidden, "heads": heads, "kv_heads": kv_heads,
        "vocab": vocab, "max_seq": max_seq,
        "gemms": f(chk["gemms"]), "npos": chk["npos"], "ngen": ngen, "gemms_per_token": gemms_per_token,
        "same": chk["same"], "expected": chk["expected"],
        "scales": q["scales"], "calib_prompts": q["calib_prompts"], "eval_tokens": q["eval_tokens"],
        "ppl_float": f"{q['ppl_float']:.2f}", "ppl_int32": f"{q['ppl_int32']:.2f}",
        "ppl_lean": f"{q['ppl_lean']:.2f}",
        "ppl_cost": f"{q['ppl_lean'] - q['ppl_float']:.2f}",
        "top1_int32": f"{q['top1_int32']:.1f}", "top1_lean": f"{q['top1_lean']:.1f}",
        "g_cycles": f(g["mean"]), "c_cycles": f(c["mean"]), "g_cycles_m": M(g["mean"]),
        "c_cycles_m": M(c["mean"]),
        "g_pct": g["gemm_pct"], "c_pct": c["gemm_pct"], "g_rest_pct": 100 - g["gemm_pct"],
        "g_gemm_m": M(g_gemm), "c_gemm_m": M(c_gemm), "g_rest_m": M(g["mean"] - g_gemm),
        "g_tps": f"{hz / g['mean']:.1f}", "c_tps": f"{hz / c['mean']:.1f}",
        "g_tps_proj": f"{PROJECT_HZ / g['mean']:.0f}", "c_tps_proj": f"{PROJECT_HZ / c['mean']:.0f}",
        "proj_ghz": f"{PROJECT_HZ / 1e9:g}",
        "speedup": f"{c['mean'] / g['mean']:.1f}", "gemm_speedup": f"{c_gemm / g_gemm:.0f}",
        "g_slope_k": f"{slope(g) / 1e3:.0f}k", "c_slope_k": f"{slope(c) / 1e3:.0f}k",
        "mhz": f"{g['mhz']:.1f}", "wall": f"{g['wall']:.0f}",
        "macs": f(macs), "fixed_macs": f(fixed), "attn_per_pos": f(attn_per_pos),
        "mops": f"{2 * macs / 1e6:.2f}",
        "g_mac_cycle": f"{macs / g_gemm:.2f}", "c_mac_cycle": f"{macs / c_gemm:.3f}",
        "g_mmacs": f"{macs / g['mean'] * hz / 1e6:.1f}", "c_mmacs": f"{macs / c['mean'] * hz / 1e6:.1f}",
        "peak_gmacs": f"{PEAK_MACS_PER_CYCLE * hz / 1e9:.1f}",
        "per_call": f(g_gemm / gemms_per_token),
        "util_perf": f"{util_perf:.0f}", "util_llm": f"{util_llm:.1f}",
        "weights": f(weights), "weights_kb": f"{weights / 1000:.0f}", "floats": f(floats),
        "kv_alloc": f(kv_alloc), "kv_used": f(kv_used), "staging": f(staging), "code": f(code),
        "elf": f(sections["Total"]), "elf_mb": f"{sections['Total'] / 1e6:.1f}",
        "rodata": f(rodata), "bss": f(bss),
        "weight_mbs": f"{weights * hz / g['mean'] / 1e6:.1f}",
        "tm_g": f(tm_g), "tm_c": f(tm_c), "tm_x": f(tm_c / tm_g),
        "perf_dims": "×".join(perf_dims), "perf_cycles": f(perf_cycles), "perf_ideal": f(perf_ideal),
        "conv": f(conv), "conv_dw": f(conv_dw),
        "n_tests": len(tests), "n_pass": len(tests) - len(failed), "n_fail": len(failed),
        "limit_rows": "\n".join(f"| {lim} | {', '.join(f'`{n}`' for n in ns)} |" for lim, ns in by_limit.items()),
    }
    return v


# ---------------------------------------------------------------- charts

def charts(g, c, prompt, g_gemm, c_gemm, hz, util_perf, util_llm, perf_dims):
    OUT.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.size": 11, "axes.spines.top": False, "axes.spines.right": False})

    fig, ax = plt.subplots(figsize=(8, 4.2))
    xs = range(len(g["positions"]))
    ax.plot(xs, [x / 1e6 for x in c["positions"]], "o-", ms=3, color=CPU, label="Rocket core only")
    ax.plot(xs, [x / 1e6 for x in g["positions"]], "o-", ms=3, color=GEMMINI, label="Rocket + Gemmini")
    ax.axvspan(-0.5, prompt - 1.5, color="0.92")
    ax.text(prompt / 2 - 1, max(c["positions"]) / 1e6 * 0.97, "prompt", ha="center", color="0.4")
    ax.set_xlabel("position in the sequence")
    ax.set_ylabel("million cycles")
    ax.set_title("stories260K on the F2 FPGA: cycles per position")
    ax.set_ylim(0)
    ax.legend(frameon=False, loc="lower right")
    fig.tight_layout()
    fig.savefig(OUT / "cycles-per-position.png", dpi=150)

    fig, ax = plt.subplots(figsize=(7, 3.2))
    gemm = [c_gemm / 1e6, g_gemm / 1e6]
    rest = [(c["mean"] - c_gemm) / 1e6, (g["mean"] - g_gemm) / 1e6]
    ax.barh(["Rocket core only", "Rocket + Gemmini"], gemm, color=[CPU, GEMMINI])
    ax.barh(["Rocket core only", "Rocket + Gemmini"], rest, left=gemm, color="0.8")
    for i, (a, b) in enumerate(zip(gemm, rest)):
        ax.text(a + b + 0.1, i, f"{a + b:.2f}M", va="center")
        if a > 1.5:  # label inside a wide GEMM bar, beside a narrow one
            ax.text(a / 2, i, f"GEMMs {a:.2f}M", va="center", ha="center", color="white", fontsize=9)
        else:
            ax.text(a + 0.1, i, f"<- GEMMs {a:.2f}M", va="center", fontsize=9)
    ax.set_xlabel("million cycles per generated token (mean); grey: the rest, on the core")
    ax.set_title("Where a token's cycles go")
    ax.set_xlim(0, c["mean"] / 1e6 * 1.15)
    fig.tight_layout()
    fig.savefig(OUT / "cycles-breakdown.png", dpi=150)

    fig, axes = plt.subplots(1, 2, figsize=(8, 3.4))
    for ax, clock, title in ((axes[0], hz, f"measured, {hz / 1e6:.0f} MHz FPGA"),
                             (axes[1], PROJECT_HZ, f"same cycles at {PROJECT_HZ / 1e9:g} GHz (projection)")):
        vals = [clock / c["mean"], clock / g["mean"]]
        bars = ax.bar(["core only", "+ Gemmini"], vals, color=[CPU, GEMMINI])
        ax.bar_label(bars, fmt="%.0f" if clock > 1e8 else "%.1f")
        ax.set_title(title, fontsize=11)
        ax.set_ylabel("tokens / s")
        ax.set_ylim(0, max(vals) * 1.2)
    fig.suptitle("stories260K generation speed")
    fig.tight_layout()
    fig.savefig(OUT / "tokens-per-second.png", dpi=150)

    fig, ax = plt.subplots(figsize=(7, 3.0))
    bars = ax.barh([f"tiled_matmul_ws_perf\n{'x'.join(perf_dims)}", "stories260K GEMMs\n1 row each (decode)"],
                   [util_perf, util_llm], color=[GEMMINI, "#9ecae1"])
    ax.bar_label(bars, fmt="%.1f%%", padding=3)
    ax.set_xlim(0, 100)
    ax.set_xlabel(f"% of Gemmini's peak ({PEAK_MACS_PER_CYCLE} MACs per cycle)")
    ax.set_title("Gemmini utilization on the FPGA")
    fig.tight_layout()
    fig.savefig(OUT / "gemmini-utilization.png", dpi=150)
    plt.close("all")


def main():
    v = values()
    template = (ROOT / "REPORT.md.in").read_text()
    missing = sorted(set(re.findall(r"\{\{(\w+)\}\}", template)) - set(v))
    if missing:
        sys.exit(f"REPORT.md.in uses values report.py does not compute: {missing}")
    out = re.sub(r"\{\{(\w+)\}\}", lambda m: str(v[m.group(1)]), template)
    (ROOT / "REPORT.md").write_text(
        "<!-- Generated by llm/tools/report.py from REPORT.md.in: edit those, not this file. -->\n" + out)
    readme = ROOT / "README.md"
    summary = (f"- all {v['gemms']} GEMMs of a generation are bit-identical to the CPU reference;\n"
               f"- a token takes {v['g_cycles_m']} cycles with Gemmini, against {v['c_cycles_m']} on the "
               f"RISC-V core alone ({v['speedup']}× faster);\n"
               f"- that's {v['g_tps']} tokens/s at {v['mhz']} MHz, against {v['c_tps']};\n"
               f"- {v['n_pass']} of {v['n_tests']} of Gemmini's own tests pass on the FPGA, and the other "
               f"{v['n_fail']} need features the prebuilt image leaves out.\n")
    text, n = re.subn(r"(<!-- report:summary start[^>]*-->\n).*?(<!-- report:summary end -->)",
                      lambda m: m.group(1) + summary + m.group(2), readme.read_text(), flags=re.S)
    if n != 1:
        sys.exit("README.md: the report:summary markers are missing")
    readme.write_text(text)
    print(f"wrote REPORT.md, the README summary, and {len(list(OUT.glob('*.png')))} charts in "
          f"{OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
