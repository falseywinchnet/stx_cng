"""Tiled transform + zstd research reference (stream CNGZ0001).

A PNG/WebP-class construction: each 128x128 tile is turned into byte streams by a cheap reversible transform that
carries what the phase-coding study learned, and the streams are compressed by zstd. No per-pixel adaptive coding.

Per tile, per pixel in raster order (s = samples, all arithmetic exact):
  1. MED(L, U, UL) per channel, with the JPEG-LS boundary rules of the phase codec (row above the tile is 0, left of
     the tile is U, UL and UR fall back to U).
  2. Energy e = max over the three colour channels of |wrap(s - MED)|; activity A = e(L) + e(U) + e(UL) + e(UR) +
     e(LL) + e(UU), zero outside the tile. Class = number of the edges 0, 3, 12, 48 that A exceeds (0..4).
  3. Quiet pixels (class < GATE, at least two rows and columns inside the tile on the left and right) use the phase-
     rounded prediction: a least-squares plane through the twelve causal samples of the 5x2 window above and the two
     samples to the left, evaluated in Q8 fixed point (weights fixed by this geometry), rounded half up and clamped.
     Its sub-integer offset (the phase) says which side of the rounded value the surface lies on, and the residual's
     sign is flipped when the surface lies above, so that the likelier of +1 and -1 maps to the smaller symbol.
     All other pixels use MED.
  4. Colour gate: quiet pixels code the three colour residuals directly; busy pixels (class >= GATE) code the green
     residual and the red and blue residuals minus the green one.
  5. Residuals are zigzagged to bytes. Stream split: one colour stream per class, holding the green-plane bytes of
     that class's pixels, then red, then blue, each in pixel order; alpha (MED, no phase) in its own stream.
Each stream is one magicless zstd frame (level 19, no content size, checksum or dictionary id). Tile 0's
uncompressed streams, concatenated, form a raw-content dictionary; every later tile is compressed with and without
it and keeps the smaller, recording the choice in its first payload byte, so a tile is guaranteed independent
exactly when its flag is clear.

Mosaic: the mean colour of every 16x16 cell (edge cells smaller, rounded half up) as an image coded by the same tile
construction without a dictionary.

Container (big-endian; CRC32 is zlib's):
    "CNGZ0001"
    fields: width u32, height u32, channels u8, tile side u8 (128), mosaic length u32, mosaic CRC32,
            tile 0 copy length u32 (the tile 0 payload length when any tile shares the dictionary, else 0) ; CRC32
    directory: per tile in raster order: payload length u32, payload CRC32 ; CRC32 of the entries
    mosaic | tile payloads in raster order | mosaic again | tile 0 payload again (when shared) | directory again |
    fields again
A tile payload is: flags u8 (bit 0: compressed against the dictionary), then per stream a LEB128 compressed length
(0 for an empty stream), then the frames.

This is an exact, allocating Python reference for sizes and correctness, not a speed or memory claim. The flags of
CONFIG switch individual components off for the ablation study; the stream format is defined for the default."""
import ctypes, ctypes.util, struct, zlib
from fractions import Fraction
from pathlib import Path
import numpy as np

MAGIC = b"CNGZ0001"; TILE = 128; CELL = 16; LEVEL = 19
EDGES = (0, 3, 12, 48)
GATE = 3
CLASSES = len(EDGES) + 1
CONFIG = dict(plane=True, flip=True, split=True, gate=True, dictionary=True)
WINDOW = [(dx, -2) for dx in range(-2, 3)] + [(dx, -1) for dx in range(-2, 3)] + [(-2, 0), (-1, 0)]   # (dx, dy)
COLOUR_ORDER = (1, 0, 2)   # planes in a colour stream: green, red, blue


def plane_weights(window=WINDOW, scale=256):
    """Integer weights (sum = scale) of the least-squares plane through `window`, evaluated at the origin."""
    m = [[Fraction(0)] * 3 for _ in range(3)]
    for dx, dy in window:
        v = (1, dx, dy)
        for i in range(3):
            for j in range(3): m[i][j] += v[i] * v[j]
    # first row of the inverse by Gauss-Jordan on exact fractions
    a = [row[:] + [Fraction(int(i == j)) for j in range(3)] for i, row in enumerate(m)]
    for col in range(3):
        pivot = next(r for r in range(col, 3) if a[r][col] != 0); a[col], a[pivot] = a[pivot], a[col]
        p = a[col][col]; a[col] = [x / p for x in a[col]]
        for r in range(3):
            if r != col and a[r][col] != 0:
                f = a[r][col]; a[r] = [x - f * y for x, y in zip(a[r], a[col])]
    first = a[0][3:]
    exact = [first[0] + first[1] * dx + first[2] * dy for dx, dy in window]
    weights = [int((w * scale).__round__()) for w in exact]
    largest = max(range(len(weights)), key=lambda i: abs(exact[i]))
    weights[largest] += scale - sum(weights)
    return tuple(weights)


WEIGHTS = plane_weights()


def wrap(v):
    return ((v + 128) & 255) - 128


def zigzag(v):
    return np.where(v >= 0, 2 * v, -2 * v - 1)


# ------------------------------------------------------------------ zstd (libzstd 1.5.7 via ctypes)
class Zstd:
    """Owned compression and decompression contexts: magicless frames at LEVEL, no content size, checksum or
    dictionary id; an optional raw-content prefix per call (single use, borrowed for the call only)."""
    def __init__(self, level=LEVEL):
        path = "/opt/homebrew/opt/zstd/lib/libzstd.dylib"
        if not Path(path).exists(): path = ctypes.util.find_library("zstd")
        lib = self.lib = ctypes.CDLL(path)
        lib.ZSTD_createCCtx.restype = ctypes.c_void_p; lib.ZSTD_createDCtx.restype = ctypes.c_void_p
        lib.ZSTD_CCtx_setParameter.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]; lib.ZSTD_CCtx_setParameter.restype = ctypes.c_size_t
        lib.ZSTD_DCtx_setParameter.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]; lib.ZSTD_DCtx_setParameter.restype = ctypes.c_size_t
        lib.ZSTD_CCtx_reset.argtypes = [ctypes.c_void_p, ctypes.c_int]; lib.ZSTD_CCtx_reset.restype = ctypes.c_size_t
        lib.ZSTD_DCtx_reset.argtypes = [ctypes.c_void_p, ctypes.c_int]; lib.ZSTD_DCtx_reset.restype = ctypes.c_size_t
        lib.ZSTD_CCtx_refPrefix.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]; lib.ZSTD_CCtx_refPrefix.restype = ctypes.c_size_t
        lib.ZSTD_DCtx_refPrefix.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]; lib.ZSTD_DCtx_refPrefix.restype = ctypes.c_size_t
        lib.ZSTD_compressBound.argtypes = [ctypes.c_size_t]; lib.ZSTD_compressBound.restype = ctypes.c_size_t
        lib.ZSTD_compress2.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]; lib.ZSTD_compress2.restype = ctypes.c_size_t
        lib.ZSTD_decompressDCtx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]; lib.ZSTD_decompressDCtx.restype = ctypes.c_size_t
        lib.ZSTD_isError.argtypes = [ctypes.c_size_t]; lib.ZSTD_isError.restype = ctypes.c_uint
        self.level = level
        self.cctx = lib.ZSTD_createCCtx(); self.dctx = lib.ZSTD_createDCtx()
        self._parameters()

    def _check(self, code, what):
        if self.lib.ZSTD_isError(code): raise ValueError("zstd " + what)
        return code

    def _parameters(self):
        lib = self.lib
        lib.ZSTD_CCtx_reset(self.cctx, 3); lib.ZSTD_DCtx_reset(self.dctx, 3)   # session and parameters
        for parameter, value in ((100, self.level), (200, 0), (201, 0), (202, 0), (10, 1)):   # level, no size/checksum/id, magicless
            self._check(lib.ZSTD_CCtx_setParameter(self.cctx, parameter, value), "compression parameter")
        self._check(lib.ZSTD_DCtx_setParameter(self.dctx, 1000, 1), "decompression format")

    def compress(self, data, prefix=b""):
        if not data: return b""
        lib = self.lib
        if prefix: self._check(lib.ZSTD_CCtx_refPrefix(self.cctx, prefix, len(prefix)), "prefix")
        capacity = lib.ZSTD_compressBound(len(data)); out = ctypes.create_string_buffer(capacity)
        count = lib.ZSTD_compress2(self.cctx, out, capacity, data, len(data))
        if lib.ZSTD_isError(count): self._parameters(); raise ValueError("zstd compress")
        return out.raw[:count]

    def decompress(self, blob, capacity, prefix=b""):
        if not blob: return b""
        lib = self.lib
        if prefix: self._check(lib.ZSTD_DCtx_refPrefix(self.dctx, prefix, len(prefix)), "prefix")
        out = ctypes.create_string_buffer(max(1, capacity))
        count = lib.ZSTD_decompressDCtx(self.dctx, out, capacity, blob, len(blob))
        if lib.ZSTD_isError(count): self._parameters(); raise ValueError("zstd decompress")
        return out.raw[:count]


# ------------------------------------------------------------------ tile transform (vectorized encoder side)
def neighbours(s):
    """L, U, UL, UR per channel under the boundary rules, for an (h, w, C) int32 tile."""
    U = np.zeros_like(s); U[1:] = s[:-1]
    L = U.copy(); L[:, 1:] = s[:, :-1]
    UL = U.copy(); UL[1:, 1:] = s[:-1, :-1]
    UR = U.copy(); UR[1:, :-1] = s[:-1, 1:]
    return L, U, UL, UR


def median_edge(a, b, c):
    hi = np.maximum(a, b); lo = np.minimum(a, b)
    return np.where(c >= hi, lo, np.where(c <= lo, hi, a + b - c))


def transform(tile, cfg=CONFIG):
    """Byte streams of one tile: [class 0..4 colour streams] (+ [alpha]) when split, else [colour] (+ [alpha])."""
    s = tile.astype(np.int32); h, w, C = s.shape
    L, U, UL, UR = neighbours(s)
    med = median_edge(L, U, UL)
    e = np.abs(wrap(s[:, :, :3] - med[:, :, :3])).max(axis=2)
    E = np.zeros((h + 2, w + 4), np.int32); E[2:, 2:w + 2] = e
    A = E[2:, 1:w + 1] + E[2:, 0:w] + E[1:h + 1, 2:w + 2] + E[0:h, 2:w + 2] + E[1:h + 1, 1:w + 1] + E[1:h + 1, 3:w + 3]
    cls = sum((A > edge).astype(np.int32) for edge in EDGES)
    pred = med.copy(); flip = np.zeros((h, w, 3), bool)
    if cfg["plane"] and h >= 3 and w >= 5:
        p = np.zeros((h - 2, w - 4, 3), np.int64)
        for (dx, dy), weight in zip(WINDOW, WEIGHTS):
            p += weight * s[2 + dy:h + dy, 2 + dx:w - 2 + dx, :3]
        q = np.clip((p + 128) >> 8, 0, 255); phase = p - 256 * q
        use = np.zeros((h, w), bool); use[2:, 2:w - 2] = cls[2:, 2:w - 2] < GATE
        inner = use[2:, 2:w - 2]
        pred[2:, 2:w - 2, :3] = np.where(inner[:, :, None], q, med[2:, 2:w - 2, :3])
        if cfg["flip"]:
            flip[2:, 2:w - 2] = inner[:, :, None] & (phase > 0)
    r = wrap(s - pred)
    r[:, :, :3] = np.where(flip, wrap(-r[:, :, :3]), r[:, :, :3])
    coded = r[:, :, :3].copy()
    if cfg["gate"]:
        busy = (cls >= GATE)[:, :, None]
        green = r[:, :, 1:2]
        coded[:, :, 0:1] = np.where(busy, wrap(r[:, :, 0:1] - green), r[:, :, 0:1])
        coded[:, :, 2:3] = np.where(busy, wrap(r[:, :, 2:3] - green), r[:, :, 2:3])
    z = zigzag(coded).astype(np.uint8)
    flat_cls = cls.reshape(-1); flat_z = z.reshape(-1, 3)
    streams = []
    groups = [flat_cls == k for k in range(CLASSES)] if cfg["split"] else [np.ones_like(flat_cls, bool)]
    for mask in groups:
        chosen = flat_z[mask]
        streams.append(b"".join(chosen[:, plane].tobytes() for plane in COLOUR_ORDER))
    if C == 4:
        streams.append(zigzag(r[:, :, 3]).astype(np.uint8).tobytes())
    return streams


# ------------------------------------------------------------------ tile transform (sequential decoder side)
def untransform(streams, w, h, C, cfg=CONFIG):
    """Reconstructs an (h, w, C) uint8 tile from its byte streams, pixel by pixel, exactly as `transform` defined them."""
    s = [[[0] * w for _ in range(h)] for _ in range(C)]
    e = [[0] * w for _ in range(h)]
    colour = streams[:CLASSES] if cfg["split"] else streams[:1]
    counts = [len(st) // 3 for st in colour]
    cursor = [0] * len(colour)
    alpha = streams[-1] if C == 4 else None
    acursor = 0
    use_plane_cfg = cfg["plane"] and h >= 3 and w >= 5
    gate = cfg["gate"]; flip_cfg = cfg["flip"]; split = cfg["split"]
    weights = WEIGHTS; window = WINDOW
    for y in range(h):
        ey = e[y]; ey1 = e[y - 1] if y >= 1 else None; ey2 = e[y - 2] if y >= 2 else None
        for x in range(w):
            A = (ey[x - 1] if x >= 1 else 0) + (ey[x - 2] if x >= 2 else 0)
            if ey1 is not None:
                A += ey1[x] + (ey1[x - 1] if x >= 1 else 0) + (ey1[x + 1] if x + 1 < w else 0)
            if ey2 is not None:
                A += ey2[x]
            k = (A > 0) + (A > 3) + (A > 12) + (A > 48)
            plane = use_plane_cfg and k < GATE and y >= 2 and 2 <= x < w - 2
            stream = k if split else 0
            st = colour[stream]; n = counts[stream]; i = cursor[stream]; cursor[stream] = i + 1
            zs = (st[n + i], st[i], st[2 * n + i])   # red, green, blue bytes (planes are green, red, blue)
            vals = [(z >> 1) if (z & 1) == 0 else -((z + 1) >> 1) for z in zs]
            if gate and k >= GATE:
                vals[0] = wrap(vals[0] + vals[1]); vals[2] = wrap(vals[2] + vals[1])
            energy = 0
            for c in range(3):
                sc = s[c]
                up = sc[y - 1][x] if y >= 1 else 0
                left = sc[y][x - 1] if x >= 1 else up
                ul = sc[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                hi = left if left > up else up; lo = up if left > up else left
                m = lo if ul >= hi else (hi if ul <= lo else left + up - ul)
                r = vals[c]
                if plane:
                    p = 0
                    for (dx, dy), weight in zip(window, weights):
                        p += weight * sc[y + dy][x + dx]
                    q = (p + 128) >> 8
                    q = 0 if q < 0 else (255 if q > 255 else q)
                    if flip_cfg and p - 256 * q > 0: r = wrap(-r)
                    v = (q + r) & 255
                else:
                    v = (m + r) & 255
                sc[y][x] = v
                d = wrap(v - m); d = -d if d < 0 else d
                if d > energy: energy = d
            ey[x] = energy
            if C == 4:
                sa = s[3]
                up = sa[y - 1][x] if y >= 1 else 0
                left = sa[y][x - 1] if x >= 1 else up
                ul = sa[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                hi = left if left > up else up; lo = up if left > up else left
                m = lo if ul >= hi else (hi if ul <= lo else left + up - ul)
                z = alpha[acursor]; acursor += 1
                sa[y][x] = (m + ((z >> 1) if (z & 1) == 0 else -((z + 1) >> 1))) & 255
    if any(cursor[j] != counts[j] for j in range(len(colour))) or (C == 4 and acursor != len(alpha)):
        raise ValueError("stream extent")
    return np.stack([np.array(s[c], np.uint8) for c in range(C)], axis=2)


# ------------------------------------------------------------------ tile payloads
def leb128(n):
    out = bytearray()
    while True:
        b = n & 0x7F; n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n: return bytes(out)


def read_leb128(data, pos):
    n = shift = 0
    while True:
        if pos >= len(data) or shift > 28: raise ValueError("length field")
        b = data[pos]; pos += 1; n |= (b & 0x7F) << shift; shift += 7
        if not b & 0x80: return n, pos


def pack(streams, z, prefix):
    frames = [z.compress(st, prefix) for st in streams]
    return b"".join(leb128(len(f)) for f in frames) + b"".join(frames)


def encode_tile(tile, z, cfg=CONFIG, dictionary=b""):
    """(payload, raw streams) of one tile; with a dictionary the payload is the smaller of the two codings."""
    streams = transform(tile, cfg)
    alone = b"\x00" + pack(streams, z, b"")
    if cfg["dictionary"] and dictionary:
        shared = b"\x01" + pack(streams, z, dictionary)
        if len(shared) < len(alone): return shared, streams
    return alone, streams


def decode_tile(payload, w, h, C, z, cfg=CONFIG, dictionary=None):
    if not payload: raise ValueError("empty tile")
    flags = payload[0]
    if flags & ~1: raise ValueError("tile flags")
    if flags & 1 and dictionary is None: raise ValueError("tile shares a lost dictionary")
    prefix = dictionary if flags & 1 else b""
    count = (CLASSES if cfg["split"] else 1) + (1 if C == 4 else 0)
    pos = 1; lengths = []
    for _ in range(count):
        n, pos = read_leb128(payload, pos); lengths.append(n)
    if pos + sum(lengths) != len(payload): raise ValueError("tile extent")
    streams = []
    for index, n in enumerate(lengths):
        capacity = w * h * (1 if (C == 4 and index == count - 1) else 3)
        streams.append(z.decompress(payload[pos:pos + n], capacity, prefix)); pos += n
    return untransform(streams, w, h, C, cfg), streams


# ------------------------------------------------------------------ image container
def tile_grid(W, H, side=TILE):
    return [(y0, min(H, y0 + side), x0, min(W, x0 + side)) for y0 in range(0, H, side) for x0 in range(0, W, side)]


def block_means(img, cell=CELL):
    H, W, C = img.shape; mh, mw = -(-H // cell), -(-W // cell)
    pad = np.zeros((mh * cell, mw * cell, C), np.int64); pad[:H, :W] = img
    ones = np.zeros((mh * cell, mw * cell), np.int64); ones[:H, :W] = 1
    sums = pad.reshape(mh, cell, mw, cell, C).sum(axis=(1, 3)); counts = ones.reshape(mh, cell, mw, cell).sum(axis=(1, 3))[:, :, None]
    return ((sums + counts // 2) // counts).astype(np.uint8)


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def encode(img, cfg=CONFIG, z=None):
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if C not in (3, 4): raise ValueError("channels")
    z = z or Zstd()
    payloads = []; dictionary = b""
    for index, (y0, y1, x0, x1) in enumerate(tile_grid(W, H)):
        payload, streams = encode_tile(img[y0:y1, x0:x1], z, cfg, dictionary if index else b"")
        if index == 0: dictionary = b"".join(streams)
        payloads.append(payload)
    mosaic, _ = encode_tile(block_means(img), z, cfg)
    copy = payloads[0] if any(p[0] & 1 for p in payloads[1:]) else b""
    fields = struct.pack(">IIBBIII", W, H, C, TILE, len(mosaic), crc(mosaic), len(copy)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    return MAGIC + fields + directory + mosaic + b"".join(payloads) + mosaic + copy + directory + fields


def _valid(block, body):
    return len(block) >= body + 4 and struct.unpack(">I", block[body:body + 4])[0] == crc(block[:body])


def parse(data):
    if data[:8] != MAGIC or len(data) < 60: raise ValueError("signature")
    front, tail = data[8:34], data[-26:]
    fields = front if _valid(front, 22) else tail if _valid(tail, 22) else None
    if fields is None: raise ValueError("header: both copies damaged")
    W, H, C, side, mlen, mcrc, clen = struct.unpack(">IIBBIII", fields[:22])
    if W == 0 or H == 0 or C not in (3, 4) or side != TILE: raise ValueError("header fields")
    grid = tile_grid(W, H, side); n = len(grid); block = 8 * n + 4
    if len(data) < 34 + 2 * block + 2 * mlen + clen + 26: raise ValueError("directory extent")
    first, mirror = data[34:34 + block], data[len(data) - 26 - block:len(data) - 26]
    directory = first if _valid(first, 8 * n) else mirror if _valid(mirror, 8 * n) else None
    if directory is None: raise ValueError("directory: both copies damaged")
    entries = [struct.unpack(">II", directory[8 * i:8 * i + 8]) for i in range(n)]
    len0 = entries[0][0]
    if clen not in (0, len0): raise ValueError("tile 0 copy length")
    end = len(data) - 26 - block - clen - mlen
    if 34 + block + mlen > end: raise ValueError("payload extent")
    mosaics = [data[34 + block:34 + block + mlen], data[end:end + mlen]]
    mosaic = next((m for m in mosaics if crc(m) == mcrc), None)
    pos = 34 + block + mlen; tiles = []
    for (rect, (length, checksum)) in zip(grid, entries):
        payload = data[pos:min(pos + length, end)]; pos += length
        tiles.append(dict(rect=rect, payload=payload, ok=len(payload) == length and crc(payload) == checksum))
    if pos != end: raise ValueError("payload extent")
    copy = data[end + mlen:end + mlen + clen]
    if not tiles[0]["ok"] and clen and crc(copy) == entries[0][1]:
        tiles[0] = dict(rect=tiles[0]["rect"], payload=copy, ok=True)
    return W, H, C, tiles, mosaic


def decode_tiles(data, cfg=CONFIG, z=None):
    """Decode every intact tile; return (image, failed tile indices). Lost tiles take their cells' mosaic colour
    (mid grey without a mosaic); a tile that shares the dictionary is lost with tile 0."""
    W, H, C, tiles, mosaic = parse(data); z = z or Zstd()
    out = np.full((H, W, C), 128, np.uint8); failed = []
    if mosaic is not None:
        try:
            m, _ = decode_tile(mosaic, -(-W // CELL), -(-H // CELL), C, z, cfg)
            out[:] = np.repeat(np.repeat(m, CELL, axis=0), CELL, axis=1)[:H, :W]
        except ValueError:
            pass
    dictionary = None
    for index, t in enumerate(tiles):
        y0, y1, x0, x1 = t["rect"]
        try:
            if not t["ok"]: raise ValueError("checksum")
            tile, streams = decode_tile(t["payload"], x1 - x0, y1 - y0, C, z, cfg, dictionary if index else None)
            out[y0:y1, x0:x1] = tile
            if index == 0: dictionary = b"".join(streams)
        except ValueError:
            failed.append(index)
    return out, failed


def decode(data, cfg=CONFIG, z=None):
    out, failed = decode_tiles(data, cfg, z)
    if failed: raise ValueError(f"tiles failed: {failed}")
    return out
