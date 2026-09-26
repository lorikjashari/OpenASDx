#!/usr/bin/env python3
"""Follow a FireSim uartlog live on stdin, and print only what matters for a recording.

Before the simulation starts: only the "AFI ..." lines (which FPGA image is loaded). After
"Commencing simulation.": the program's output, character by character as it arrives, without the
driver's "tsibridge_t::tick skipping tick" lines. Stops after the PASSED or FAILED line.
"""
import sys

NOISE = ("tsibridge_t::",)
inp, out = sys.stdin.buffer, sys.stdout.buffer


def emit(b):
    out.write(b)
    out.flush()


line = b""
line_printed = False  # the current line is already on screen
started = False
while True:
    c = inp.read(1)
    if not c:
        break
    if c == b"\r":
        continue
    if not started:
        if c != b"\n":
            line += c
            continue
        if line.startswith(b"AFI "):
            emit(line + b"\n")
        started = line.strip() == b"Commencing simulation."
        line = b""
        continue
    # Running: hold the start of a line while it could still be noise, else print as it comes.
    if c == b"\n":
        if not any(line.startswith(n.encode()) for n in NOISE):
            emit(b"" if line_printed else line)
            emit(b"\n")
        done = b"*** PASSED ***" in line or b"*** FAILED ***" in line
        line, line_printed = b"", False
        if done:
            break
        continue
    line += c
    if line_printed:
        emit(c)
    elif not any(n.encode().startswith(line) or line.startswith(n.encode()) for n in NOISE):
        emit(line)
        line_printed = True
