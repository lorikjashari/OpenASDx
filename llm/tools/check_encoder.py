#!/usr/bin/env python3
"""Check the C encoder (src/tokenizer.c, via ./llm-encode) against Tokenizer.encode()."""
import subprocess
import sys
from pathlib import Path

from stories_int8 import CALIB_PROMPTS, EVAL_TEXT, GEN_PROMPTS, load

PROMPTS = GEN_PROMPTS + CALIB_PROMPTS + [
    EVAL_TEXT, "", "a", "The dog", "  two  spaces", "Tom said, \"Hi!\" 123 times.",
    "It's 3:45 pm; café, naïve, 日本, 🙂", "UPPER lower MiXeD", "tab\there", "new\nline",
]

m, tok = load()
exe = Path(__file__).resolve().parent.parent / "llm-encode"
out = subprocess.run([str(exe), *PROMPTS], capture_output=True, text=True, check=True).stdout.split("\n")
bad = 0
for p, line in zip(PROMPTS, out):
    want = tok.encode(p)
    got = [int(x) for x in line.split()]
    if got != want:
        bad += 1
        print(f"MISMATCH {p!r}\n  python {want}\n  c      {got}")
print(f"{len(PROMPTS) - bad} of {len(PROMPTS)} prompts encode the same in C and Python")
sys.exit(1 if bad else 0)
