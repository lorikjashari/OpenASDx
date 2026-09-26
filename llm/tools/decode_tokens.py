#!/usr/bin/env python3
"""Decode token ids from llm-stories into text, with stories260K's tok512 tokenizer.

  ./llm-stories | tools/.venv/bin/python tools/decode_tokens.py
  tools/.venv/bin/python tools/decode_tokens.py 1 403 407 261 378
"""

import sys

from stories_int8 import load

m, tok = load()
if len(sys.argv) > 1:
    ids = [int(t) for t in sys.argv[1:]]
else:
    line = next((l for l in sys.stdin if l.startswith("tokens:")), "")
    ids = [int(t) for t in line.split()[1:]]
print(tok.decode(ids[1:] if ids and ids[0] == 1 else ids))
