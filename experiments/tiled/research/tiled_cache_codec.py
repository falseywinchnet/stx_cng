"""Tiled context-coded reference with matches, a colour cache and escaped table entries (streams CNGM0003, CNGM0004).

The construction of research/tiled_match_codec.py (CNGM0002: MED, colour gate, causal contexts, residual matches with
repeat distances, cost-aware parse, per-image static rANS tables stored twice, independent 128x128 tiles, mosaic,
duplicated index) with two additions aimed at small images and rendered content.

Colour cache, as WebP's: each tile keeps 256 recent colours, indexed by hash(colour) = (colour * 0x1E35A7BD mod 2^32)
>> 24, where colour packs the pixel's samples (red in the low byte). Every decoded pixel, however it was coded, is
written to its slot after it is reconstructed. A pixel whose colour is in its slot can be coded by the slot index:
the green alphabet is 256 literals, 30 length codes and 256 cache indices (542 symbols). The decoder recovers the
residuals of a cached pixel from its MED predictions, so later matches and energies see the same residuals as for
a literal. The cache starts empty in every tile, so tiles stay independent.

Escaped table entries: a symbol used at most twice in a context is not listed in that context's table. Each table
lists symbols 0..L-1 (q = 0 for unlisted ones) and then an escape entry; the escape symbol (index L) is followed by
the symbol in raw bits: 10 for green contexts, 6 for the distance table, 8 otherwise. This removes most table bytes
of small images, where rare residuals dominated the listed range.

Tables body (zstd frame): per context, L as u16, L q bytes, one q byte for the escape. Container as CNGM0002 with
signature "CNGM0003". CNGM0004 is identical except that an image of a single tile stores its tables once: the copy
before the trailing directory is omitted, since on a one-tile image it protects only the table region itself. The
encoder writes CNGM0004; `encode(..., tables_once=False)` reproduces CNGM0003. The parse is the cost-aware parse of CNGM0002 with the cache option added (a cached pixel is
priced from its index symbol); its code lengths come from a greedy first pass that uses the cache whenever it hits.
Exact, allocating Python reference; sizes and correctness only."""
import math, struct
import numpy as np
from research.tiled_zstd_codec import CELL, GATE, TILE, Zstd, block_means, crc, tile_grid, wrap
from research.tiled_context_codec import ALPHA0, BLUE0, LOWER, RED0, TOTAL, normalize, unzig
from research import tiled_match_codec as M
from research.tiled_match_codec import (CHAIN, DISTANCE, MAXLEN, MINLEN, NEWLEN, NREPEAT, NSPECIAL, NCODES, SUFFICIENT,
                                        TRIAL_LENGTHS, UNSEEN, Decoder, analyse, code_bits, initial_repeats,
                                        literal_ops, match_ops, prefix, rans_encode, specials, unprefix, update_repeats)

MAGIC = b"CNGM0004"; MAGIC_BOTH = b"CNGM0003"
CACHE_BITS = 8; CACHE_SIZE = 1 << CACHE_BITS
CACHE0 = 256 + NCODES
GREEN_SYMBOLS = CACHE0 + CACHE_SIZE
CONTEXTS = DISTANCE + 1
ESCAPE_MAX = 2


def raw_bits(context):
    return 10 if context < RED0 else 6 if context == DISTANCE else 8


def slot_of(colour):
    return ((colour * 0x1E35A7BD) & 0xFFFFFFFF) >> (32 - CACHE_BITS)


def cache_hits(tile):
    """Per pixel, the cache slot holding its colour when it is reached (-1 when it is not there)."""
    h, w, C = tile.shape
    keys = (tile.astype(np.int64).reshape(-1, C) @ (np.int64(256) ** np.arange(C, dtype=np.int64))).tolist()
    cache = [-1] * CACHE_SIZE; hits = []
    for key in keys:
        slot = slot_of(key); hits.append(slot if cache[slot] == key else -1); cache[slot] = key
    return hits


def heads_insert(heads, P, n, j):
    if j + 4 <= n:
        key = (P[j], P[j + 1], P[j + 2], P[j + 3]); chain = heads.get(key)
        if chain is None: heads[key] = [j]
        else:
            chain.append(j)
            if len(chain) > 2 * CHAIN: del chain[:CHAIN]


def prepared(tile):
    h, w, C = tile.shape
    r, cls, quiet = analyse(tile)
    packed = (r + 128).astype(np.int64) @ (np.int64(256) ** np.arange(C, dtype=np.int64))
    return h, w, C, h * w, packed.tolist(), r.tolist(), cls.tolist(), quiet.tolist(), cache_hits(tile)


def tile_ops_greedy(tile):
    """Greedy parse: the longest admissible match, else the cache when it hits, else a literal."""
    h, w, C, n, P, R, K, Q, hits = prepared(tile)
    special = specials(w); repeats = initial_repeats(w); heads = {}; ops = []
    i = 0
    while i < n:
        best_length = 0; best_d = 0; limit = min(MAXLEN, n - i)
        candidates = [d for d in repeats + list(special) if 1 <= d <= i]
        if i + 4 <= n:
            chain = heads.get((P[i], P[i + 1], P[i + 2], P[i + 3]))
            if chain: candidates += [i - j for j in reversed(chain[-CHAIN:])]
        for d in candidates:
            if P[i] != P[i - d]: continue
            length = 1
            while length < limit and P[i + length] == P[i + length - d]: length += 1
            if length < (MINLEN if d in repeats else NEWLEN): continue
            if length > best_length: best_length, best_d = length, d
            if length == limit: break
        if best_length:
            match_ops(ops, K[i], best_length, best_d, repeats, special)
            for j in range(i, i + best_length): heads_insert(heads, P, n, j)
            i += best_length; continue
        if hits[i] >= 0: ops.append(("s", K[i], CACHE0 + hits[i]))
        else: literal_ops(ops, i, R, K, Q, C)
        heads_insert(heads, P, n, i); i += 1
    return ops


def tile_ops_costed(tile, costs):
    """Cheapest parse under `costs`: literal, cached colour or match at every pixel (see research/tiled_match_codec)."""
    h, w, C, n, P, R, K, Q, hits = prepared(tile)
    special = specials(w); distance = costs[DISTANCE]
    lit = []
    for i in range(n):
        probe = []; literal_ops(probe, i, R, K, Q, C)
        v = sum(costs[a][b] for _, a, b in probe)
        if hits[i] >= 0: v = min(v, costs[K[i]][CACHE0 + hits[i]])
        lit.append(v)
    INF = float("inf")
    price = [INF] * (n + 1); price[0] = 0.0
    back = [None] * (n + 1); state = [None] * (n + 1); state[0] = tuple(initial_repeats(w))
    heads = {}; i = 0
    while i < n:
        base = price[i]; reps = state[i]
        if base + lit[i] < price[i + 1]:
            price[i + 1] = base + lit[i]; back[i + 1] = (i, 0, 1); state[i + 1] = reps
        candidates = []
        for d in reps + special:
            if 1 <= d <= i and d not in candidates: candidates.append(d)
        if i + 4 <= n:
            chain = heads.get((P[i], P[i + 1], P[i + 2], P[i + 3]))
            if chain:
                for j in reversed(chain[-CHAIN:]):
                    if i - j not in candidates: candidates.append(i - j)
        longest = 0; gcost = costs[K[i]]; cap = min(n - i, MAXLEN)
        for d in candidates:
            if P[i] != P[i - d]: continue
            length = 1; limit = min(cap, SUFFICIENT)
            while length < limit and P[i + length] == P[i + length - d]: length += 1
            if length == SUFFICIENT:
                while length < cap and P[i + length] == P[i + length - d]: length += 1
            repeat = d in reps; shortest = MINLEN if repeat else NEWLEN
            if length < shortest: continue
            if repeat: dcost = distance[reps.index(d)]
            elif d in special: dcost = distance[NREPEAT + special.index(d)]
            else:
                code, bits, _ = prefix(d - 1); dcost = distance[NREPEAT + NSPECIAL + code] + bits
            moved = list(reps); update_repeats(moved, d); moved = tuple(moved)
            for trial in set([length] + [t for t in TRIAL_LENGTHS if shortest <= t < length]):
                code, bits, _ = prefix(trial - MINLEN)
                v = base + gcost[256 + code] + bits + dcost
                if v < price[i + trial]: price[i + trial] = v; back[i + trial] = (i, d, trial); state[i + trial] = moved
            if length > longest: longest = length
        if longest >= SUFFICIENT:
            for j in range(i, i + longest): heads_insert(heads, P, n, j)
            i += longest; continue
        heads_insert(heads, P, n, i); i += 1
    path = []; j = n
    while j > 0:
        i, d, length = back[j]; path.append((i, d, length)); j = i
    path.reverse()
    ops = []; repeats = initial_repeats(w)
    for i, d, length in path:
        if d: match_ops(ops, K[i], length, d, repeats, special)
        else:
            probe = []; literal_ops(probe, i, R, K, Q, C)
            if hits[i] >= 0 and costs[K[i]][CACHE0 + hits[i]] < sum(costs[a][b] for _, a, b in probe):
                ops.append(("s", K[i], CACHE0 + hits[i]))
            else: ops.extend(probe)
    return ops


# ------------------------------------------------------------------ tables with escaped entries
def count_symbols(parts):
    counts = np.zeros((CONTEXTS, GREEN_SYMBOLS), np.int64)
    for ops in parts:
        for kind, a, b in ops:
            if kind == "s": counts[a, b] += 1
    return counts


def bit_costs(counts):
    out = []
    for k in range(CONTEXTS):
        c = [UNSEEN] * GREEN_SYMBOLS; total = int(counts[k].sum())
        if total:
            freq = normalize(quantize_full(counts[k]))
            for sym, f in enumerate(freq):
                if f: c[sym] = -math.log2(f / TOTAL)
        out.append(c)
    return out


def quantize_full(vector):
    """q bytes of every entry (0 for unused), as the context-coded reference quantizes, without trimming."""
    total = int(np.sum(vector)); out = bytearray()
    for c in vector:
        if c == 0 or total == 0: out.append(0); continue
        f = c * TOTAL / total
        out.append(1 + (min(24, max(0, round(2 * math.log2(f)))) if f >= 1 else 0))
    return bytes(out)


class Table(M.Table):
    def __init__(self, q):
        super().__init__(normalize(q)); self.escape = len(q) - 1


def escape_tables(counts):
    """Per context, the q bytes of the listed symbols followed by the escape entry, and the listed symbol count L."""
    qs, listed = [], []
    for k in range(CONTEXTS):
        row = counts[k]; keep = np.nonzero(row > ESCAPE_MAX)[0]
        L = int(keep.max()) + 1 if keep.size else 0
        vector = np.where(row[:L] > ESCAPE_MAX, row[:L], 0)
        escaped = int(row.sum() - vector.sum())
        qs.append(quantize_full(np.append(vector, escaped))); listed.append(L)
    return qs, listed


def escape_ops(ops, counts, listed):
    out = []
    for kind, a, b in ops:
        if kind == "s" and (b >= listed[a] or counts[a, b] <= ESCAPE_MAX):
            out.append(("s", a, listed[a])); out.append(("b", raw_bits(a), b))
        else: out.append((kind, a, b))
    return out


def serialize_tables(qs, z):
    return z.compress(b"".join(struct.pack(">H", len(q) - 1) + q for q in qs))


def parse_tables(blob, z):
    body = z.decompress(blob, CONTEXTS * (3 + GREEN_SYMBOLS)); pos = 0; qs = []
    for _ in range(CONTEXTS):
        if pos + 2 > len(body): raise ValueError("tables")
        n = struct.unpack(">H", body[pos:pos + 2])[0]; pos += 2
        if n > GREEN_SYMBOLS or pos + n + 1 > len(body): raise ValueError("tables")
        qs.append(body[pos:pos + n + 1]); pos += n + 1
    if pos != len(body): raise ValueError("tables")
    return qs


# ------------------------------------------------------------------ tile decode (sequential)
def decode_tile(payload, w, h, C, tables):
    d = Decoder(payload); n = w * h
    s = [[[0] * w for _ in range(h)] for _ in range(C)]
    R = [[0] * n for _ in range(C)]
    e = [[0] * w for _ in range(h)]
    special = specials(w); repeats = initial_repeats(w); copy_left = 0; copy_d = 0
    cache = [-1] * CACHE_SIZE

    def symbol(context):
        t = tables[context]; v = d.decode(t)
        return d.bits(raw_bits(context)) if v == t.escape else v

    for y in range(h):
        ey = e[y]; ey1 = e[y - 1] if y >= 1 else None; ey2 = e[y - 2] if y >= 2 else None
        for x in range(w):
            i = y * w + x; colour = None
            if copy_left:
                res = [R[c][i - copy_d] for c in range(C)]; copy_left -= 1
            else:
                A = (ey[x - 1] if x >= 1 else 0) + (ey[x - 2] if x >= 2 else 0)
                if ey1 is not None: A += ey1[x] + (ey1[x - 1] if x >= 1 else 0) + (ey1[x + 1] if x + 1 < w else 0)
                if ey2 is not None: A += ey2[x]
                k = (A > 0) + (A > 3) + (A > 12) + (A > 48)
                gs = symbol(k)
                if gs >= CACHE0:
                    colour = cache[gs - CACHE0]
                    if colour < 0: raise ValueError("empty cache slot")
                    res = None
                elif gs >= 256:
                    code = gs - 256; length = MINLEN + unprefix(code, d.bits(code_bits(code)))
                    ds = symbol(DISTANCE)
                    if ds < NREPEAT: dist = repeats[ds]
                    elif ds < NREPEAT + NSPECIAL: dist = special[ds - NREPEAT]
                    else:
                        code = ds - NREPEAT - NSPECIAL
                        if code >= NCODES: raise ValueError("distance code")
                        dist = 1 + unprefix(code, d.bits(code_bits(code)))
                    if dist < 1 or dist > i or length > n - i: raise ValueError("match extent")
                    update_repeats(repeats, dist)
                    copy_d = dist; copy_left = length - 1
                    res = [R[c][i - dist] for c in range(C)]
                else:
                    g = unzig(gs); gb = 0 if g == 0 else (1 if -2 <= g <= 2 else 2)
                    red = unzig(symbol(RED0 + 3 * k + gb)); blue = unzig(symbol(BLUE0 + 3 * k + gb))
                    if k >= GATE: red = wrap(red + g); blue = wrap(blue + g)
                    res = [red, g, blue]
                    if C == 4:
                        sa = s[3]; up = sa[y - 1][x] if y >= 1 else 0
                        left = sa[y][x - 1] if x >= 1 else up; ul = sa[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                        res.append(unzig(symbol(ALPHA0 + (0 if left == up == ul else 1))))
            energy = 0; key = 0
            for c in range(C):
                sc = s[c]
                up = sc[y - 1][x] if y >= 1 else 0
                left = sc[y][x - 1] if x >= 1 else up
                ul = sc[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                hi = left if left > up else up; lo = up if left > up else left
                m = lo if ul >= hi else (hi if ul <= lo else left + up - ul)
                if colour is not None:
                    v = (colour >> (8 * c)) & 255; r = wrap(v - m)
                else:
                    r = res[c]; v = (m + r) & 255
                sc[y][x] = v; R[c][i] = r; key |= v << (8 * c)
                if c < 3:
                    a = r if r >= 0 else -r
                    if a > energy: energy = a
            ey[x] = energy
            cache[slot_of(key)] = key
    if copy_left or d.pos != len(d.data) or d.x != LOWER: raise ValueError("rANS stream extent")
    return np.stack([np.array(s[c], np.uint8) for c in range(C)], axis=2)


# ------------------------------------------------------------------ image container
def encode(img, z=None, detail=None, tables_once=True):
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if C not in (3, 4): raise ValueError("channels")
    z = z or Zstd()
    tiles = [img[y0:y1, x0:x1] for (y0, y1, x0, x1) in tile_grid(W, H)] + [block_means(img)]
    costs = bit_costs(count_symbols([tile_ops_greedy(t) for t in tiles]))
    parts = [tile_ops_costed(t, costs) for t in tiles]
    counts = count_symbols(parts)
    qs, listed = escape_tables(counts)
    tables = [Table(q) for q in qs]
    parts = [escape_ops(ops, counts, listed) for ops in parts]
    blob = serialize_tables(qs, z)
    payloads = [rans_encode(ops, tables) for ops in parts[:-1]]
    mosaic = rans_encode(parts[-1], tables)
    fields = struct.pack(">IIBBIIII", W, H, C, TILE, len(mosaic), crc(mosaic), len(blob), crc(blob)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    magic = MAGIC if tables_once else MAGIC_BOTH
    second = b"" if (tables_once and len(payloads) == 1) else blob
    if detail is not None:
        flat = [op for ops in parts[:-1] for op in ops]
        detail.update(tables=len(blob), mosaic=len(mosaic), payload=sum(len(p) for p in payloads),
                      cached=sum(1 for kind, a, b in flat if kind == "s" and a < RED0 and CACHE0 <= b < listed[a]),
                      escapes=sum(1 for kind, a, b in flat if kind == "s" and b == listed[a]))
    return magic + fields + directory + blob + mosaic + b"".join(payloads) + mosaic + second + directory + fields


def _valid(block, body):
    return len(block) >= body + 4 and struct.unpack(">I", block[body:body + 4])[0] == crc(block[:body])


def parse(data, z, table_reader=None):
    """`table_reader(blob)` builds the tables from the tables blob (default: this stream's own format)."""
    if data[:8] not in (MAGIC, MAGIC_BOTH) or len(data) < 72: raise ValueError("signature")
    front, tail = data[8:38], data[-30:]
    fields = front if _valid(front, 26) else tail if _valid(tail, 26) else None
    if fields is None: raise ValueError("header: both copies damaged")
    W, H, C, side, mlen, mcrc, tlen, tcrc = struct.unpack(">IIBBIIII", fields[:26])
    if W == 0 or H == 0 or C not in (3, 4) or side != TILE: raise ValueError("header fields")
    grid = tile_grid(W, H, side); n = len(grid); block = 8 * n + 4
    copies = 1 if (data[:8] == MAGIC and n == 1) else 2
    if len(data) < 38 + 2 * block + 2 * mlen + copies * tlen + 30: raise ValueError("extent")
    first, mirror = data[38:38 + block], data[len(data) - 30 - block:len(data) - 30]
    directory = first if _valid(first, 8 * n) else mirror if _valid(mirror, 8 * n) else None
    if directory is None: raise ValueError("directory: both copies damaged")
    entries = [struct.unpack(">II", directory[8 * i:8 * i + 8]) for i in range(n)]
    second = tlen if copies == 2 else 0
    end = len(data) - 30 - block - second - mlen; start = 38 + block + tlen + mlen
    if start > end: raise ValueError("payload extent")
    candidates = [data[38 + block:38 + block + tlen]] + ([data[end + mlen:end + mlen + tlen]] if copies == 2 else [])
    blob = next((b for b in candidates if crc(b) == tcrc), None)
    if blob is None: raise ValueError("tables: damaged")
    tables = table_reader(blob) if table_reader else [Table(q) for q in parse_tables(blob, z)]
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
