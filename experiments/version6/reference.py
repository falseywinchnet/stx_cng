"""Reference implementation of the phase-aware causal codec ("CNG Phi" research codec).
Exact integer arithmetic; the decoder recomputes every context from already-decoded samples only.
Encoder and decoder share one per-pixel routine; the only difference is whether residual symbols are
written to or read from the binary range coder.

Construction (fixed for every image; no encoder search):
  * raster order, planes per pixel: alpha first (if present), then colour.
  * per plane: MED integer prediction; residual r.
  * activity a = quantized sum over six causal neighbours of max_c |r_c| (error energy, slope-independent).
  * phase context: fixed least-squares plane extrapolation over the causal (2K+1)x(K+1) window of the
    *cleaned* plane (samples whose own |r|>=2 replaced by their prediction). The plane fit's prediction is a
    linear function of three integer window moments (sum S, sum dx*S, sum (-dy)*S); the three Q24 coefficients
    depend only on the boundary configuration (min(y,K), min(x,K), min(W-1-x,K)) and are tabulated offline
    (data/phase_moment_coefficients.json). Quantized offset from MED in 1/8 steps. Revision CNGPHI05 (V6): the
    repetition table is bounded and direct-mapped (REPEAT_RATIO slots per pixel, 29-bit fingerprints) so the codec
    streams with O(width) row state plus a fixed table; CNGPHI04 (research/phase_codec_phi04.py) kept the exact
    unbounded dictionary. Revision CNGPHI04: the
    previous per-tap Q16 weights (CNGPHI03, research/phase_codec_phi03.py) are superseded by this exact form,
    which a native decoder evaluates in O(1) per pixel with running moments.
  * repetition predictor: hash of the causal neighbourhood (L,U,UL,UR of all planes) -> last pixel seen after
    that neighbourhood, with a confidence 0..3. Confidence>=1 selects 'repeat mode'.
  * colour mode by activity: a<GATE -> RGB planes (digital-surface regime); else the published CNG modular
    mean/opponent lift (three 8-bit planes, bijective on RGB8) for the texture regime.
  * residuals are modular 8-bit everywhere; binarized: zero flag (fine context), sign (sign context),
    exp-Golomb magnitude with adaptive unary prefix bins (coarse context), the most significant suffix bit
    adaptive (coarse context, prefix length), remaining suffix bits bypass. Count-based adaptation rate.
  * An earlier variant with YCoCg-R and exact 9-bit chroma residuals (signature CNGPHI01) measured 7-10% larger
    on noise strips and equal on photographs; see docs/PHASE_AWARE_CAUSAL_CODING.md.
"""
import numpy as np, struct
from functools import lru_cache
K = 6; PB = 8; GATE = 5; CLEAN_T = 2; ACT_BINS = 8; MAXCONF = 3
ERR_BINS = [0, 1, 2, 3, 5, 9, 17, 33]; ABS_BINS = [0, 1, 2, 3, 5, 9, 17]
LZP_OFFS = ((0, 1), (1, 0), (1, 1), (1, -1)); ACT_OFFS = ((0, 1), (1, 0), (1, 1), (1, -1), (0, 2), (2, 0))
WIN = [(dy, dx) for dy in range(0, K + 1) for dx in range(-K, K + 1) if not (dy == 0 and dx >= 0)]   # dy = rows above
MAGIC = b"CNGPHI05"
# Bounded repetition table (CNGPHI05): direct-mapped slots, capacity = smallest power of two >= REPEAT_RATIO * pixels
# clamped to [2^6, 2^22]; a slot holds a 29-bit fingerprint of the neighbourhood key, the colour and a confidence.
# A fingerprint collision is a wrong prediction that the residual corrects; encoder and decoder agree exactly.
REPEAT_RATIO = 0.5; REPEAT_MIN_LOG = 6; REPEAT_MAX_LOG = 22
MASK64 = (1 << 64) - 1
def pack_key(components):
    """Sixteen (or twelve) 9-bit components, little-end first, seven per 64-bit word; -1 (outside) packs to 0x1FF."""
    words = [0, 0, 0]
    for index, value in enumerate(components): words[index // 7] |= (value & 0x1FF) << (9 * (index % 7))
    return words
def hash_words(words):
    h = 0x9E3779B97F4A7C15
    for w in words:
        h ^= w; h = (h * 0xBF58476D1CE4E5B9) & MASK64; h ^= h >> 29
    return h
def repeat_capacity(pixels):
    log = REPEAT_MIN_LOG
    while log < REPEAT_MAX_LOG and (1 << log) < REPEAT_RATIO * pixels: log += 1
    return 1 << log
class BoundedRepeats:
    """dict-like: get(key) -> (colour tuple, confidence) or None; table[key] = (colour tuple, confidence)."""
    def __init__(self, pixels):
        capacity = repeat_capacity(pixels); self.mask = capacity - 1
        self.fingerprint = [-1] * capacity; self.colour = [None] * capacity; self.confidence = [0] * capacity
    def get(self, key):
        h = hash_words(pack_key(key)); slot = h & self.mask; fp = h >> 35
        if self.fingerprint[slot] == fp: return (self.colour[slot], self.confidence[slot])
        return None
    def __setitem__(self, key, value):
        h = hash_words(pack_key(key)); slot = h & self.mask
        self.fingerprint[slot] = h >> 35; self.colour[slot] = value[0]; self.confidence[slot] = value[1]
MOMENT_BITS = 24
def qbin(v, bins):
    i = 0
    while i + 1 < len(bins) and v >= bins[i + 1]: i += 1
    return i
@lru_cache(maxsize=None)
def moment_coefficients(avail):
    """Q24 integer coefficients (c0, c1, c2) such that the least-squares plane prediction at the origin over the
    available offsets is (c0*sum S + c1*sum dx*S + c2*sum (-dy)*S) / 2^24, up to coefficient rounding."""
    if len(avail) == 0: return ()
    O = np.array(avail, float); X = np.stack([np.ones(len(O)), O[:, 1], -O[:, 0]], -1)
    g = np.array([1., 0., 0.]) @ np.linalg.pinv(X.T @ X, rcond=1e-9)
    return tuple(int(round(v * (1 << MOMENT_BITS))) for v in g)
def avail_offsets(y, x, W):
    return tuple((dy, dx) for dy, dx in WIN if y - dy >= 0 and 0 <= x + dx < W)
_COEFFICIENT_TABLE = None
def coefficient_table():
    """Shared Q24 moment-coefficient table (data/phase_moment_coefficients.json, written by
    research/generate_phase_tables.py), keyed by (min(y,K), min(x,K), min(W-1-x,K)); identical to
    native/phase/phase_tables.hpp so Python and C++ never diverge."""
    global _COEFFICIENT_TABLE
    if _COEFFICIENT_TABLE is None:
        import json, os
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "phase_moment_coefficients.json")
        raw = json.load(open(path))["coefficients"]
        _COEFFICIENT_TABLE = {tuple(int(v) for v in k.split(",")): tuple(c) for k, c in raw.items()}
    return _COEFFICIENT_TABLE
def med(L, U, UL):
    if UL >= max(L, U): return min(L, U)
    if UL <= min(L, U): return max(L, U)
    return L + U - UL
def zig(v): return 2 * v if v >= 0 else -2 * v - 1
def unzig(s): return s >> 1 if (s & 1) == 0 else -((s + 1) >> 1)
def wrap8(v): return ((v + 128) & 255) - 128
def rct_fwd(R, G, B):
    """Owner's modular mean/opponent lift (bijective on RGB8): planes Y, A, Bc, each wrapped to [-128,127]."""
    A = wrap8(R - G); T = wrap8(G + (A >> 1)); Bc = wrap8(B - T); Y = wrap8(T + ((Bc + (A & 1) + 1) // 3)); return Y, A, Bc
def rct_inv(Y, A, Bc):
    T = wrap8(Y - ((Bc + (A & 1) + 1) // 3)); G = wrap8(T - (A >> 1)); R = wrap8(G + A); B = wrap8(Bc + T)
    return R & 255, G & 255, B & 255
# ---------------------------------------------------------------- adaptive binary range coder (LZMA-style)
TOP = 1 << 24; PBITS = 16; PONE = 1 << PBITS; NCAP = 30
RECIP = [65536 // (n + 2) for n in range(NCAP + 1)]
class Model:
    __slots__ = ("p", "n")
    def __init__(self): self.p = PONE >> 1; self.n = 0
    def update(self, bit):
        target = PONE if bit == 0 else 0
        self.p += ((target - self.p) * RECIP[self.n]) >> 16
        if self.p < 64: self.p = 64
        elif self.p > PONE - 64: self.p = PONE - 64
        if self.n < NCAP: self.n += 1
class Encoder:
    def __init__(self):
        self.low = 0; self.range = 0xFFFFFFFF; self.cache = 0; self.cache_size = 1; self.out = bytearray(); self.models = {}
        self.costs = {}
    def model(self, key):
        m = self.models.get(key)
        if m is None: m = Model(); self.models[key] = m
        return m
    def shift_low(self):
        if self.low < 0xFF000000 or self.low >= (1 << 32):
            carry = self.low >> 32
            temp = self.cache
            while True:
                self.out.append((temp + carry) & 0xFF); temp = 0xFF
                self.cache_size -= 1
                if self.cache_size == 0: break
            self.cache = (self.low >> 24) & 0xFF
        self.cache_size += 1
        self.low = (self.low & 0x00FFFFFF) << 8
    def bit(self, key, b):
        m = self.model(key); bound = (self.range >> PBITS) * m.p
        import math
        pr = m.p / PONE if b == 0 else 1 - m.p / PONE
        self.costs[key[1]] = self.costs.get(key[1], 0.0) - math.log2(pr) / 8
        if b == 0: self.range = bound
        else: self.low += bound; self.range -= bound
        m.update(b)
        while self.range < TOP: self.range <<= 8; self.shift_low()
    def bypass(self, b):
        self.costs["bypass"] = self.costs.get("bypass", 0.0) + 0.125
        self.range >>= 1
        if b: self.low += self.range
        while self.range < TOP: self.range <<= 8; self.shift_low()
    def finish(self):
        for _ in range(5): self.shift_low()
        return bytes(self.out)
class Decoder:
    def __init__(self, data):
        self.data = data; self.pos = 0; self.range = 0xFFFFFFFF; self.code = 0; self.models = {}
        for _ in range(5): self.code = ((self.code << 8) | self.next()) & 0xFFFFFFFF
    def next(self):
        b = self.data[self.pos] if self.pos < len(self.data) else 0; self.pos += 1; return b
    def model(self, key):
        m = self.models.get(key)
        if m is None: m = Model(); self.models[key] = m
        return m
    def bit(self, key, b_unused=None):
        m = self.model(key); bound = (self.range >> PBITS) * m.p
        if self.code < bound: self.range = bound; b = 0
        else: self.code -= bound; self.range -= bound; b = 1
        m.update(b)
        while self.range < TOP: self.range <<= 8; self.code = ((self.code << 8) | self.next()) & 0xFFFFFFFF
        return b
    def bypass(self, b_unused=None):
        self.range >>= 1
        if self.code >= self.range: self.code -= self.range; b = 1
        else: b = 0
        while self.range < TOP: self.range <<= 8; self.code = ((self.code << 8) | self.next()) & 0xFFFFFFFF
        return b
# ---------------------------------------------------------------- residual binarization
def code_symbol(coder, decoding, sym, fine, coarse, sctx):
    """Encode (sym given) or decode (returns sym) a nonnegative zigzag symbol."""
    if decoding:
        if coder.bit(("z",) + fine) == 1: return 0
        s = coder.bit(("s",) + sctx)
        p = 0
        while p < 24 and coder.bit(("m",) + coarse + (min(p, 15),)) == 1: p += 1
        v = 1
        for i in range(p):
            if i == 0: v = (v << 1) | coder.bit(("h",) + coarse + (min(p, 15),))
            else: v = (v << 1) | coder.bypass()
        m = v - 1
        return 2 * m + 2 - s
    if sym == 0: coder.bit(("z",) + fine, 1); return 0
    coder.bit(("z",) + fine, 0)
    s = sym & 1; coder.bit(("s",) + sctx, s)
    m = (sym + 1) // 2 - 1; v = m + 1; p = v.bit_length() - 1
    for i in range(p): coder.bit(("m",) + coarse + (min(i, 15),), 1)
    coder.bit(("m",) + coarse + (min(p, 15),), 0)
    for i in range(p - 1, -1, -1):
        if i == p - 1: coder.bit(("h",) + coarse + (min(p, 15),), (v >> i) & 1)
        else: coder.bypass((v >> i) & 1)
    return sym
# ---------------------------------------------------------------- shared pixel pipeline
class State:
    def __init__(self, H, W, C):
        self.H, self.W, self.C = H, W, C
        self.img = np.zeros((H, W, C), np.int64)            # decoded samples
        self.absr = np.zeros((H, W), np.int64)             # max over colour planes of |MED residual| (RGB coordinates)
        self.S = np.zeros((C, H, W), np.int64)             # cleaned RGB(A) planes for phase estimation
        self.T = np.zeros((3, H, W), np.int64)             # YCoCg-R planes of decoded pixels
        self.ST = np.zeros((3, H, W), np.int64)            # cleaned transformed planes
        self.lzp = BoundedRepeats(H * W)
    def get(self, arr, y, x):
        if y < 0: return 0
        if x < 0: x = 0
        elif x >= self.W: x = self.W - 1
        return int(arr[y, x])
    def neigh(self, arr, y, x):
        """L,U,UL,UR with JPEG-LS style boundary rules (row above image = 0; left of image = above)."""
        U = int(arr[y - 1, x]) if y > 0 else 0
        UL = int(arr[y - 1, x - 1]) if (y > 0 and x > 0) else U
        UR = int(arr[y - 1, x + 1]) if (y > 0 and x + 1 < self.W) else U
        L = int(arr[y, x - 1]) if x > 0 else U
        return L, U, UL, UR
    def phase(self, Splane, pred, y, x, step=1, clip=2 * PB):
        """Quantized offset of the fixed-point plane extrapolation from the integer prediction; None if no taps."""
        t = min(y, K); l = min(x, K); r = min(self.W - 1 - x, K)
        if t == 0 and l == 0: return None
        c0, c1, c2 = coefficient_table()[(t, l, r)]
        m0 = m1 = m2 = 0                                          # sum S, sum dx*S, sum (-dy)*S over available taps
        for dy in range(0, t + 1):
            row = Splane[y - dy]
            lo = x - l; hi = x + r if dy > 0 else x - 1
            for xx in range(lo, hi + 1):
                v = int(row[xx]); m0 += v; m1 += (xx - x) * v; m2 -= dy * v
        acc = c0 * m0 + c1 * m1 + c2 * m2                         # Q24 prediction
        off = acc - (pred << MOMENT_BITS)
        d = (off * PB + (step << (MOMENT_BITS - 1))) // (step << MOMENT_BITS)   # floor division
        if d > clip: d = clip
        elif d < -clip: d = -clip
        return d
def process(st, coder, decoding, src=None):
    H, W, C = st.H, st.W, st.C
    has_alpha = (C == 4)
    for y in range(H):
        for x in range(W):
            # --- activity (error energy of causal neighbours)
            err = 0
            for dy, dx in ACT_OFFS:
                yy, xx = y - dy, x - dx
                if yy >= 0 and 0 <= xx < W: err += int(st.absr[yy, xx])
            act = qbin(err, ERR_BINS)
            # --- repetition predictor lookup
            key = []
            for dy, dx in LZP_OFFS:
                yy, xx = y - dy, x - dx
                if yy >= 0 and 0 <= xx < W: key.extend(int(v) for v in st.img[yy, xx])
                else: key.extend([-1] * C)
            key = tuple(key); ent = st.lzp.get(key)
            conf = ent[1] if ent is not None else 0
            rep = ent[0] if (ent is not None and conf >= 1) else None
            # --- MED predictions in RGB(A)
            preds = []
            for c in range(C):
                L, U, UL, UR = st.neigh(st.img[..., c], y, x); preds.append(med(L, U, UL))
            vals = [None] * C
            # --- alpha first
            actx = 0
            if has_alpha:
                c = 3
                if rep is not None:
                    fine = ("Arep", conf, act); coarse = ("Ar", conf, act)
                    if not decoding: sym = zig(wrap8(int(src[y, x, 3]) - rep[3]))
                    sym = code_symbol(coder, decoding, None if decoding else sym, fine, coarse, coarse)
                    vals[3] = (rep[3] + unzig(sym)) & 255
                else:
                    dq = st.phase(st.S[3], preds[3], y, x); dqk = (dq if dq is not None else 99)
                    sg = 0 if dq is None else (1 if dq > 0 else (-1 if dq < 0 else 0))
                    fine = ("A", act, dqk); coarse = ("Ac", act); sctx = ("As", act, sg)
                    if not decoding: sym = zig(wrap8(int(src[y, x, 3]) - preds[3]))
                    sym = code_symbol(coder, decoding, None if decoding else sym, fine, coarse, sctx)
                    vals[3] = (preds[3] + unzig(sym)) & 255
                rA = wrap8(vals[3] - preds[3]); actx = min(abs(rA), 2)
            # --- colour planes
            if rep is not None:
                for c in (1, 0, 2):
                    fine = ("Crep", conf, act, actx); coarse = ("Cr", conf, act)
                    if not decoding: sym = zig(wrap8(int(src[y, x, c]) - rep[c]))
                    sym = code_symbol(coder, decoding, None if decoding else sym, fine, coarse, coarse)
                    vals[c] = (rep[c] + unzig(sym)) & 255
            elif act < GATE:
                gq = None
                for c in (1, 0, 2):
                    dq = st.phase(st.S[c], preds[c], y, x); dqk = (dq if dq is not None else 99)
                    sg = 0 if dq is None else (1 if dq > 0 else (-1 if dq < 0 else 0))
                    if c == 1: fine = ("G", act, dqk, actx); coarse = ("Gc", act); sctx = ("Gs", act, sg)
                    else: fine = ("RB", act, dqk, actx, gq); coarse = ("RBc", act); sctx = ("RBs", act, sg, gq)
                    if not decoding: sym = zig(wrap8(int(src[y, x, c]) - preds[c]))
                    sym = code_symbol(coder, decoding, None if decoding else sym, fine, coarse, sctx)
                    vals[c] = (preds[c] + unzig(sym)) & 255
                    if c == 1: gq = max(-1, min(1, wrap8(vals[1] - preds[1])))
            else:
                tp = []; step = 1 + act // 2
                for t in range(3):
                    L, U, UL, UR = st.neigh(st.T[t], y, x); tp.append(med(L, U, UL))
                offs = [st.phase(st.ST[t], tp[t], y, x, step, 3) for t in range(3)]
                offk = [99 if o is None else o for o in offs]
                if not decoding: Yv, Cov, Cgv = rct_fwd(int(src[y, x, 0]), int(src[y, x, 1]), int(src[y, x, 2]))
                # Y: modular 8-bit residual
                fine = ("Y", act, offk[0], actx); coarse = ("Yc", act); sctx = ("Ys", act, offk[0])
                if not decoding: sym = zig(wrap8(Yv - tp[0]))
                sym = code_symbol(coder, decoding, None if decoding else sym, fine, coarse, sctx)
                Yd = wrap8(tp[0] + unzig(sym)); ry = qbin(abs(wrap8(Yd - tp[0])), ABS_BINS)
                # Co: exact signed residual
                fine = ("Co", act, ry, offk[1], actx); coarse = ("Coc", act, ry); sctx = ("Cos", act, ry, offk[1])
                if not decoding: sym = zig(wrap8(Cov - tp[1]))
                sym = code_symbol(coder, decoding, None if decoding else sym, fine, coarse, sctx)
                Cod = wrap8(tp[1] + unzig(sym)); rco = qbin(abs(wrap8(Cod - tp[1])), ABS_BINS)
                fine = ("Cg", act, ry, rco, offk[2], actx); coarse = ("Cgc", act, rco); sctx = ("Cgs", act, rco, offk[2])
                if not decoding: sym = zig(wrap8(Cgv - tp[2]))
                sym = code_symbol(coder, decoding, None if decoding else sym, fine, coarse, sctx)
                Cgd = wrap8(tp[2] + unzig(sym))
                R, G, B = rct_inv(Yd, Cod, Cgd); vals[0], vals[1], vals[2] = R, G, B
                if not decoding: assert (R, G, B) == (int(src[y, x, 0]), int(src[y, x, 1]), int(src[y, x, 2]))
            # --- commit sample and update state
            for c in range(C): st.img[y, x, c] = vals[c]
            mx = 0
            for c in range(C):
                r = wrap8(vals[c] - preds[c])
                if c < 3 and abs(r) > mx: mx = abs(r)
                st.S[c, y, x] = preds[c] if abs(r) >= CLEAN_T else vals[c]
            st.absr[y, x] = mx
            Yv, Cov, Cgv = rct_fwd(vals[0], vals[1], vals[2])
            for t, v in enumerate((Yv, Cov, Cgv)):
                L, U, UL, UR = st.neigh(st.T[t], y, x); pt = med(L, U, UL)
                st.T[t, y, x] = v; st.ST[t, y, x] = pt if abs(wrap8(v - pt)) >= CLEAN_T else v
            cur = tuple(vals)
            if ent is not None:
                if ent[0] == cur: st.lzp[key] = (cur, min(ent[1] + 1, MAXCONF))
                else: st.lzp[key] = (cur, 0)
            else: st.lzp[key] = (cur, 0)
def encode(img):
    img = np.asarray(img); H, W, C = img.shape
    st = State(H, W, C); enc = Encoder()
    process(st, enc, False, img.astype(np.int64))
    payload = enc.finish()
    encode.last_costs = enc.costs
    return MAGIC + struct.pack(">IIB", W, H, C) + payload
def decode(data):
    assert data[:8] == MAGIC
    W, H, C = struct.unpack(">IIB", data[8:17])
    st = State(H, W, C); dec = Decoder(data[17:])
    process(st, dec, True)
    return st.img.astype(np.uint8)
