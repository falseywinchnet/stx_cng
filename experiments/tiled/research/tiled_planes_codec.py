"""CNGM0007: per-plane entropy streams. Each tile carries one rANS stream per plane: green decodes alone; red, blue and
alpha each need only green's decoded planes and their own, so they can be decoded by separate workers once green is
done (and reconstruction follows as in CNGM0006).

Built on CNGM0006 (research/tiled_stream_codec.py: colour cache resolved in reconstruction, cached pixels zero in the
residual domain, alpha context from residuals). Changes:
  - Activity classes per plane: green's class comes from green's residual energies alone (|r_g| over the six causal
    neighbours, the same edges); red's from max(|r_g|, |r_r|); blue's from max(|r_g|, |r_b|). Red and blue use their
    own class for the colour gate (red minus green on busy pixels) and in their contexts with the green bucket.
  - Whole-pixel matches and colour-cache indices are signalled in the green stream only; the other planes read green's
    per-pixel map of them (a whole-matched pixel copies every plane from the same distance; a cached pixel has zero
    residual in every plane).
  - Per-plane matches: each plane's own matches live in its own stream (green's in green's).
Tile payload: the lengths of the first C - 1 streams as LEB128, then the C rANS streams (green, red, blue, alpha).
Container, tables (one shared set; the same 38 contexts and alphabets) and the mosaic as CNGM0006, signature
"CNGM0007". Exact, allocating Python reference; sizes and correctness only."""
import struct
import numpy as np
from research.tiled_zstd_codec import CELL, EDGES, GATE, TILE, Zstd, block_means, crc, median_edge, neighbours, tile_grid, wrap
from research.tiled_context_codec import ALPHA0, BLUE0, LOWER, RED0, unzig
from research import tiled_cache_codec as Q
from research import tiled_plane_codec as P
from research.tiled_match_codec import (DISTANCE, MINLEN, NREPEAT, NSPECIAL, NCODES, Decoder, code_bits, initial_repeats,
                                        prefix, rans_encode, specials, unprefix, update_repeats)
from research.tiled_cache_codec import CACHE0, CACHE_SIZE, cache_hits, slot_of

MAGIC = b"CNGM0007"
GREEN_ONLY0 = P.GREEN_ONLY0
ORDER = (1, 0, 2, 3)


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


def classes(energy):
    h, w = energy.shape
    E = np.zeros((h + 2, w + 4), np.int32); E[2:, 2:w + 2] = energy
    A = E[2:, 1:w + 1] + E[2:, 0:w] + E[1:h + 1, 2:w + 2] + E[0:h, 2:w + 2] + E[1:h + 1, 1:w + 1] + E[1:h + 1, 3:w + 3]
    return sum((A > edge).astype(np.int32) for edge in EDGES)


def view_of(tile, cached=None):
    h, w, C = tile.shape; n = h * w
    s = tile.astype(np.int32)
    L, U, UL, UR = neighbours(s)
    r = wrap(s - median_edge(L, U, UL))
    if cached is not None:
        r = r.copy(); r[np.array(cached, bool).reshape(h, w)] = 0
    ag = np.abs(r[:, :, 1]); ar = np.abs(r[:, :, 0]); ab = np.abs(r[:, :, 2])
    kg = classes(ag).reshape(-1).tolist(); kr = classes(np.maximum(ag, ar)).reshape(-1).tolist(); kb = classes(np.maximum(ag, ab)).reshape(-1).tolist()
    R = r.reshape(-1, C).tolist()
    packed = ((r.reshape(-1, C) + 128).astype(np.int64) @ (np.int64(256) ** np.arange(C, dtype=np.int64))).tolist()
    quiet = [True] * n
    if C == 4:
        a = r[:, :, 3]
        La = np.zeros_like(a); La[:, 1:] = a[:, :-1]
        Ua = np.zeros_like(a); Ua[1:] = a[:-1]
        ULa = np.zeros_like(a); ULa[1:, 1:] = a[:-1, :-1]
        quiet = ((La == 0) & (Ua == 0) & (ULa == 0)).reshape(-1).tolist()
    planes = [[R[i][c] for i in range(n)] for c in range(C)]
    contexts = [[0] * n for _ in range(C)]; literals = [[0] * n for _ in range(C)]
    for i in range(n):
        g = R[i][1]; red, blue = R[i][0], R[i][2]
        if kr[i] >= GATE: red = wrap(red - g)
        if kb[i] >= GATE: blue = wrap(blue - g)
        gb = 0 if g == 0 else (1 if -2 <= g <= 2 else 2)
        contexts[1][i] = kg[i]; literals[1][i] = P.zig(g)
        contexts[0][i] = RED0 + 3 * kr[i] + gb; literals[0][i] = P.zig(red)
        contexts[2][i] = BLUE0 + 3 * kb[i] + gb; literals[2][i] = P.zig(blue)
        if C == 4: contexts[3][i] = ALPHA0 + (0 if quiet[i] else 1); literals[3][i] = P.zig(R[i][3])
    return dict(h=h, w=w, C=C, n=n, packed=packed, planes=planes, contexts=contexts, literals=literals, hits=cache_hits(tile))


def plan(tile, costs):
    """As CNGM0006: CNGM0005's decisions on the true residuals, cached pixels' residuals at zero, whole-pixel matches
    cut where they no longer reproduce, per-plane parses with cached and whole-matched pixels as stops."""
    first = view_of(tile)
    whole, cached, _ = P.plan(first, costs)
    n, w, C = first["n"], first["w"], first["C"]
    view = view_of(tile, cached)
    S = view["packed"]; kept = []
    for i, d, L in whole:
        length = 0
        while length < L and S[i + length] == S[i + length - d]: length += 1
        if length >= P.NEWLEN or (length >= P.MINLEN and length == L): kept.append((i, d, length))
    stop = list(cached)
    for i, d, L in kept:
        for j in range(i, i + L): stop[j] = True
    plane_lit = [[costs[view["contexts"][c][i]][view["literals"][c][i]] for i in range(n)] for c in range(C)]
    per_plane = []
    for c in range(C):
        lengths = [costs[view["contexts"][c][i]][P.plane_length0(c):P.plane_length0(c) + NCODES] for i in range(n)]
        matches, _ = P.costed_parse(view["planes"][c], n, w, [0.0 if stop[i] else plane_lit[c][i] for i in range(n)], lengths, costs[DISTANCE], stop)
        per_plane.append(matches)
    return view, kept, cached, per_plane


def emit_streams(view, whole, cached, per_plane):
    """The ops of each plane's stream, in plane index order (red, green, blue, alpha)."""
    n, w, C = view["n"], view["w"], view["C"]
    contexts, literals, hits = view["contexts"], view["literals"], view["hits"]
    special = specials(w)
    whole_at = {i: (d, L) for i, d, L in whole}
    covered = [False] * n
    for i, d, L in whole:
        for j in range(i, i + L): covered[j] = True
    streams = [[] for _ in range(C)]

    def distance_ops(ops, d, reps):
        if d in reps: ops.append(("s", DISTANCE, reps.index(d)))
        elif d in special: ops.append(("s", DISTANCE, NREPEAT + special.index(d)))
        else:
            code, bits, extra = prefix(d - 1)
            ops.append(("s", DISTANCE, NREPEAT + NSPECIAL + code))
            if bits: ops.append(("b", bits, extra))
        update_repeats(reps, d)

    for c in range(C):
        ops = streams[c]; reps = initial_repeats(w); whole_reps = initial_repeats(w); until = 0
        plane_at = {i: (d, L) for i, d, L in per_plane[c]}
        for i in range(n):
            if c == 1 and i in whole_at:
                d, L = whole_at[i]; code, bits, extra = prefix(L - MINLEN)
                ops.append(("s", contexts[1][i], 256 + code))
                if bits: ops.append(("b", bits, extra))
                distance_ops(ops, d, whole_reps); continue
            if covered[i]: continue
            if cached[i]:
                if c == 1: ops.append(("s", contexts[1][i], CACHE0 + hits[i]))
                continue
            if until > i: continue
            if i in plane_at:
                d, L = plane_at[i]; code, bits, extra = prefix(L - MINLEN)
                ops.append(("s", contexts[c][i], P.plane_length0(c) + code))
                if bits: ops.append(("b", bits, extra))
                distance_ops(ops, d, reps); until = i + L
            else:
                ops.append(("s", contexts[c][i], literals[c][i]))
    return streams


def pack_streams(streams, tables, C):
    order = [c for c in ORDER if c < C]
    payloads = [rans_encode(streams[c], tables) for c in order]
    return b"".join(leb128(len(p)) for p in payloads[:-1]) + b"".join(payloads)


# ------------------------------------------------------------------ decoding, plane by plane
class PlaneDecoder:
    def __init__(self, data, tables, w):
        self.d = Decoder(data); self.tables = tables; self.special = specials(w)

    def symbol(self, context):
        t = self.tables[context]; v = self.d.decode(t)
        return self.d.bits(P.raw_bits(context)) if v == t.escape else v

    def match(self, code, reps, i, n):
        length = MINLEN + unprefix(code, self.d.bits(code_bits(code)))
        ds = self.symbol(DISTANCE)
        if ds < NREPEAT: dd = reps[ds]
        elif ds < NREPEAT + NSPECIAL: dd = self.special[ds - NREPEAT]
        else:
            code = ds - NREPEAT - NSPECIAL
            if code >= NCODES: raise ValueError("distance code")
            dd = 1 + unprefix(code, self.d.bits(code_bits(code)))
        if dd < 1 or dd > i or length > n - i: raise ValueError("match extent")
        update_repeats(reps, dd)
        return dd, length

    def finish(self):
        if self.d.pos != len(self.d.data) or self.d.x != LOWER: raise ValueError("rANS stream extent")


def plane_class(e, i, x, y, w):
    A = (e[i - 1] if x >= 1 else 0) + (e[i - 2] if x >= 2 else 0)
    if y >= 1: A += e[i - w] + (e[i - w - 1] if x >= 1 else 0) + (e[i - w + 1] if x + 1 < w else 0)
    if y >= 2: A += e[i - 2 * w]
    return (A > 0) + (A > 3) + (A > 12) + (A > 48)


def decode_green(data, w, h, tables):
    """Green worker: green residuals, the whole-match distance of every pixel (0 for none) and the cache slots."""
    n = w * h; dec = PlaneDecoder(data, tables, w)
    g = [0] * n; eg = [0] * n; whole = [0] * n; slots = [-1] * n
    reps = initial_repeats(w); whole_reps = initial_repeats(w); left = 0; dist = 0; wleft = 0; wd = 0
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if wleft:
                g[i] = g[i - wd]; whole[i] = wd; wleft -= 1
            elif left:
                g[i] = g[i - dist]; left -= 1
            else:
                k = plane_class(eg, i, x, y, w)
                gs = dec.symbol(k)
                if 256 <= gs < CACHE0:
                    wd, L = dec.match(gs - 256, whole_reps, i, n); wleft = L - 1; g[i] = g[i - wd]; whole[i] = wd
                elif CACHE0 <= gs < GREEN_ONLY0:
                    slots[i] = gs - CACHE0; g[i] = 0
                elif gs >= GREEN_ONLY0:
                    dist, L = dec.match(gs - GREEN_ONLY0, reps, i, n); left = L - 1; g[i] = g[i - dist]
                else:
                    g[i] = unzig(gs)
            eg[i] = abs(g[i])
    if wleft or left: raise ValueError("match past the tile")
    dec.finish()
    return g, whole, slots


def decode_other(c, data, w, h, tables, g, whole, slots):
    """Red, blue or alpha worker: needs green's residuals, whole-match distances and cache slots, and its own plane."""
    n = w * h; dec = PlaneDecoder(data, tables, w)
    r = [0] * n; e = [0] * n; reps = initial_repeats(w); left = 0; dist = 0
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if whole[i]: r[i] = r[i - whole[i]]
            elif slots[i] >= 0: r[i] = 0
            elif left: r[i] = r[i - dist]; left -= 1
            else:
                if c == 3:
                    quiet = (r[i - 1] if x >= 1 else 0) == 0 and (r[i - w] if y >= 1 else 0) == 0 and (r[i - w - 1] if (x >= 1 and y >= 1) else 0) == 0
                    ctx = ALPHA0 + (0 if quiet else 1); k = 0
                else:
                    k = plane_class(e, i, x, y, w)
                    gb = 0 if g[i] == 0 else (1 if -2 <= g[i] <= 2 else 2)
                    ctx = (RED0 if c == 0 else BLUE0) + 3 * k + gb
                v = dec.symbol(ctx)
                if v >= 256:
                    dist, L = dec.match(v - 256, reps, i, n); left = L - 1; r[i] = r[i - dist]
                else:
                    v = unzig(v); r[i] = wrap(v + g[i]) if (c != 3 and k >= GATE) else v
            e[i] = max(abs(g[i]), abs(r[i]))
    if left: raise ValueError("match past the tile")
    dec.finish()
    return r


def decode_tile(payload, w, h, C, tables):
    order = [c for c in ORDER if c < C]
    pos = 0; lengths = []
    for _ in range(len(order) - 1):
        L, pos = read_leb128(payload, pos); lengths.append(L)
    rest = len(payload) - pos - sum(lengths)
    if rest < 0: raise ValueError("stream lengths")
    lengths.append(rest)
    datas = {}
    for c, L in zip(order, lengths):
        datas[c] = payload[pos:pos + L]; pos += L
    g, whole, slots = decode_green(datas[1], w, h, tables)
    planes = {1: g}
    for c in order[1:]:
        planes[c] = decode_other(c, datas[c], w, h, tables, g, whole, slots)
    # reconstruction: the cached colour, or MED plus residual (as CNGM0006)
    s = [[[0] * w for _ in range(h)] for _ in range(C)]
    cache = [-1] * CACHE_SIZE
    for y in range(h):
        for x in range(w):
            i = y * w + x; key = 0; colour = cache[slots[i]] if slots[i] >= 0 else None
            if slots[i] >= 0 and colour < 0: raise ValueError("empty cache slot")
            for c in range(C):
                sc = s[c]; up = sc[y - 1][x] if y >= 1 else 0
                lf = sc[y][x - 1] if x >= 1 else up; ul = sc[y - 1][x - 1] if (y >= 1 and x >= 1) else up
                hi = lf if lf > up else up; lo = up if lf > up else lf
                m = lo if ul >= hi else (hi if ul <= lo else lf + up - ul)
                v = ((colour >> (8 * c)) & 255) if colour is not None else (m + planes[c][i]) & 255
                sc[y][x] = v; key |= v << (8 * c)
            cache[slot_of(key)] = key
    return np.stack([np.array(s[c], np.uint8) for c in range(C)], axis=2)


# ------------------------------------------------------------------ container (as CNGM0006)
def encode(img, z=None, detail=None):
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if C not in (3, 4): raise ValueError("channels")
    z = z or Zstd()
    tiles = [img[y0:y1, x0:x1] for (y0, y1, x0, x1) in tile_grid(W, H)] + [block_means(img)]
    costs = P.bit_costs(P.count_symbols([Q.tile_ops_greedy(t) for t in tiles]))
    for _ in range(2):
        plans = [plan(t, costs) for t in tiles]
        streams = [emit_streams(*p) for p in plans]
        costs = P.bit_costs(P.count_symbols([sum(s, []) for s in streams]))
    counts = P.count_symbols([sum(s, []) for s in streams])
    qs, listed = P.escape_tables(counts)
    tables = [P.Table(q) for q in qs]
    streams = [[P.escape_ops(st, counts, listed) for st in s] for s in streams]
    blob = P.serialize_tables(qs, z)
    payloads = [pack_streams(s, tables, C) for s in streams[:-1]]
    mosaic = pack_streams(streams[-1], tables, C)
    fields = struct.pack(">IIBBIIII", W, H, C, TILE, len(mosaic), crc(mosaic), len(blob), crc(blob)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    second = b"" if len(payloads) == 1 else blob
    if detail is not None:
        detail.update(tables=len(blob), mosaic=len(mosaic), payload=sum(len(x) for x in payloads),
                      whole_matches=int(sum(len(p[1]) for p in plans[:-1])), cached=int(sum(sum(bool(v) for v in p[2]) for p in plans[:-1])),
                      plane_matches=int(sum(sum(len(m) for m in p[3]) for p in plans[:-1])))
    return MAGIC + fields + directory + blob + mosaic + b"".join(payloads) + mosaic + second + directory + fields


def parse(data, z):
    if data[:8] != MAGIC: raise ValueError("signature")
    return Q.parse(Q.MAGIC + data[8:], z, table_reader=lambda blob: [P.Table(q) for q in P.parse_tables(blob, z)])


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
