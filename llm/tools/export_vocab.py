#!/usr/bin/env python3
"""Write llm/weights/tok512.h: stories260K's tokenizer pieces, so the C demo can print text.

    tools/.venv/bin/python tools/export_vocab.py

Each piece is stored as raw bytes: byte-fallback tokens (<0xXX>) become that one byte. Token 1
(BOS) and 2 (EOS) print nothing. The leading space of the first piece after BOS is dropped, as in
Tokenizer.decode().
"""
from pathlib import Path

from stories_int8 import load

OUT = Path(__file__).resolve().parent.parent / "weights" / "tok512.h"


def piece_bytes(p):
    if p.startswith("<0x") and p.endswith(">"):
        return bytes([int(p[3:-1], 16)])
    if p.strip() in ("<s>", "</s>", "<unk>"):  # tok512 stores BOS and EOS as "\n<s>\n", "\n</s>\n"
        return b""
    return p.encode("utf-8")


def c_string(b):
    return '"' + "".join(chr(c) if 32 <= c < 127 and chr(c) not in '"\\?' else f"\\{c:03o}" for c in b) + '"'


def main():
    m, tok = load()
    pieces = [piece_bytes(p) for p in tok.pieces]
    blob, offsets = b"", []
    for p in pieces:
        offsets.append(len(blob))
        blob += p + b"\0"
    lines = [
        "/* stories260K's tok512 tokenizer pieces, from llm/tools/export_vocab.py. Generated: do not edit. */\n",
        "#ifndef TOK512_H\n#define TOK512_H\n\n",
        f"enum {{ TOK_VOCAB = {len(pieces)}, TOK_BOS = 1, TOK_EOS = 2 }};\n\n",
        "/* Piece i is the NUL-terminated string at tok_pieces + tok_offsets[i]. */\n",
        "static const unsigned short tok_offsets[TOK_VOCAB] = {\n",
    ]
    for i in range(0, len(offsets), 16):
        lines.append("  " + ", ".join(map(str, offsets[i:i + 16])) + ",\n")
    lines.append("};\n\nstatic const char tok_pieces[] =\n")
    for p in pieces:
        lines.append(f"  {c_string(p)} \"\\000\"\n")
    lines.append(";\n\n#endif\n")
    OUT.write_text("".join(lines))
    # Check: decoding the pieces back gives the tokenizer's own text.
    sample = tok.encode("Once upon a time, there was a little girl named Lily.")
    ours = b"".join(pieces[t] for t in sample[1:]).decode("utf-8")
    assert ours.lstrip(" ") == tok.decode(sample[1:]), (ours, tok.decode(sample[1:]))
    print(f"wrote {OUT.name}: {len(pieces)} pieces, {len(blob)} bytes")


if __name__ == "__main__":
    main()
