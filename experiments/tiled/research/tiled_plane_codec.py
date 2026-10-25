"""Tiled context-coded reference with whole-pixel and per-plane matches (stream CNGM0005).

CNGM0004 (research/tiled_cache_codec.py) matches whole pixels: one match copies the residuals of every channel at
one distance. On smooth graphics each channel's residual pattern repeats with its own period, so whole-pixel matches
are short and their distances vary; zstd matching each plane on its own wins there by about 2x
(research/diagnose_smooth_layouts.py). Per-plane matches alone lose badly elsewhere (a repeated pixel costs one match
per plane, and they crowd out the colour cache), so here both exist.

Per pixel, in raster order, with the contexts of CNGM0004 (they depend only on residual values, so they are the same
however a pixel is coded):
  - Inside a whole-pixel match every plane copies.
  - Otherwise planes inside a per-plane match copy, and each other plane, in the order green, red, blue, alpha, codes
    one symbol in its context. Green's alphabet is 256 literals, 30 whole-pixel match lengths, 256 colour cache
    indices and 30 green-only match lengths (572); the whole-pixel lengths and the cache indices are used only when no
    plane is inside a match. Red, blue and alpha have 256 literals and 30 lengths of a match of that plane alone.
  - A match of L = MINLEN + v pixels (v prefix-coded with extra bits) copies from d pixels back in the tile, d from
    the shared distance table: three repeat distances (kept for whole-pixel matches and for each plane separately),
    six fixed offsets, prefix codes with extra bits.
Escaped table entries (raw bits 10 for green, 9 for red, blue and alpha, 6 for distances), colour cache, mosaic,
duplicated index and single-copy tables on one-tile images as CNGM0004.

Encoder parse (not part of the format), per tile: each plane is first parsed by cost on its own over the whole
tile and the cost of that plane-wise route is shared over the pixels it covers; the cost-aware whole-pixel parse of
CNGM0004 then takes a whole-pixel match or a cached pixel only where it is cheaper than the plane-wise route; finally
each plane is parsed again over the pixels left to it. Code lengths come from a greedy CNGM0004 pass and are re-estimated once from
a first costed pass. Exact, allocating Python reference; sizes and correctness only."""
import struct
import numpy as np
from research.tiled_zstd_codec import CELL, GATE, TILE, Zstd, block_means, crc, tile_grid, wrap
from research.tiled_context_codec import ALPHA0, BLUE0, LOWER, RED0, normalize, unzig
from research import tiled_cache_codec as Q
from research.tiled_match_codec import (CHAIN, DISTANCE, MAXLEN, MINLEN, NEWLEN, NREPEAT, NSPECIAL, NCODES, SUFFICIENT,
                                        TRIAL_LENGTHS, UNSEEN, Decoder, analyse, code_bits, initial_repeats, prefix,
                                        rans_encode, specials, unprefix, update_repeats)
from research.tiled_cache_codec import CACHE0, CACHE_SIZE, cache_hits, slot_of, quantize_full

MAGIC = b"CNGM0005"
GREEN_ONLY0 = CACHE0 + CACHE_SIZE
GREEN_SYMBOLS = GREEN_ONLY0 + NCODES
CONTEXTS = DISTANCE + 1
ESCAPE_MAX = 2
PLANE_ORDER = (1, 0, 2, 3)   # green, red, blue, alpha
INF = float("inf")


def raw_bits(context):
    return 10 if context < RED0 else 6 if context == DISTANCE else 9


def zig(v):
    return 2 * v if v >= 0 else -2 * v - 1


def plane_length0(c):
    return GREEN_ONLY0 if c == 1 else 256


# ------------------------------------------------------------------ encoder view of a tile
def view_of(tile):
    h, w, C = tile.shape; n = h * w
    r, cls, quiet = analyse(tile)
    R = r.tolist(); K = cls.tolist(); Qa = quiet.tolist()
    packed = ((r + 128).astype(np.int64) @ (np.int64(256) ** np.arange(C, dtype=np.int64))).tolist()
    planes = [[R[i][c] for i in range(n)] for c in range(C)]
    contexts = [[0] * n for _ in range(C)]; literals = [[0] * n for _ in range(C)]
    for i in range(n):
        k = K[i]; g = R[i][1]; red, blue = R[i][0], R[i][2]
        if k >= GATE: red = wrap(red - g); blue = wrap(blue - g)
        gb = 0 if g == 0 else (1 if -2 <= g <= 2 else 2)
        contexts[1][i] = k; literals[1][i] = zig(g)
        contexts[0][i] = RED0 + 3 * k + gb; literals[0][i] = zig(red)
        contexts[2][i] = BLUE0 + 3 * k + gb; literals[2][i] = zig(blue)
        if C == 4: contexts[3][i] = ALPHA0 + (0 if Qa[i] else 1); literals[3][i] = zig(R[i][3])
    return dict(h=h, w=w, C=C, n=n, packed=packed, planes=planes, contexts=contexts, literals=literals, hits=cache_hits(tile))


def chain_insert(heads, S, n, j):
    if j + 4 <= n:
        key = (S[j], S[j + 1], S[j + 2], S[j + 3]); chain = heads.get(key)
        if chain is None: heads[key] = [j]
        else:
            chain.append(j)
            if len(chain) > 2 * CHAIN: del chain[:CHAIN]


def candidates_at(i, n, S, reps, special, heads):
    out = []
    for d in tuple(reps) + special:
        if 1 <= d <= i and d not in out: out.append(d)
    if i + 4 <= n:
        chain = heads.get((S[i], S[i + 1], S[i + 2], S[i + 3]))
        if chain:
            for j in reversed(chain[-CHAIN:]):
                if i - j not in out: out.append(i - j)
    return out


def costed_parse(S, n, w, lit, length_costs, distance, stop):
    """Cheapest parse of the sequence S: lit[i] the cost of position i without a match (0 and no match allowed where
    stop[i] says the position is coded otherwise), length_costs[i] the code lengths of the length symbols at i (a list
    indexed by length code), distance the distance table's code lengths. Returns the matches (i, d, L)."""
    special = specials(w)
    limit_at = [n] * (n + 1); nxt = n
    for i in range(n - 1, -1, -1):
        if stop[i]: nxt = i
        limit_at[i] = nxt
    price = [INF] * (n + 1); price[0] = 0.0
    back = [None] * (n + 1); state = [None] * (n + 1); state[0] = tuple(initial_repeats(w))
    heads = {}; i = 0
    while i < n:
        base = price[i]; reps = state[i]
        if base + lit[i] < price[i + 1]:
            price[i + 1] = base + lit[i]; back[i + 1] = (i, 0, 1); state[i + 1] = reps
        if stop[i]:
            chain_insert(heads, S, n, i); i += 1; continue
        longest = 0; lcost = length_costs[i]; cap = min(limit_at[i] - i, MAXLEN)
        for d in candidates_at(i, n, S, reps, special, heads):
            if S[i] != S[i - d]: continue
            L = 1; lim = min(cap, SUFFICIENT)
            while L < lim and S[i + L] == S[i + L - d]: L += 1
            if L == SUFFICIENT:
                while L < cap and S[i + L] == S[i + L - d]: L += 1
            repeat = d in reps; shortest = MINLEN if repeat else NEWLEN
            if L < shortest: continue
            if repeat: dcost = distance[reps.index(d)]
            elif d in special: dcost = distance[NREPEAT + special.index(d)]
            else:
                code, bits, _ = prefix(d - 1); dcost = distance[NREPEAT + NSPECIAL + code] + bits
            moved = list(reps); update_repeats(moved, d); moved = tuple(moved)
            for trial in set([L] + [t for t in TRIAL_LENGTHS if shortest <= t < L]):
                code, bits, _ = prefix(trial - MINLEN)
                v = base + lcost[code] + bits + dcost
                if v < price[i + trial]: price[i + trial] = v; back[i + trial] = (i, d, trial); state[i + trial] = moved
            if L > longest: longest = L
        if longest >= SUFFICIENT:
            for j in range(i, i + longest): chain_insert(heads, S, n, j)
            i += longest; continue
        chain_insert(heads, S, n, i); i += 1
    matches = []; amortized = [0.0] * n; j = n
    while j > 0:
        i, d, L = back[j]
        if d: matches.append((i, d, L))
        share = (price[j] - price[i]) / (j - i)
        for q in range(i, j): amortized[q] = share
        j = i
    matches.reverse()
    return matches, amortized


def plan(view, costs):
    """Whole-pixel matches, cached pixels and per-plane matches of one tile under `costs`."""
    n, w, C = view["n"], view["w"], view["C"]
    contexts, literals, hits = view["contexts"], view["literals"], view["hits"]
    plane_lit = [[costs[contexts[c][i]][literals[c][i]] for i in range(n)] for c in range(C)]
    pixel_lit = [sum(plane_lit[c][i] for c in range(C)) for i in range(n)]
    cache_cost = [costs[contexts[1][i]][CACHE0 + hits[i]] if hits[i] >= 0 else INF for i in range(n)]
    plane_lengths = [[costs[contexts[c][i]][plane_length0(c):plane_length0(c) + NCODES] for i in range(n)] for c in range(C)]
    # the plane-wise route first: each plane parsed on its own over the whole tile, its cost shared over the pixels
    open_ = [False] * n
    plane_route = [0.0] * n
    for c in range(C):
        _, amortized = costed_parse(view["planes"][c], n, w, plane_lit[c], plane_lengths[c], costs[DISTANCE], open_)
        for i in range(n): plane_route[i] += amortized[i]
    # a whole-pixel match or a cached pixel wins a pixel only where it is cheaper than the plane-wise route
    lit = [min(plane_route[i], cache_cost[i]) for i in range(n)]
    green_lengths = [costs[contexts[1][i]][256:256 + NCODES] for i in range(n)]
    whole, _ = costed_parse(view["packed"], n, w, lit, green_lengths, costs[DISTANCE], open_)
    covered = [False] * n
    for i, d, L in whole:
        for j in range(i, i + L): covered[j] = True
    cached = [not covered[i] and cache_cost[i] < plane_route[i] for i in range(n)]
    stop = [covered[i] or cached[i] for i in range(n)]
    per_plane = []
    for c in range(C):
        matches, _ = costed_parse(view["planes"][c], n, w, [0.0 if stop[i] else plane_lit[c][i] for i in range(n)],
                                  plane_lengths[c], costs[DISTANCE], stop)
        per_plane.append(matches)
    return whole, cached, per_plane


def emit(view, whole, cached, per_plane):
    n, w, C = view["n"], view["w"], view["C"]
    contexts, literals, hits = view["contexts"], view["literals"], view["hits"]
    special = specials(w); order = PLANE_ORDER if C == 4 else PLANE_ORDER[:3]
    whole_at = {i: (d, L) for i, d, L in whole}
    plane_at = [{i: (d, L) for i, d, L in per_plane[c]} for c in range(C)]
    whole_reps = initial_repeats(w); plane_reps = [initial_repeats(w) for _ in range(C)]
    whole_until = 0; plane_until = [0] * C; ops = []

    def distance_ops(d, reps):
        if d in reps: ops.append(("s", DISTANCE, reps.index(d)))
        elif d in special: ops.append(("s", DISTANCE, NREPEAT + special.index(d)))
        else:
            code, bits, extra = prefix(d - 1)
            ops.append(("s", DISTANCE, NREPEAT + NSPECIAL + code))
            if bits: ops.append(("b", bits, extra))
        update_repeats(reps, d)

    for i in range(n):
        if whole_until > i: continue
        idle = all(plane_until[c] <= i for c in range(C))
        if i in whole_at:
            if not idle: raise ValueError("parse: whole-pixel match over a plane match")
            d, L = whole_at[i]; code, bits, extra = prefix(L - MINLEN)
            ops.append(("s", contexts[1][i], 256 + code))
            if bits: ops.append(("b", bits, extra))
            distance_ops(d, whole_reps); whole_until = i + L; continue
        if cached[i]:
            if not idle: raise ValueError("parse: cached pixel inside a plane match")
            ops.append(("s", contexts[1][i], CACHE0 + hits[i])); continue
        for c in order:
            if plane_until[c] > i: continue
            if i in plane_at[c]:
                d, L = plane_at[c][i]; code, bits, extra = prefix(L - MINLEN)
                ops.append(("s", contexts[c][i], plane_length0(c) + code))
                if bits: ops.append(("b", bits, extra))
                distance_ops(d, plane_reps[c]); plane_until[c] = i + L
            else:
                ops.append(("s", contexts[c][i], literals[c][i]))
    return ops


# ------------------------------------------------------------------ tables
def count_symbols(parts):
    counts = np.zeros((CONTEXTS, GREEN_SYMBOLS), np.int64)
    for ops in parts:
        for kind, a, b in ops:
            if kind == "s": counts[a, b] += 1
    return counts


def bit_costs(counts):
    out = []
    for k in range(CONTEXTS):
        c = [UNSEEN] * GREEN_SYMBOLS
        if counts[k].sum():
            for sym, f in enumerate(normalize(quantize_full(counts[k]))):
                if f: c[sym] = -np.log2(f / 4096)
        out.append(c)
    return out


def escape_tables(counts):
    qs, listed = [], []
    for k in range(CONTEXTS):
        row = counts[k]; keep = np.nonzero(row > ESCAPE_MAX)[0]
        L = int(keep.max()) + 1 if keep.size else 0
        vector = np.where(row[:L] > ESCAPE_MAX, row[:L], 0)
        qs.append(quantize_full(np.append(vector, int(row.sum() - vector.sum())))); listed.append(L)
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
    Rp = [[0] * n for _ in range(C)]
    e = [[0] * w for _ in range(h)]
    special = specials(w); order = PLANE_ORDER if C == 4 else PLANE_ORDER[:3]
    whole_reps = initial_repeats(w); plane_reps = [initial_repeats(w) for _ in range(C)]
    whole_left = 0; whole_d = 0; left = [0] * C; dist = [0] * C
    cache = [-1] * CACHE_SIZE

    def symbol(context):
        t = tables[context]; v = d.decode(t)
        return d.bits(raw_bits(context)) if v == t.escape else v

    def match(code, reps, i):
        length = MINLEN + unprefix(code, d.bits(code_bits(code)))
        ds = symbol(DISTANCE)
        if ds < NREPEAT: dd = reps[ds]
        elif ds < NREPEAT + NSPECIAL: dd = special[ds - NREPEAT]
        else:
            code = ds - NREPEAT - NSPECIAL
            if code >= NCODES: raise ValueError("distance code")
            dd = 1 + unprefix(code, d.bits(code_bits(code)))
        if dd < 1 or dd > i or length > n - i: raise ValueError("match extent")
        update_repeats(reps, dd)
        return dd, length

    for y in range(h):
        ey = e[y]; ey1 = e[y - 1] if y >= 1 else None; ey2 = e[y - 2] if y >= 2 else None
        for x in range(w):
            i = y * w + x
            meds = []; quiet = True
            for c in range(C):
                sc = s[c]; up = sc[y - 1][x] if y >= 1 else 0
                lf = sc[y][x - 1] if x >= 1 else up; ul = sc[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                hi = lf if lf > up else up; lo = up if lf > up else lf
                meds.append(lo if ul >= hi else (hi if ul <= lo else lf + up - ul))
                if c == 3: quiet = lf == up == ul
            res = [None] * C; colour = None
            if whole_left:
                res = [Rp[c][i - whole_d] for c in range(C)]; whole_left -= 1
            else:
                A = (ey[x - 1] if x >= 1 else 0) + (ey[x - 2] if x >= 2 else 0)
                if ey1 is not None: A += ey1[x] + (ey1[x - 1] if x >= 1 else 0) + (ey1[x + 1] if x + 1 < w else 0)
                if ey2 is not None: A += ey2[x]
                k = (A > 0) + (A > 3) + (A > 12) + (A > 48)
                for c in range(C):
                    if left[c]: res[c] = Rp[c][i - dist[c]]; left[c] -= 1
                idle = all(v is None for v in res)
                g = res[1]
                for c in order:
                    if res[c] is not None: continue
                    if c == 1:
                        gs = symbol(k)
                        if 256 <= gs < CACHE0:
                            if not idle: raise ValueError("whole-pixel match inside a plane match")
                            whole_d, length = match(gs - 256, whole_reps, i); whole_left = length - 1
                            res = [Rp[q][i - whole_d] for q in range(C)]; break
                        if CACHE0 <= gs < GREEN_ONLY0:
                            if not idle: raise ValueError("cache inside a plane match")
                            colour = cache[gs - CACHE0]
                            if colour < 0: raise ValueError("empty cache slot")
                            break
                        if gs >= GREEN_ONLY0:
                            dist[1], length = match(gs - GREEN_ONLY0, plane_reps[1], i); left[1] = length - 1
                            res[1] = Rp[1][i - dist[1]]
                        else: res[1] = unzig(gs)
                        g = res[1]
                    else:
                        if c == 3: ctx = ALPHA0 + (0 if quiet else 1)
                        else:
                            gb = 0 if g == 0 else (1 if -2 <= g <= 2 else 2)
                            ctx = (RED0 if c == 0 else BLUE0) + 3 * k + gb
                        v = symbol(ctx)
                        if v >= 256:
                            dist[c], length = match(v - 256, plane_reps[c], i); left[c] = length - 1
                            res[c] = Rp[c][i - dist[c]]
                        else:
                            v = unzig(v); res[c] = wrap(v + g) if (c != 3 and k >= GATE) else v
            energy = 0; key = 0
            for c in range(C):
                if colour is not None:
                    v = (colour >> (8 * c)) & 255; r = wrap(v - meds[c])
                else:
                    r = res[c]; v = (meds[c] + r) & 255
                s[c][y][x] = v; Rp[c][i] = r; key |= v << (8 * c)
                if c < 3:
                    a = r if r >= 0 else -r
                    if a > energy: energy = a
            ey[x] = energy
            cache[slot_of(key)] = key
    if whole_left or any(left) or d.pos != len(d.data) or d.x != LOWER: raise ValueError("rANS stream extent")
    return np.stack([np.array(s[c], np.uint8) for c in range(C)], axis=2)


# ------------------------------------------------------------------ image container (as CNGM0004)
class Table(Q.Table):
    pass


def encode(img, z=None, detail=None):
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if C not in (3, 4): raise ValueError("channels")
    z = z or Zstd()
    tiles = [img[y0:y1, x0:x1] for (y0, y1, x0, x1) in tile_grid(W, H)] + [block_means(img)]
    views = [view_of(t) for t in tiles]
    costs = bit_costs(count_symbols([Q.tile_ops_greedy(t) for t in tiles]))
    for _ in range(2):
        plans = [plan(v, costs) for v in views]
        parts = [emit(v, *p) for v, p in zip(views, plans)]
        costs = bit_costs(count_symbols(parts))
    counts = count_symbols(parts)
    qs, listed = escape_tables(counts)
    tables = [Table(q) for q in qs]
    parts = [escape_ops(ops, counts, listed) for ops in parts]
    blob = serialize_tables(qs, z)
    payloads = [rans_encode(ops, tables) for ops in parts[:-1]]
    mosaic = rans_encode(parts[-1], tables)
    fields = struct.pack(">IIBBIIII", W, H, C, TILE, len(mosaic), crc(mosaic), len(blob), crc(blob)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    second = b"" if len(payloads) == 1 else blob
    if detail is not None:
        p = plans[:-1]
        detail.update(tables=len(blob), mosaic=len(mosaic), payload=sum(len(x) for x in payloads),
                      whole_matches=int(sum(len(x[0]) for x in p)), cached=int(sum(sum(bool(v) for v in x[1]) for x in p)), plane_matches=int(sum(sum(len(m) for m in x[2]) for x in p)))
    return MAGIC + fields + directory + blob + mosaic + b"".join(payloads) + mosaic + second + directory + fields


def parse(data, z):
    """As CNGM0004's container; the tables use this stream's 572-symbol green alphabet."""
    if data[:8] != MAGIC: raise ValueError("signature")
    return Q.parse(Q.MAGIC + data[8:], z, table_reader=lambda blob: [Table(q) for q in parse_tables(blob, z)])


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
