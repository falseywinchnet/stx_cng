"""Tiled context-coded research reference (stream CNGC0001): zstd's entropy stage made context-aware.

zstd codes literals with one order-0 table per block and decodes all of them before any output exists, so a context
taken from reconstructed pixels cannot reach it. This construction keeps zstd's kind of entropy coder (static
table-driven ANS, one table lookup and one multiply per symbol) but gives it one table per context and decodes each
symbol in step with reconstruction, so its context is known when it is decoded. zstd's LZ stage is not used here.

Per tile, per pixel in raster order, exactly as research/tiled_zstd_codec.py with the plane and the stream split off:
MED prediction per channel (boundary rules of the phase codec), activity class 0..4 from the six causal MED-residual
energies, colour gate (busy pixels code green, red minus green, blue minus green), zigzag symbols. Contexts:
    green  : class                                   (5)
    red    : class x bucket of the green residual    (15)   bucket: 0 if zero, 1 if |r| <= 2, 2 otherwise
    blue   : class x bucket of the green residual    (15)
    alpha  : whether the alpha neighbours L, U, UL are equal (2)
Each symbol is coded by byte-wise rANS (32-bit state, 12-bit frequencies) with its context's table; each tile is
one rANS stream (state in the first four bytes), so tiles decode independently given the tables.

Tables are per image: for each context, the frequency of every symbol up to the last one used, quantized to half
powers of two (q = 1 + round(2 log2 f), 0 for unused) and renormalized deterministically to 4096 on both sides; the
q bytes of all contexts are compressed as one zstd frame. Every symbol that occurs (tiles and mosaic) has q >= 1.

Container (big-endian; CRC32 is zlib's):
    "CNGC0001"
    fields: width u32, height u32, channels u8, tile side u8 (128), mosaic length u32, mosaic CRC32,
            tables length u32, tables CRC32 ; CRC32 of the fields
    directory: per tile, payload length u32 and CRC32 ; CRC32 of the entries
    tables | mosaic | tile payloads in raster order | mosaic again | tables again | directory again | fields again
Mosaic: the 16x16-cell mean image, coded as one tile with the image's tables.
Exact, allocating Python reference for sizes and correctness; no speed or memory claim."""
import math, struct
import numpy as np
from research.tiled_zstd_codec import (CELL, CLASSES, EDGES, GATE, TILE, Zstd, block_means, crc, median_edge,
                                       neighbours, tile_grid, wrap, zigzag)

MAGIC = b"CNGC0001"
PRECISION = 12; TOTAL = 1 << PRECISION; LOWER = 1 << 23
CONTEXTS = CLASSES + 2 * 3 * CLASSES + 2
RED0, BLUE0, ALPHA0 = CLASSES, CLASSES + 3 * CLASSES, CLASSES + 6 * CLASSES
LEVELS = [max(1, round(2 ** (i / 2))) for i in range(25)]   # dequantized frequency of q = i + 1


def bucket(v):
    return np.where(v == 0, 0, np.where(np.abs(v) <= 2, 1, 2))


# ------------------------------------------------------------------ symbols and contexts (vectorized encoder side)
def tile_symbols(tile):
    """(contexts, symbols) of one tile in coding order: per pixel green, red, blue, then alpha."""
    s = tile.astype(np.int32); h, w, C = s.shape
    L, U, UL, UR = neighbours(s)
    med = median_edge(L, U, UL)
    r = wrap(s - med)
    e = np.abs(r[:, :, :3]).max(axis=2)
    E = np.zeros((h + 2, w + 4), np.int32); E[2:, 2:w + 2] = e
    A = E[2:, 1:w + 1] + E[2:, 0:w] + E[1:h + 1, 2:w + 2] + E[0:h, 2:w + 2] + E[1:h + 1, 1:w + 1] + E[1:h + 1, 3:w + 3]
    cls = sum((A > edge).astype(np.int32) for edge in EDGES)
    busy = cls >= GATE
    g = r[:, :, 1]
    red = np.where(busy, wrap(r[:, :, 0] - g), r[:, :, 0]); blue = np.where(busy, wrap(r[:, :, 2] - g), r[:, :, 2])
    gb = bucket(g)
    ctx = [cls, RED0 + 3 * cls + gb, BLUE0 + 3 * cls + gb]
    sym = [zigzag(g), zigzag(red), zigzag(blue)]
    if C == 4:
        a = s[:, :, 3]; quiet = (L[:, :, 3] == U[:, :, 3]) & (U[:, :, 3] == UL[:, :, 3])
        ctx.append(ALPHA0 + np.where(quiet, 0, 1)); sym.append(zigzag(r[:, :, 3]))
    return np.stack(ctx, axis=2).reshape(-1), np.stack(sym, axis=2).reshape(-1)


# ------------------------------------------------------------------ tables
def quantize(counts):
    """q bytes of one context: per symbol up to the last used, 0 unused, else 1 + round(2 log2 f) of its share of 4096."""
    total = int(counts.sum())
    if total == 0: return b""
    last = int(np.nonzero(counts)[0].max()) + 1
    out = bytearray()
    for c in counts[:last]:
        if c == 0: out.append(0); continue
        f = c * TOTAL / total
        out.append(1 + (min(24, max(0, round(2 * math.log2(f)))) if f >= 1 else 0))   # every used symbol keeps q >= 1
    return bytes(out)


def normalize(q):
    """Frequencies summing to 4096 from q bytes; identical on both sides (integers only)."""
    f = [0 if v == 0 else LEVELS[v - 1] for v in q]
    total = sum(f)
    if total == 0: return []
    scaled = [0 if v == 0 else max(1, v * TOTAL // total) for v in f]
    order = sorted(range(len(scaled)), key=lambda i: (-scaled[i], i))
    diff = TOTAL - sum(scaled); i = 0
    while diff != 0:
        k = order[i % len(order)]
        if diff > 0: scaled[k] += diff; diff = 0
        elif scaled[k] > 1:
            take = min(scaled[k] - 1, -diff); scaled[k] -= take; diff += take
        i += 1
    return scaled


class Table:
    def __init__(self, freq):
        self.freq = freq; self.cum = [0] * len(freq); acc = 0
        for i, v in enumerate(freq): self.cum[i] = acc; acc += v
        self.slot = bytearray(TOTAL) if freq else None
        if freq:
            for i, v in enumerate(freq):
                for j in range(self.cum[i], self.cum[i] + v): self.slot[j] = i


def serialize_tables(qs, z):
    body = b"".join(struct.pack(">H", len(q)) + q for q in qs)
    return z.compress(body)


def parse_tables(blob, z):
    body = z.decompress(blob, CONTEXTS * (2 + 256)); pos = 0; qs = []
    for _ in range(CONTEXTS):
        if pos + 2 > len(body): raise ValueError("tables")
        n = struct.unpack(">H", body[pos:pos + 2])[0]; pos += 2
        if n > 256 or pos + n > len(body): raise ValueError("tables")
        qs.append(body[pos:pos + n]); pos += n
    if pos != len(body): raise ValueError("tables")
    return qs


# ------------------------------------------------------------------ rANS
def rans_encode(contexts, symbols, tables):
    x = LOWER; out = bytearray()
    for k, s in zip(reversed(contexts.tolist()), reversed(symbols.tolist())):
        t = tables[k]; f = t.freq[s]
        limit = ((LOWER >> PRECISION) << 8) * f
        while x >= limit: out.append(x & 255); x >>= 8
        x = ((x // f) << PRECISION) + (x % f) + t.cum[s]
    out.reverse()
    return struct.pack(">I", x) + bytes(out)


class Decoder:
    def __init__(self, data):
        if len(data) < 4: raise ValueError("rANS stream")
        self.x = struct.unpack(">I", data[:4])[0]; self.data = data; self.pos = 4

    def decode(self, t):
        if t.slot is None: raise ValueError("unused context")
        x = self.x; m = x & (TOTAL - 1); s = t.slot[m]
        x = t.freq[s] * (x >> PRECISION) + m - t.cum[s]
        while x < LOWER:
            if self.pos >= len(self.data): raise ValueError("rANS exhausted")
            x = (x << 8) | self.data[self.pos]; self.pos += 1
        self.x = x
        return s


# ------------------------------------------------------------------ tile decode (sequential)
def unzig(z):
    return (z >> 1) if (z & 1) == 0 else -((z + 1) >> 1)


def decode_tile(payload, w, h, C, tables):
    d = Decoder(payload)
    s = [[[0] * w for _ in range(h)] for _ in range(C)]
    e = [[0] * w for _ in range(h)]
    for y in range(h):
        ey = e[y]; ey1 = e[y - 1] if y >= 1 else None; ey2 = e[y - 2] if y >= 2 else None
        for x in range(w):
            A = (ey[x - 1] if x >= 1 else 0) + (ey[x - 2] if x >= 2 else 0)
            if ey1 is not None: A += ey1[x] + (ey1[x - 1] if x >= 1 else 0) + (ey1[x + 1] if x + 1 < w else 0)
            if ey2 is not None: A += ey2[x]
            k = (A > 0) + (A > 3) + (A > 12) + (A > 48)
            g = unzig(d.decode(tables[k]))
            gb = 0 if g == 0 else (1 if -2 <= g <= 2 else 2)
            red = unzig(d.decode(tables[RED0 + 3 * k + gb])); blue = unzig(d.decode(tables[BLUE0 + 3 * k + gb]))
            if k >= GATE: red = wrap(red + g); blue = wrap(blue + g)
            energy = 0
            for c, r in ((0, red), (1, g), (2, blue)):
                sc = s[c]
                up = sc[y - 1][x] if y >= 1 else 0
                left = sc[y][x - 1] if x >= 1 else up
                ul = sc[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                hi = left if left > up else up; lo = up if left > up else left
                m = lo if ul >= hi else (hi if ul <= lo else left + up - ul)
                sc[y][x] = (m + r) & 255
                a = r if r >= 0 else -r
                if a > energy: energy = a
            ey[x] = energy
            if C == 4:
                sa = s[3]
                up = sa[y - 1][x] if y >= 1 else 0
                left = sa[y][x - 1] if x >= 1 else up
                ul = sa[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                quiet = left == up == ul
                hi = left if left > up else up; lo = up if left > up else left
                m = lo if ul >= hi else (hi if ul <= lo else left + up - ul)
                sa[y][x] = (m + unzig(d.decode(tables[ALPHA0 + (0 if quiet else 1)]))) & 255
    if d.pos != len(d.data) or d.x != LOWER: raise ValueError("rANS stream extent")
    return np.stack([np.array(s[c], np.uint8) for c in range(C)], axis=2)


# ------------------------------------------------------------------ image container
def encode(img, z=None, detail=None):
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if C not in (3, 4): raise ValueError("channels")
    z = z or Zstd()
    grid = tile_grid(W, H); mosaic_img = block_means(img)
    parts = [tile_symbols(img[y0:y1, x0:x1]) for (y0, y1, x0, x1) in grid] + [tile_symbols(mosaic_img)]
    counts = np.zeros((CONTEXTS, 256), np.int64)
    for ctx, sym in parts: np.add.at(counts, (ctx, sym), 1)
    qs = [quantize(counts[k]) for k in range(CONTEXTS)]
    tables = [Table(normalize(q)) for q in qs]
    blob = serialize_tables(qs, z)
    payloads = [rans_encode(ctx, sym, tables) for ctx, sym in parts[:-1]]
    mosaic = rans_encode(*parts[-1], tables)
    fields = struct.pack(">IIBBIIII", W, H, C, TILE, len(mosaic), crc(mosaic), len(blob), crc(blob)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    if detail is not None:
        detail.update(tables=len(blob), mosaic=len(mosaic), payload=sum(len(p) for p in payloads), index=2 * (len(fields) + len(directory)) + 8)
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
    end = len(data) - 30 - block - tlen - mlen
    start = 38 + block + tlen + mlen
    if start > end: raise ValueError("payload extent")
    blobs = [data[38 + block:38 + block + tlen], data[end + mlen:end + mlen + tlen]]
    blob = next((b for b in blobs if crc(b) == tcrc), None)
    if blob is None: raise ValueError("tables: both copies damaged")
    tables = [Table(normalize(q)) for q in parse_tables(blob, z)]
    mosaics = [data[38 + block + tlen:start], data[end:end + mlen]]
    mosaic = next((m for m in mosaics if crc(m) == mcrc), None)
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
        except ValueError:
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
