"""Tiled context-coded research reference with a match stage (stream CNGM0001).

The construction of research/tiled_context_codec.py (MED, colour gate, causal contexts, per-image static rANS tables
stored twice, independent 128x128 tiles, mosaic, duplicated index) with zstd's other half restored: matches.

A match copies the MED residuals of every channel of L pixels from d pixels back in the tile's raster order (copies
may overlap, so a short distance repeats a pattern); the decoder adds them to its own MED predictions. Copying
residuals rather than samples covers exact repeats and repeated rounding patterns alike. Matches stay inside the
tile, so tiles remain independent.

Signalling, as WebP does: each green context's alphabet is the 256 green literal symbols followed by 30 length
codes. A length symbol starts a match of MINLEN + v pixels, v prefix-coded (codes 0..3 are v itself; code c >= 4 has
h = c >> 1, v = ((2 | c & 1) << (h - 1)) + h - 1 raw extra bits). A distance symbol follows from its own table:
symbols 0..5 are the offsets 1, w, w - 1, w + 1, 2, 2w (left, up, up-left, up-right, two left, two up; w = tile
width), symbol 6 + c is the prefix code c of d - 1. Extra bits are coded in the same rANS stream as uniform symbols.
After a match the decoder copies for L pixels without decoding; otherwise it decodes green, red, blue and alpha
literals with their contexts exactly as the context-coded reference does.

Encoder parse (an encoder choice, not part of the format): greedy, at every pixel the longest match among the six
special offsets and the 16 most recent positions with the same four-pixel residual key; taken when L >= MINLEN.

Container: as CNGC0001 with signature "CNGM0001" and 38 tables (the 37 literal contexts with the length codes in
the five green ones, then the distance table). Exact, allocating Python reference; sizes and correctness only."""
import struct
import numpy as np
from research.tiled_zstd_codec import (CELL, EDGES, GATE, TILE, Zstd, block_means, crc, median_edge, neighbours,
                                       tile_grid, wrap)
from research.tiled_context_codec import (ALPHA0, BLUE0, LOWER, PRECISION, RED0, TOTAL, normalize, quantize,
                                          unzig)

MAGIC = b"CNGM0001"
MINLEN = 4; MAXLEN = 16384; NCODES = 30; CHAIN = 16; NSPECIAL = 6
GREEN_SYMBOLS = 256 + NCODES
DISTANCE = ALPHA0 + 2
CONTEXTS = DISTANCE + 1


def specials(w):
    return (1, w, w - 1, w + 1, 2, 2 * w)


def prefix(v):
    if v < 4: return v, 0, 0
    h = v.bit_length() - 1; bits = h - 1
    return 2 * h + ((v >> bits) & 1), bits, v & ((1 << bits) - 1)


def code_bits(code):
    return 0 if code < 4 else (code >> 1) - 1


def unprefix(code, extra):
    if code < 4: return code
    h = code >> 1
    return ((2 | (code & 1)) << (h - 1)) | extra


# ------------------------------------------------------------------ encoder: residuals, contexts, parse
def analyse(tile):
    s = tile.astype(np.int32); h, w, C = s.shape
    L, U, UL, UR = neighbours(s)
    med = median_edge(L, U, UL)
    r = wrap(s - med)
    e = np.abs(r[:, :, :3]).max(axis=2)
    E = np.zeros((h + 2, w + 4), np.int32); E[2:, 2:w + 2] = e
    A = E[2:, 1:w + 1] + E[2:, 0:w] + E[1:h + 1, 2:w + 2] + E[0:h, 2:w + 2] + E[1:h + 1, 1:w + 1] + E[1:h + 1, 3:w + 3]
    cls = sum((A > edge).astype(np.int32) for edge in EDGES)
    quiet = ((L[:, :, 3] == U[:, :, 3]) & (U[:, :, 3] == UL[:, :, 3])) if C == 4 else np.zeros((h, w), bool)
    return r.reshape(-1, C), cls.reshape(-1), quiet.reshape(-1)


def tile_ops(tile):
    """The coding operations of one tile: ('s', context, symbol) for table symbols, ('b', bits, value) for extra bits."""
    h, w, C = tile.shape; n = h * w
    r, cls, quiet = analyse(tile)
    packed = (r + 128).astype(np.int64) @ (np.int64(256) ** np.arange(C, dtype=np.int64))
    P = packed.tolist(); R = r.tolist(); K = cls.tolist(); Q = quiet.tolist()
    special = specials(w); heads = {}
    ops = []; matches = covered = 0

    def insert(j):
        if j + 4 <= n:
            key = (P[j], P[j + 1], P[j + 2], P[j + 3]); chain = heads.get(key)
            if chain is None: heads[key] = [j]
            else:
                chain.append(j)
                if len(chain) > 2 * CHAIN: del chain[:CHAIN]

    i = 0
    while i < n:
        best_length = 0; best_d = 0; limit = min(MAXLEN, n - i)
        candidates = [d for d in special if 1 <= d <= i]
        if i + 4 <= n:
            chain = heads.get((P[i], P[i + 1], P[i + 2], P[i + 3]))
            if chain: candidates += [i - j for j in reversed(chain[-CHAIN:])]
        for d in candidates:
            if P[i] != P[i - d]: continue
            length = 1
            while length < limit and P[i + length] == P[i + length - d]: length += 1
            if length > best_length: best_length, best_d = length, d
            if length == limit: break
        if best_length >= MINLEN:
            code, bits, extra = prefix(best_length - MINLEN)
            ops.append(("s", K[i], 256 + code))
            if bits: ops.append(("b", bits, extra))
            if best_d in special: ops.append(("s", DISTANCE, special.index(best_d)))
            else:
                code, bits, extra = prefix(best_d - 1)
                ops.append(("s", DISTANCE, NSPECIAL + code))
                if bits: ops.append(("b", bits, extra))
            for j in range(i, i + best_length): insert(j)
            matches += 1; covered += best_length; i += best_length
            continue
        k = K[i]; g = R[i][1]
        red, blue = R[i][0], R[i][2]
        if k >= GATE: red = wrap(red - g); blue = wrap(blue - g)
        gb = 0 if g == 0 else (1 if -2 <= g <= 2 else 2)
        ops.append(("s", k, 2 * g if g >= 0 else -2 * g - 1))
        ops.append(("s", RED0 + 3 * k + gb, 2 * red if red >= 0 else -2 * red - 1))
        ops.append(("s", BLUE0 + 3 * k + gb, 2 * blue if blue >= 0 else -2 * blue - 1))
        if C == 4:
            a = R[i][3]; ops.append(("s", ALPHA0 + (0 if Q[i] else 1), 2 * a if a >= 0 else -2 * a - 1))
        insert(i); i += 1
    return ops, matches, covered


# ------------------------------------------------------------------ tables and rANS
class Table:
    def __init__(self, freq):
        self.freq = freq; self.cum = []; acc = 0
        for v in freq: self.cum.append(acc); acc += v
        self.slot = None
        if freq:
            self.slot = [0] * TOTAL
            for i, v in enumerate(freq):
                for j in range(self.cum[i], self.cum[i] + v): self.slot[j] = i


def serialize_tables(qs, z):
    return z.compress(b"".join(struct.pack(">H", len(q)) + q for q in qs))


def parse_tables(blob, z):
    body = z.decompress(blob, CONTEXTS * (2 + GREEN_SYMBOLS)); pos = 0; qs = []
    for _ in range(CONTEXTS):
        if pos + 2 > len(body): raise ValueError("tables")
        n = struct.unpack(">H", body[pos:pos + 2])[0]; pos += 2
        if n > GREEN_SYMBOLS or pos + n > len(body): raise ValueError("tables")
        qs.append(body[pos:pos + n]); pos += n
    if pos != len(body): raise ValueError("tables")
    return qs


def rans_encode(ops, tables):
    x = LOWER; out = bytearray()
    for kind, a, b in reversed(ops):
        if kind == "s": t = tables[a]; f = t.freq[b]; start = t.cum[b]
        else: f = TOTAL >> a; start = b * f
        limit = ((LOWER >> PRECISION) << 8) * f
        while x >= limit: out.append(x & 255); x >>= 8
        x = ((x // f) << PRECISION) + (x % f) + start
    out.reverse()
    return struct.pack(">I", x) + bytes(out)


class Decoder:
    def __init__(self, data):
        if len(data) < 4: raise ValueError("rANS stream")
        self.x = struct.unpack(">I", data[:4])[0]; self.data = data; self.pos = 4

    def _renorm(self, x):
        while x < LOWER:
            if self.pos >= len(self.data): raise ValueError("rANS exhausted")
            x = (x << 8) | self.data[self.pos]; self.pos += 1
        self.x = x

    def decode(self, t):
        if t.slot is None: raise ValueError("unused context")
        x = self.x; m = x & (TOTAL - 1); s = t.slot[m]
        self._renorm(t.freq[s] * (x >> PRECISION) + m - t.cum[s])
        return s

    def bits(self, k):
        if k == 0: return 0
        x = self.x; m = x & (TOTAL - 1); f = TOTAL >> k; v = m // f
        self._renorm(f * (x >> PRECISION) + m - v * f)
        return v


# ------------------------------------------------------------------ tile decode (sequential)
def decode_tile(payload, w, h, C, tables):
    d = Decoder(payload); n = w * h
    s = [[[0] * w for _ in range(h)] for _ in range(C)]
    R = [[0] * n for _ in range(C)]
    e = [[0] * w for _ in range(h)]
    special = specials(w); copy_left = 0; copy_d = 0
    for y in range(h):
        ey = e[y]; ey1 = e[y - 1] if y >= 1 else None; ey2 = e[y - 2] if y >= 2 else None
        for x in range(w):
            i = y * w + x
            if copy_left:
                res = [R[c][i - copy_d] for c in range(C)]; copy_left -= 1
            else:
                A = (ey[x - 1] if x >= 1 else 0) + (ey[x - 2] if x >= 2 else 0)
                if ey1 is not None: A += ey1[x] + (ey1[x - 1] if x >= 1 else 0) + (ey1[x + 1] if x + 1 < w else 0)
                if ey2 is not None: A += ey2[x]
                k = (A > 0) + (A > 3) + (A > 12) + (A > 48)
                gs = d.decode(tables[k])
                if gs >= 256:
                    code = gs - 256; length = MINLEN + unprefix(code, d.bits(code_bits(code)))
                    ds = d.decode(tables[DISTANCE])
                    if ds < NSPECIAL: dist = special[ds]
                    else:
                        code = ds - NSPECIAL; dist = 1 + unprefix(code, d.bits(code_bits(code)))
                    if dist < 1 or dist > i or length > n - i: raise ValueError("match extent")
                    copy_d = dist; copy_left = length - 1
                    res = [R[c][i - dist] for c in range(C)]
                else:
                    g = unzig(gs); gb = 0 if g == 0 else (1 if -2 <= g <= 2 else 2)
                    red = unzig(d.decode(tables[RED0 + 3 * k + gb])); blue = unzig(d.decode(tables[BLUE0 + 3 * k + gb]))
                    if k >= GATE: red = wrap(red + g); blue = wrap(blue + g)
                    res = [red, g, blue]
                    if C == 4:
                        sa = s[3]; up = sa[y - 1][x] if y >= 1 else 0
                        left = sa[y][x - 1] if x >= 1 else up; ul = sa[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                        res.append(unzig(d.decode(tables[ALPHA0 + (0 if left == up == ul else 1)])))
            energy = 0
            for c in range(C):
                sc = s[c]; r = res[c]; R[c][i] = r
                up = sc[y - 1][x] if y >= 1 else 0
                left = sc[y][x - 1] if x >= 1 else up
                ul = sc[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                hi = left if left > up else up; lo = up if left > up else left
                m = lo if ul >= hi else (hi if ul <= lo else left + up - ul)
                sc[y][x] = (m + r) & 255
                if c < 3:
                    a = r if r >= 0 else -r
                    if a > energy: energy = a
            ey[x] = energy
    if copy_left or d.pos != len(d.data) or d.x != LOWER: raise ValueError("rANS stream extent")
    return np.stack([np.array(s[c], np.uint8) for c in range(C)], axis=2)


# ------------------------------------------------------------------ image container
def encode(img, z=None, detail=None):
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if C not in (3, 4): raise ValueError("channels")
    z = z or Zstd()
    grid = tile_grid(W, H)
    parts = [tile_ops(img[y0:y1, x0:x1]) for (y0, y1, x0, x1) in grid] + [tile_ops(block_means(img))]
    counts = np.zeros((CONTEXTS, GREEN_SYMBOLS), np.int64)
    for ops, _, _ in parts:
        for kind, a, b in ops:
            if kind == "s": counts[a, b] += 1
    qs = [quantize(counts[k]) for k in range(CONTEXTS)]
    tables = [Table(normalize(q)) for q in qs]
    blob = serialize_tables(qs, z)
    payloads = [rans_encode(ops, tables) for ops, _, _ in parts[:-1]]
    mosaic = rans_encode(parts[-1][0], tables)
    fields = struct.pack(">IIBBIIII", W, H, C, TILE, len(mosaic), crc(mosaic), len(blob), crc(blob)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    if detail is not None:
        detail.update(tables=len(blob), mosaic=len(mosaic), payload=sum(len(p) for p in payloads),
                      matches=sum(p[1] for p in parts[:-1]), matched_pixels=sum(p[2] for p in parts[:-1]))
    return MAGIC + fields + directory + blob + mosaic + b"".join(payloads) + mosaic + blob + directory + fields


def _valid(block, body):
    return len(block) >= body + 4 and struct.unpack(">I", block[body:body + 4])[0] == crc(block[:body])


def parse(data, z):
    if data[:8] != MAGIC or len(data) < 72: raise ValueError("signature")
    front, tail = data[8:38], data[-30:]
    fields = front if _valid(front, 26) else tail if _valid(tail, 26) else None
    if fields is None: raise ValueError("header: both copies damaged")
    W, H, C, side, mlen, mcrc, tlen, tcrc = struct.unpack(">IIBBIIII", fields[:26])
    if W == 0 or H == 0 or C not in (3, 4) or side != TILE: raise ValueError("header fields")
    grid = tile_grid(W, H, side); n = len(grid); block = 8 * n + 4
    if len(data) < 38 + 2 * block + 2 * mlen + 2 * tlen + 30: raise ValueError("extent")
    first, mirror = data[38:38 + block], data[len(data) - 30 - block:len(data) - 30]
    directory = first if _valid(first, 8 * n) else mirror if _valid(mirror, 8 * n) else None
    if directory is None: raise ValueError("directory: both copies damaged")
    entries = [struct.unpack(">II", directory[8 * i:8 * i + 8]) for i in range(n)]
    end = len(data) - 30 - block - tlen - mlen; start = 38 + block + tlen + mlen
    if start > end: raise ValueError("payload extent")
    blob = next((b for b in (data[38 + block:38 + block + tlen], data[end + mlen:end + mlen + tlen]) if crc(b) == tcrc), None)
    if blob is None: raise ValueError("tables: both copies damaged")
    tables = [Table(normalize(q)) for q in parse_tables(blob, z)]
    mosaic = next((m for m in (data[38 + block + tlen:start], data[end:end + mlen]) if crc(m) == mcrc), None)
    pos = start; tiles = []
    for rect, (length, checksum) in zip(grid, entries):
        payload = data[pos:min(pos + length, end)]; pos += length
        tiles.append(dict(rect=rect, payload=payload, ok=len(payload) == length and crc(payload) == checksum))
    if pos != end: raise ValueError("payload extent")
    return W, H, C, tables, tiles, mosaic


def decode_tiles(data, z=None):
    """Decode every intact tile; lost tiles take their cells' mosaic colour (mid grey without a mosaic)."""
    z = z or Zstd(); W, H, C, tables, tiles, mosaic = parse(data, z)
    out = np.full((H, W, C), 128, np.uint8); failed = []
    if mosaic is not None:
        try:
            m = decode_tile(mosaic, -(-W // CELL), -(-H // CELL), C, tables)
            out[:] = np.repeat(np.repeat(m, CELL, axis=0), CELL, axis=1)[:H, :W]
        except (ValueError, IndexError):
            pass
    for index, t in enumerate(tiles):
        y0, y1, x0, x1 = t["rect"]
        try:
            if not t["ok"]: raise ValueError("checksum")
            out[y0:y1, x0:x1] = decode_tile(t["payload"], x1 - x0, y1 - y0, C, tables)
        except (ValueError, IndexError):
            failed.append(index)
    return out, failed


def decode(data, z=None):
    out, failed = decode_tiles(data, z)
    if failed: raise ValueError(f"tiles failed: {failed}")
    return out
