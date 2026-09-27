#!/usr/bin/env python3
"""Benchmark stories260K in PyTorch, float32, with a KV cache: on the CPU, the Mac's GPU (MPS) or an
NVIDIA GPU (CUDA).

    tools/.venv/bin/python tools/bench_torch.py                  every configuration, one JSON line each
    tools/.venv/bin/python tools/bench_torch.py --check          tokens against the numpy float reference
    tools/.venv/bin/python tools/bench_torch.py --device mps --batch 64 --seconds 20   one, for power

The prompt and length are those of llm-stories: "Once upon a time", then 30 greedy tokens. A batch
runs that many sequences side by side, all with the same prompt; tokens/s counts every sequence.
"""
import argparse
import json
import math
import time

import numpy as np
import torch

import stories_int8 as si

PROMPT_LEN, NEW = 5, 30


class Torch260K:
    def __init__(self, m, device):
        t = lambda a: torch.tensor(np.ascontiguousarray(a), dtype=torch.float32, device=device)
        self.m, self.device = m, device
        self.emb, self.cls = t(m.tok_emb), t(m.wcls)
        self.rms_att, self.rms_ffn, self.rms_final = t(m.rms_att), t(m.rms_ffn), t(m.rms_final)
        self.wq, self.wk, self.wv, self.wo = t(m.wq), t(m.wk), t(m.wv), t(m.wo)
        self.w1, self.w2, self.w3 = t(m.w1), t(m.w2), t(m.w3)
        # RoPE as in stories_int8.rope(): pair i of the vector rotates at 1/10000^((2i mod head)/head).
        i = np.arange(0, m.dim, 2)
        freq = 1.0 / (10000.0 ** ((i % m.head) / m.head))
        ang = np.arange(m.seq_len)[:, None] * freq[None, :]
        self.cos, self.sin = t(np.cos(ang)), t(np.sin(ang))
        self.kv_mul = m.n_heads // m.n_kv

    def rope(self, x, pos):  # x [B, n], n = dim for q, kv_dim for k: pair j uses column j
        c, s = self.cos[pos, : x.shape[1] // 2], self.sin[pos, : x.shape[1] // 2]
        a, b = x[:, 0::2], x[:, 1::2]
        out = torch.empty_like(x)
        out[:, 0::2], out[:, 1::2] = a * c - b * s, a * s + b * c
        return out

    @staticmethod
    def rmsnorm(x, w):
        return x * torch.rsqrt((x * x).mean(-1, keepdim=True) + 1e-5) * w

    @torch.no_grad()
    def generate(self, prompt, batch, new):
        m = self.m
        L, H, KVH, hd = m.n_layers, m.n_heads, m.n_kv, m.head
        T = len(prompt) + new
        kc = torch.zeros(L, batch, T, KVH, hd, device=self.device)
        vc = torch.zeros_like(kc)
        tok = torch.full((batch,), prompt[0], dtype=torch.long, device=self.device)
        out = []
        for pos in range(T - 1):
            x = self.emb[tok]
            for l in range(L):
                h = self.rmsnorm(x, self.rms_att[l])
                q = self.rope(h @ self.wq[l].T, pos).view(batch, H, hd)
                k = self.rope(h @ self.wk[l].T, pos).view(batch, KVH, hd)
                kc[l, :, pos], vc[l, :, pos] = k, (h @ self.wv[l].T).view(batch, KVH, hd)
                K = kc[l, :, : pos + 1].repeat_interleave(self.kv_mul, dim=2)  # [B, t, H, hd]
                V = vc[l, :, : pos + 1].repeat_interleave(self.kv_mul, dim=2)
                att = torch.softmax(torch.einsum("bhd,bthd->bht", q, K) / math.sqrt(hd), dim=-1)
                ctx = torch.einsum("bht,bthd->bhd", att, V).reshape(batch, m.dim)
                x = x + ctx @ self.wo[l].T
                h = self.rmsnorm(x, self.rms_ffn[l])
                a1, a3 = h @ self.w1[l].T, h @ self.w3[l].T
                x = x + (torch.nn.functional.silu(a1) * a3) @ self.w2[l].T
            logits = self.rmsnorm(x, self.rms_final) @ self.cls.T
            nxt = logits.argmax(-1)
            if pos + 1 < len(prompt):  # still reading the prompt
                tok = torch.full_like(tok, prompt[pos + 1])
            else:  # this position produced a new token
                tok = nxt
                out.append(nxt)
        return torch.stack(out, 1)


def sync(device):
    if device == "mps":
        torch.mps.synchronize()
    elif device == "cuda":
        torch.cuda.synchronize()


def gpu_available(device):
    return torch.backends.mps.is_available() if device == "mps" else torch.cuda.is_available()


def run(model, device, prompt, batch, seconds, threads):
    model.generate(prompt, batch, NEW)  # warm up (kernels, caches)
    sync(device)
    t_start = time.time()  # wall clock, to match power samples
    gens, start = 0, time.perf_counter()
    while True:
        toks = model.generate(prompt, batch, NEW)
        sync(device)
        gens += 1
        wall = time.perf_counter() - start
        if (seconds and wall >= seconds) or (not seconds and gens >= 5):
            break
    n = gens * batch * toks.shape[1]
    first = [int(t) for t in toks[0].cpu()]
    return {"system": f"torch-{device}-float32", "threads": threads, "batch": batch, "generations": gens,
            "tokens": n, "wall_s": round(wall, 3), "tokens_per_s": round(n / wall, 1),
            "ms_per_step": round(1e3 * wall / (gens * toks.shape[1]), 3),
            "t_start": round(t_start, 3), "t_end": round(time.time(), 3), "first_tokens": prompt + first}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device", choices=["cpu", "mps", "cuda"])
    ap.add_argument("--batch", type=int)
    ap.add_argument("--threads", type=int)
    ap.add_argument("--seconds", type=float, default=0)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    m, tok = si.load()
    prompt = tok.encode("Once upon a time")
    assert len(prompt) == PROMPT_LEN

    if args.check:
        ref = si.generate(m, si.Gemm("float"), prompt, NEW)
        for device in ["cpu"] + [d for d in ("mps", "cuda") if gpu_available(d)]:
            got = [int(t) for t in Torch260K(m, device).generate(prompt, 1, NEW)[0].cpu()]
            same = next((i for i, (a, b) in enumerate(zip(got, ref[PROMPT_LEN:])) if a != b), NEW)
            print(json.dumps({"check": device, "same_as_numpy_float": same, "of": NEW,
                              "text": tok.decode(prompt[1:] + got)}))
        return

    gpus = [d for d in ("mps", "cuda") if gpu_available(d)]
    configs = [(args.device, args.batch, args.threads)] if args.device else [
        ("cpu", 1, 1), ("cpu", 1, torch.get_num_threads()), ("cpu", 64, torch.get_num_threads())] + [
        (d, b, 0) for d in gpus for b in (1, 64, 1024)]
    for device, batch, threads in configs:
        if device == "cpu":
            torch.set_num_threads(threads or torch.get_num_threads())
        print(json.dumps(run(Torch260K(m, device), device, prompt, batch or 1, args.seconds, threads or 0)),
              flush=True)


if __name__ == "__main__":
    main()
