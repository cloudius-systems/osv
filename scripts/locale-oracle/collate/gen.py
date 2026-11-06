# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Random probe strings for xf: gen.py <seed> <n> [x|c] -> lines "x <hex>" or "c <hex> <hex>".
import random, sys
AL = ["a", "b", "c", "h", "C", "H", "A", "e", "E", "\u00e9", "\u00e8", "\u00ea", "o", "\u00f4", "-", " ", "'",
      "1", "2", "$", "\u0301", "\u0300", "\u0302", "\u4e00", "\u4e01", "L", "l", "\u00b7", "\u0430", "\u0410",
      "\u0627", "\u0653", "\u0e2f", "\u0e40", "\u0e01", "\x01", "\t", "\u00df", "\u00e6", "\u0131", "I", "i",
      "\u0130", "\u1100", "\uac00", "\u3042", "\u30a2", "\U0001f600", "\ufffd", "\u00a0", "\u2010", "\u00bd"]
BAD = [b"\x80", b"\xff", b"\xc2", b"\xe4\xb8", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xc0\xaf"]
def one(r, maxlen, bad):
    s = b""
    for _ in range(r.randrange(maxlen + 1)):
        if bad and r.random() < 0.08: s += r.choice(BAD)
        else: s += r.choice(AL).encode()
    return s
if __name__ == "__main__":
    r = random.Random(int(sys.argv[1])); n = int(sys.argv[2]); mode = sys.argv[3] if len(sys.argv) > 3 else "x"
    for _ in range(n):
        if mode == "x": print("x", one(r, 8, True).hex())
        else: print("c", one(r, 6, True).hex(), one(r, 6, True).hex())
