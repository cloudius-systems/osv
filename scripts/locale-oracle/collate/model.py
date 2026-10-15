# Copyright (C) 2026 Greg Burd
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.
#
# Probe model of a compiled LC_COLLATE (learned from the bytes of localedef's files and the answers of
# glibc's strxfrm/strcoll; see STAGE1-FORMAT-SPEC.md section 6). Used to test hypotheses before
# collate.cc is written: model.py <LC_COLLATE> < xf-output compares the model's strxfrm with glibc's.
import struct, sys

class Coll:
    def __init__(s, path):
        b = s.b = open(path, "rb").read()
        s.n = struct.unpack_from("<I", b, 4)[0]
        s.idx = struct.unpack_from("<%dI" % s.n, b, 8)
        s.nrules = s.w(s.idx[0])
        s.rules = b[s.idx[1]:s.idx[1] + 0x10000][:0]  # filled below
        s.T, s.W, s.E, s.I = s.idx[2], s.idx[3], s.idx[4], s.idx[5]
        s.TW, s.WW, s.EW, s.IW = s.idx[9], s.idx[10], s.idx[11], s.idx[12]
        s.R = s.idx[1]
    def w(s, o): return struct.unpack_from("<I", s.b, o)[0]
    def i(s, o): return struct.unpack_from("<i", s.b, o)[0]
    def rule(s, idx, level): return s.b[s.R + (idx >> 24) * s.nrules + level]
    # ---- multibyte
    def mb_find(s, st, i):
        """(weight index, bytes consumed) for the element at st[i:]."""
        v = s.i(s.T + 4 * st[i])
        if v >= 0:
            return v, 1
        o = s.E - v
        while True:
            x = s.i(o); ln = s.b[o + 4]
            if x >= 0:
                if st[i + 1:i + 1 + ln] == s.b[o + 5:o + 5 + ln]:
                    return x, 1 + ln
                o += 5 + ln
            else:
                a = s.b[o + 5:o + 5 + ln]; z = s.b[o + 5 + ln:o + 5 + 2 * ln]; r = st[i + 1:i + 1 + ln]
                if len(r) == ln and r[:-1] == a[:-1] and a[-1] <= r[-1] <= z[-1]:
                    return s.i(s.I + 4 * (-x + r[-1] - a[-1])), 1 + ln
                o += 5 + 2 * ln
            o = (o + 3) & ~3
    def mb_weights(s, idx, level):
        o = s.W + (idx & 0xffffff)
        for L in range(level):
            o += 1 + s.b[o]
        return s.b[o + 1:o + 1 + s.b[o]]
    def mb_elems(s, st):
        out = []; i = 0
        while i < len(st):
            x, n = s.mb_find(st, i); out.append(x); i += n
        return out
    # ---- wide
    def wc_lookup(s, wc):
        t = s.TW
        s1, n1, s2, m2, m3 = [s.w(t + 4 * k) for k in range(5)]
        i1 = wc >> s1
        if i1 >= n1: return None
        o2 = s.w(t + 20 + 4 * i1)
        if not o2: return None
        o3 = s.w(t + o2 + 4 * ((wc >> s2) & m2))
        if not o3: return None
        return s.i(t + o3 + 4 * (wc & m3))
    def wc_find(s, ws, i):
        v = s.wc_lookup(ws[i])
        if v is None: v = 0
        if v >= 0: return v, 1
        o = s.EW + 4 * (-v)
        while True:
            x = s.i(o); ln = s.w(o + 4)
            seq = [s.w(o + 8 + 4 * k) for k in range(ln)]
            if x >= 0:
                if list(ws[i + 1:i + 1 + ln]) == seq: return x, 1 + ln
                o += 8 + 4 * ln
            else:
                z = [s.w(o + 8 + 4 * ln + 4 * k) for k in range(ln)]; r = list(ws[i + 1:i + 1 + ln])
                if len(r) == ln and r[:-1] == seq[:-1] and seq[-1] <= r[-1] <= z[-1]:
                    return s.i(s.IW + 4 * (-x + r[-1] - seq[-1])), 1 + ln
                o += 8 + 8 * ln
    def wc_weights(s, idx, level):
        o = s.WW + 4 * (idx & 0xffffff)
        for L in range(level):
            o += 4 + 4 * s.w(o)
        return [s.w(o + 4 + 4 * k) for k in range(s.w(o))]
    def wc_elems(s, ws):
        out = []; i = 0
        while i < len(ws):
            x, n = s.wc_find(ws, i); out.append(x); i += n
        return out

def utf8_pos(n):
    return bytes([n]) if n < 0x80 else chr(n).encode("utf-8", "surrogatepass")

def xfrm_order(c, e, L):
    """Element order and position flag strxfrm uses at level L (measured). The level is a position
    level if the first element's rule says so. Each maximal run of backward elements is emitted
    reversed; at a position level a run that reaches the end of a string of fewer than 4096 elements
    loses its last element."""
    pos = bool(c.rule(e[0], L) & 4)
    out = []; i = 0; n = len(e)
    while i < n:
        if c.rule(e[i], L) & 2:
            j = i
            while j < n and c.rule(e[j], L) & 2: j += 1
            run = list(range(j - 1, i - 1, -1))
            if pos and j == n and n < 4096: run = run[1:]
            out += run; i = j
        else:
            out.append(i); i += 1
    return out, pos

def xfrm(c, elems, weights, wide):
    """strxfrm/wcsxfrm units of a list of weight indices."""
    out = []
    if not elems:
        return out
    for L in range(c.nrules):
        if L:
            out += [1]
        order, pos = xfrm_order(c, elems, L)
        ign = 0; wrote = False
        for k in order:
            wt = weights(elems[k], L)
            if pos:
                if not wt:
                    ign += 1; continue
                out += [ign + 1] if wide else list(utf8_pos(ign + 1))
                ign = 0
            out += list(wt); wrote = wrote or bool(wt)
        # A last level that wrote nothing drops its separator, unless that is all there is.
        if L == c.nrules - 1 and L and not wrote and len(out) > 1:
            out = out[:-1]
    return out

def sign(x):
    return (x > 0) - (x < 0)

def coll_order(c, e, L, pos):
    """Element order strcoll uses at level L (measured): as strxfrm's, except that a backward run of
    two or more elements followed by a forward element skips the run's second-to-last element, and
    the position-level end-of-string drop does not happen. (Not modelled: a level whose position bit
    differs between sections, where glibc's strcoll is not antisymmetric; no shipped locale has it.)"""
    out = []; i = 0; n = len(e)
    while i < n:
        if c.rule(e[i], L) & 2:
            j = i
            while j < n and c.rule(e[j], L) & 2: j += 1
            run = list(range(j - 1, i - 1, -1))
            if j < n and len(run) >= 2: del run[1]
            out += run; i = j
        else:
            out.append(i); i += 1
    return out, pos

def coll_level(c, e, L, weights, wide, pos):
    out = []; ign = 0
    order, pos = coll_order(c, e, L, pos)
    for k in order:
        wt = weights(e[k], L)
        if pos:
            if not wt: ign += 1; continue
            out += [ign + 1] if wide else list(utf8_pos(ign + 1)); ign = 0
        out += list(wt)
    return out

def coll(c, ea, eb, weights, wide=False):
    # Measured: the empty string sorts before every other string, even one of ignorable characters
    # only (strxfrm agrees: such a string gives the 0x01 separator).
    if not ea or not eb: return sign(len(ea) - len(eb))
    for L in range(c.nrules):
        # Measured: both strings use the position rule of the first string's first element.
        pos = bool(c.rule(ea[0], L) & 4)
        a = coll_level(c, ea, L, weights, wide, pos); b = coll_level(c, eb, L, weights, wide, pos)
        if a != b: return sign((a > b) - (a < b))
    return 0

def check_signs(c, f):
    """signs.c output (or xf "c" lines): the model's strcoll/wcscoll sign against glibc's."""
    strs = []; bad = n = 0
    for line in open(f):
        p = line.rstrip("\n").split(" ")
        if p[0] == "s": strs.append(bytes.fromhex(p[1]) if len(p) > 1 else b""); continue
        wa = wb = None
        if p[0] == "c":
            a, b, s, w = bytes.fromhex(p[1]), bytes.fromhex(p[2]), int(p[3]), p[4]
            if w != "-": wa, wb = [[int(u, 16) for u in q.split(",") if u] for q in p[5:7]]
        else: a, b, s, w = strs[int(p[0])], strs[int(p[1])], int(p[2]), p[3]
        n += 1
        m = coll(c, c.mb_elems(a), c.mb_elems(b), c.mb_weights)
        ok = m == s
        if w != "-":
            if wa is None: wa = [ord(ch) for ch in a.decode()]; wb = [ord(ch) for ch in b.decode()]
            ok = ok and coll(c, c.wc_elems(wa), c.wc_elems(wb), c.wc_weights, True) == int(w)
        if not ok:
            bad += 1
            if bad < 10: print("DIFF glibc", s, w, "model", m, a.hex(), b.hex())
    print("model-vs-glibc strcoll: %d/%d differ" % (bad, n))

if __name__ == "__main__" and len(sys.argv) > 2:
    check_signs(Coll(sys.argv[1]), sys.argv[2])
elif __name__ == "__main__":
    c = Coll(sys.argv[1])
    bad = tot = 0
    for line in sys.stdin:
        if not line.startswith("x "): continue
        p = line[2:].rstrip("\n").split("|")
        st = bytes.fromhex(p[0]); tot += 1
        e = c.mb_elems(st)
        mine = bytes(xfrm(c, e, c.mb_weights, False)).hex()
        ok = mine == p[2]
        if p[3] != "-":
            ws = [int(u, 16) for u in p[5].split(",") if u]
            we = c.wc_elems(ws)
            wm = "".join("%x," % u for u in xfrm(c, we, c.wc_weights, True))
            ok = ok and wm == p[4] and int(p[3]) == len(xfrm(c, we, c.wc_weights, True))
        ok = ok and int(p[1]) == len(mine) // 2
        if not ok:
            bad += 1
            if bad < 15: print("DIFF", p[0], "glibc", p[2], p[4] if len(p) > 4 else "", "model", mine)
    print("model-vs-glibc strxfrm: %d/%d differ" % (bad, tot))
