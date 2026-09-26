#!/usr/bin/env python3
"""Float vs int8 quality of stories260K (llama2.c tinyllamas) under Gemmini's GEMM contracts.

Three ways to run every matmul:
  float  reference
  int32  int8 A (dynamic scale), int8 B (one scale per tensor), exact int32 accumulate,
         dequantized on the CPU: Gemmini's full config (acc_read_full_width)
  lean   as int32, but the result leaves Gemmini as int8 with a static output scale
         calibrated in advance: FireSimLeanGemminiRocketConfig, the prebuilt F2 image

Everything else (RMSNorm, RoPE, softmax, SiLU, residuals) stays in float, as on the Rocket core.

Needs numpy. Downloads the 1 MB checkpoint into llm/weights/stories260K/ on the first run.

  python stories_int8.py                     calibrate, then compare the three modes
  python stories_int8.py --calib p99.9       percentile calibration instead of max
"""

import argparse
import struct
import subprocess
from pathlib import Path

import numpy as np

WEIGHTS = Path(__file__).resolve().parent.parent / "weights" / "stories260K"
URL = "https://huggingface.co/karpathy/tinyllamas/resolve/main/stories260K/"
FILES = ("stories260K.bin", "tok512.bin")   # MIT licensed, from karpathy/llama2.c

# Calibration and evaluation texts are separate, so the scales are not fitted to the test.
CALIB_PROMPTS = [
    "Once upon a time, there was a little girl named Lily.",
    "One day, a boy named Tom went to the park with his mom.",
    "The cat was very hungry. It looked for food in the kitchen.",
    "Sam had a big red ball. He liked to play with it every day.",
]
EVAL_TEXT = (
    "Once upon a time, there was a small dog named Max. Max liked to run in the garden. "
    "One day, Max saw a big bird on the tree. He wanted to play with the bird, but the bird "
    "flew away. Max was sad. Then his friend Lucy came and gave him a ball. Max was happy again, "
    "and they played together all day."
)
GEN_PROMPTS = ["Once upon a time", "The little bird"]


# ---------------------------------------------------------------- checkpoint and tokenizer

class Model:
    def __init__(self, path):
        raw = path.read_bytes()
        dim, hidden, n_layers, n_heads, n_kv, vocab, seq_len = struct.unpack("7i", raw[:28])
        self.shared = vocab > 0
        vocab = abs(vocab)
        self.dim, self.hidden, self.n_layers = dim, hidden, n_layers
        self.n_heads, self.n_kv, self.vocab, self.seq_len = n_heads, n_kv, vocab, seq_len
        self.head = dim // n_heads
        kv_dim = self.head * n_kv
        w = np.frombuffer(raw[28:], dtype=np.float32)
        off = 0

        def take(*shape):
            nonlocal off
            n = int(np.prod(shape))
            a = w[off:off + n].reshape(shape)
            off += n
            return a

        self.tok_emb = take(vocab, dim)
        self.rms_att = take(n_layers, dim)
        self.wq = take(n_layers, dim, dim)          # [out, in], as llama2.c stores them
        self.wk = take(n_layers, kv_dim, dim)
        self.wv = take(n_layers, kv_dim, dim)
        self.wo = take(n_layers, dim, dim)
        self.rms_ffn = take(n_layers, dim)
        self.w1 = take(n_layers, hidden, dim)
        self.w2 = take(n_layers, dim, hidden)
        self.w3 = take(n_layers, hidden, dim)
        self.rms_final = take(dim)
        take(seq_len, self.head // 2)                # freq_cis_real, unused
        take(seq_len, self.head // 2)                # freq_cis_imag, unused
        self.wcls = self.tok_emb if self.shared else take(vocab, dim)


class Tokenizer:
    def __init__(self, path, vocab):
        raw = path.read_bytes()
        off = 4                                      # max_token_length
        self.pieces, self.scores = [], []
        for _ in range(vocab):
            score, n = struct.unpack_from("fi", raw, off)
            off += 8
            self.pieces.append(raw[off:off + n].decode("utf-8", errors="replace"))
            off += n
            self.scores.append(score)
        self.index = {p: i for i, p in enumerate(self.pieces)}

    def encode(self, text):
        """llama2.c's BPE: start from characters, repeatedly merge the best-scoring pair."""
        text = " " + text                            # sentencepiece's dummy prefix
        toks = []
        for ch in text:
            if ch in self.index:
                toks.append(self.index[ch])
            else:                                    # byte fallback, <0xXX> tokens start at 3
                toks.extend(b + 3 for b in ch.encode("utf-8"))
        while True:
            best, best_i = -1e10, -1
            for i in range(len(toks) - 1):
                j = self.index.get(self.pieces[toks[i]] + self.pieces[toks[i + 1]])
                if j is not None and self.scores[j] > best:
                    best, best_i, best_tok = self.scores[j], i, j
            if best_i < 0:
                return [1] + toks                    # BOS
            toks[best_i:best_i + 2] = [best_tok]

    def decode(self, toks):
        out = []
        for prev, t in zip([1] + toks[:-1], toks):
            p = self.pieces[t]
            if prev == 1 and p.startswith(" "):
                p = p[1:]
            if p.startswith("<0x") and p.endswith(">"):
                p = chr(int(p[3:-1], 16))
            out.append(p)
        return "".join(out)


# ---------------------------------------------------------------- GEMM contracts

def q8(x):
    """Symmetric int8 with one scale for the whole tensor."""
    s = float(np.max(np.abs(x))) / 127.0 or 1.0
    return np.clip(np.rint(x / s), -127, 127).astype(np.int64), s


class Gemm:
    """y = A @ B.T for A [m, k] and B [n, k], under one of the three contracts."""

    def __init__(self, mode, out_scales=None, record=None):
        self.mode, self.out_scales, self.record = mode, out_scales, record
        self.wq_cache = {}

    def __call__(self, name, a, b, b_is_weight):
        if self.record is not None:                  # calibration: collect float outputs
            y = a @ b.T
            self.record.setdefault(name, []).append(np.abs(y).ravel())
            return y
        if self.mode == "float":
            return a @ b.T
        aq, a_s = q8(a)
        if b_is_weight:                              # weights are quantized once
            key = (name, id(b))
            if key not in self.wq_cache:
                self.wq_cache[key] = q8(b)
            bq, b_s = self.wq_cache[key]
        else:                                        # KV cache: activations, dynamic scale
            bq, b_s = q8(b)
        acc = aq @ bq.T                              # exact int32 (int64 here, no overflow)
        if self.mode == "int32":
            return acc * (a_s * b_s)
        s_o = self.out_scales[name]                  # lean: int8 out with a static scale
        out8 = np.clip(np.rint(acc * (a_s * b_s / s_o)), -128, 127)
        return out8 * s_o


# ---------------------------------------------------------------- the model

def rmsnorm(x, w):
    return x / np.sqrt(np.mean(x * x) + 1e-5) * w


def rope(x, pos, head):
    x = x.copy()
    for i in range(0, x.size, 2):
        freq = 1.0 / (10000.0 ** ((i % head) / head))
        c, s = np.cos(pos * freq), np.sin(pos * freq)
        x[i], x[i + 1] = x[i] * c - x[i + 1] * s, x[i] * s + x[i + 1] * c
    return x


def forward(m, gemm, tokens):
    """Logits after each token, one token at a time with a KV cache, like llm/src/model.c."""
    kv_mul = m.n_heads // m.n_kv
    kc = [[] for _ in range(m.n_layers)]
    vc = [[] for _ in range(m.n_layers)]
    all_logits = []
    for pos, tok in enumerate(tokens):
        x = m.tok_emb[tok].astype(np.float64)
        for l in range(m.n_layers):
            h = rmsnorm(x, m.rms_att[l])[None, :]
            q = gemm(f"L{l}.q", h, m.wq[l], True)[0]
            k = gemm(f"L{l}.k", h, m.wk[l], True)[0]
            v = gemm(f"L{l}.v", h, m.wv[l], True)[0]
            q, k = rope(q, pos, m.head), rope(k, pos, m.head)
            kc[l].append(k)
            vc[l].append(v)
            K, V = np.array(kc[l]), np.array(vc[l])
            ctx = np.empty(m.dim)
            for hd in range(m.n_heads):
                g = hd // kv_mul
                qh = q[hd * m.head:(hd + 1) * m.head][None, :]
                Kh = K[:, g * m.head:(g + 1) * m.head]
                Vh = V[:, g * m.head:(g + 1) * m.head]
                sc = gemm(f"L{l}.qk", qh, Kh, False)[0] / np.sqrt(m.head)
                p = np.exp(sc - sc.max())
                p /= p.sum()
                ctx[hd * m.head:(hd + 1) * m.head] = gemm(f"L{l}.av", p[None, :], Vh.T, False)[0]
            x = x + gemm(f"L{l}.o", ctx[None, :], m.wo[l], True)[0]
            h = rmsnorm(x, m.rms_ffn[l])[None, :]
            a1 = gemm(f"L{l}.w1", h, m.w1[l], True)[0]
            a3 = gemm(f"L{l}.w3", h, m.w3[l], True)[0]
            x = x + gemm(f"L{l}.w2", ((a1 / (1 + np.exp(-a1))) * a3)[None, :], m.w2[l], True)[0]
        x_f = rmsnorm(x, m.rms_final)[None, :]
        all_logits.append(gemm("lm_head", x_f, m.wcls, True)[0])
    return np.array(all_logits)


def generate(m, gemm, prompt_toks, n):
    toks = list(prompt_toks)
    for _ in range(n):
        toks.append(int(np.argmax(forward(m, gemm, toks)[-1])))
    return toks


# ---------------------------------------------------------------- main

def load():
    """The model and tokenizer, downloaded on first use."""
    WEIGHTS.mkdir(parents=True, exist_ok=True)
    for f in FILES:
        if not (WEIGHTS / f).exists():
            print(f"downloading {f}")
            subprocess.run(["curl", "-fsSL", "-o", str(WEIGHTS / f), URL + f], check=True)
    m = Model(WEIGHTS / "stories260K.bin")
    return m, Tokenizer(WEIGHTS / "tok512.bin", m.vocab)


def calibrate(m, tok, how="max"):
    """One static output scale per GEMM, from a float pass over CALIB_PROMPTS."""
    rec = {}
    for p in CALIB_PROMPTS:
        forward(m, Gemm("float", record=rec), tok.encode(p))
    if how == "max":
        return {k: float(np.max(np.concatenate(v))) / 127.0 for k, v in rec.items()}
    pct = float(how.lstrip("p"))
    return {k: float(np.percentile(np.concatenate(v), pct)) / 127.0 for k, v in rec.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--calib", default="max", help="'max' or a percentile such as 'p99.9'")
    ap.add_argument("--gen", type=int, default=40, help="tokens to generate per prompt")
    args = ap.parse_args()

    m, tok = load()
    print(f"stories260K: dim {m.dim}, hidden {m.hidden}, layers {m.n_layers}, heads {m.n_heads}/{m.n_kv}, "
          f"vocab {m.vocab}, shared classifier {m.shared}")
    scales = calibrate(m, tok, args.calib)
    print(f"calibrated {len(scales)} static output scales ({args.calib}) on {len(CALIB_PROMPTS)} prompts")

    ev = tok.encode(EVAL_TEXT)
    ref = forward(m, Gemm("float"), ev)
    ref_top = ref.argmax(axis=1)
    print(f"\nevaluation text: {len(ev)} tokens, next-token prediction with teacher forcing")
    print(f"{'mode':<7} {'perplexity':>10} {'top-1 = float':>14}")
    for mode in ("float", "int32", "lean"):
        lg = ref if mode == "float" else forward(m, Gemm(mode, scales), ev)
        lg = lg - lg.max(axis=1, keepdims=True)
        logp = lg - np.log(np.exp(lg).sum(axis=1, keepdims=True))
        nll = -np.mean([logp[i, ev[i + 1]] for i in range(len(ev) - 1)])
        agree = np.mean(lg.argmax(axis=1)[:-1] == ref_top[:-1])
        print(f"{mode:<7} {np.exp(nll):>10.2f} {agree * 100:>13.1f}%")

    for p in GEN_PROMPTS:
        print(f"\nprompt: {p!r}, greedy, {args.gen} tokens")
        for mode in ("float", "int32", "lean"):
            out = generate(m, Gemm(mode, scales), tok.encode(p), args.gen)
            print(f"  {mode:<6} {tok.decode(out[1:])!r}")


if __name__ == "__main__":
    main()
