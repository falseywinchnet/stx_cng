"""CNGM0008: per-plane entropy streams with a green-keyed colour cache that every plane worker resolves itself.

As CNGM0007 (research/tiled_planes_codec.py), every tile carries one rANS stream per plane; green decodes alone, and
red, blue and alpha each need only green's output and their own plane. Unlike CNGM0006 and CNGM0007, no residual is
set to zero: every plane worker reconstructs its own plane (MED on that plane only) as it decodes, so all residuals,
energies and matches are the true ones, as in CNGM0005.

The cache, per tile: for every green sample value, the last pixel (in raster order) with that green value. A pixel
whose colour equals that pixel's colour in every channel can be coded by a cache symbol in the green alphabet:
CACHE0 + zigzag(green residual). The green worker decodes the green residual as for a literal, reconstructs the green
sample, and names the source pixel j = last[green]; the other workers copy their sample from pixel j and derive their
residual from it. Every pixel updates last[its green value]. (CNGM0005's cache hashes the whole colour into 256 slots;
here the slot is the green value, so two colours with the same green value share a slot.)

Other details: per-plane activity classes (green from |r_g|, red from max(|r_g|, |r_r|), blue from max(|r_g|, |r_b|),
each over the six causal neighbours) as CNGM0007; the alpha context is CNGM0005's (the alpha samples of L, U, UL are
equal), which the alpha worker can evaluate on its own plane; whole-pixel matches and cache symbols are in the green
stream. Tables, container and mosaic as CNGM0007, signature "CNGM0008". The encoder makes CNGM0005's decisions (its
parse), with this stream's contexts and cache. Exact, allocating Python reference; sizes and correctness only."""
import struct
import numpy as np
from research.tiled_zstd_codec import CELL, EDGES, GATE, TILE, Zstd, block_means, crc, median_edge, neighbours, tile_grid, wrap
from research.tiled_context_codec import ALPHA0, BLUE0, LOWER, RED0, unzig
from research import tiled_cache_codec as Q
from research import tiled_plane_codec as P
from research import tiled_planes_codec as S7
from research.tiled_match_codec import DISTANCE, NCODES, initial_repeats, rans_encode
from research.tiled_cache_codec import CACHE0

MAGIC = b"CNGM0008"
GREEN_ONLY0 = P.GREEN_ONLY0
ORDER = S7.ORDER


def green_hits(tile, residual_green):
    """Per pixel: zigzag of its green residual when the last earlier pixel with its green value has its colour, else -1."""
    h, w, C = tile.shape
    flat = tile.reshape(-1, C).astype(np.int64)
    keys = (flat @ (np.int64(256) ** np.arange(C, dtype=np.int64))).tolist(); greens = flat[:, 1].tolist()
    last = [-1] * 256; hits = []
    for i, (key, gv) in enumerate(zip(keys, greens)):
        j = last[gv]
        hits.append(P.zig(residual_green[i]) if (j >= 0 and keys[j] == key) else -1)
        last[gv] = i
    return hits


def view_of(tile):
    h, w, C = tile.shape; n = h * w
    s = tile.astype(np.int32)
    L, U, UL, UR = neighbours(s)
    r = wrap(s - median_edge(L, U, UL))
    ag = np.abs(r[:, :, 1]); ar = np.abs(r[:, :, 0]); ab = np.abs(r[:, :, 2])
    kg = S7.classes(ag).reshape(-1).tolist(); kr = S7.classes(np.maximum(ag, ar)).reshape(-1).tolist(); kb = S7.classes(np.maximum(ag, ab)).reshape(-1).tolist()
    quiet = ((L[:, :, 3] == U[:, :, 3]) & (U[:, :, 3] == UL[:, :, 3])).reshape(-1).tolist() if C == 4 else [True] * n
    R = r.reshape(-1, C).tolist()
    packed = ((r.reshape(-1, C) + 128).astype(np.int64) @ (np.int64(256) ** np.arange(C, dtype=np.int64))).tolist()
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
    hits = green_hits(tile, [R[i][1] for i in range(n)])
    return dict(h=h, w=w, C=C, n=n, packed=packed, planes=planes, contexts=contexts, literals=literals, hits=hits)


# ------------------------------------------------------------------ decoding: one worker per plane, each reconstructing its plane
def med_at(plane, x, y, w):
    up = plane[(y - 1) * w + x] if y >= 1 else 0
    lf = plane[y * w + x - 1] if x >= 1 else up
    ul = plane[(y - 1) * w + x - 1] if (y >= 1 and x >= 1) else up
    hi = lf if lf > up else up; lo = up if lf > up else lf
    return (lo if ul >= hi else (hi if ul <= lo else lf + up - ul)), lf, up, ul


def decode_green(data, w, h, tables):
    n = w * h; dec = S7.PlaneDecoder(data, tables, w)
    g = [0] * n; sg = [0] * n; eg = [0] * n; whole = [0] * n; source = [-1] * n; last = [-1] * 256
    reps = initial_repeats(w); whole_reps = initial_repeats(w); left = dist = wleft = wd = 0
    for y in range(h):
        for x in range(w):
            i = y * w + x; m = med_at(sg, x, y, w)[0]
            if wleft: g[i] = g[i - wd]; whole[i] = wd; wleft -= 1
            elif left: g[i] = g[i - dist]; left -= 1
            else:
                gs = dec.symbol(S7.plane_class(eg, i, x, y, w))
                if 256 <= gs < CACHE0:
                    wd, L = dec.match(gs - 256, whole_reps, i, n); wleft = L - 1; g[i] = g[i - wd]; whole[i] = wd
                elif CACHE0 <= gs < GREEN_ONLY0:
                    g[i] = unzig(gs - CACHE0); value = (m + g[i]) & 255
                    if last[value] < 0: raise ValueError("empty cache slot")
                    source[i] = last[value]
                elif gs >= GREEN_ONLY0:
                    dist, L = dec.match(gs - GREEN_ONLY0, reps, i, n); left = L - 1; g[i] = g[i - dist]
                else: g[i] = unzig(gs)
            sg[i] = (m + g[i]) & 255; eg[i] = abs(g[i]); last[sg[i]] = i
    if wleft or left: raise ValueError("match past the tile")
    dec.finish()
    return g, sg, whole, source


def decode_other(c, data, w, h, tables, g, whole, source):
    n = w * h; dec = S7.PlaneDecoder(data, tables, w)
    r = [0] * n; sc = [0] * n; e = [0] * n; reps = initial_repeats(w); left = dist = 0
    for y in range(h):
        for x in range(w):
            i = y * w + x; m, lf, up, ul = med_at(sc, x, y, w)
            if whole[i]: r[i] = r[i - whole[i]]; sc[i] = (m + r[i]) & 255
            elif source[i] >= 0: sc[i] = sc[source[i]]; r[i] = wrap(sc[i] - m)
            elif left: r[i] = r[i - dist]; left -= 1; sc[i] = (m + r[i]) & 255
            else:
                if c == 3: ctx = ALPHA0 + (0 if lf == up == ul else 1); k = 0
                else:
                    k = S7.plane_class(e, i, x, y, w)
                    gb = 0 if g[i] == 0 else (1 if -2 <= g[i] <= 2 else 2)
                    ctx = (RED0 if c == 0 else BLUE0) + 3 * k + gb
                v = dec.symbol(ctx)
                if v >= 256:
                    dist, L = dec.match(v - 256, reps, i, n); left = L - 1; r[i] = r[i - dist]
                else:
                    v = unzig(v); r[i] = wrap(v + g[i]) if (c != 3 and k >= GATE) else v
                sc[i] = (m + r[i]) & 255
            e[i] = max(abs(g[i]), abs(r[i]))
    if left: raise ValueError("match past the tile")
    dec.finish()
    return sc


def decode_tile(payload, w, h, C, tables):
    order = [c for c in ORDER if c < C]
    pos = 0; lengths = []
    for _ in range(len(order) - 1):
        L, pos = S7.read_leb128(payload, pos); lengths.append(L)
    rest = len(payload) - pos - sum(lengths)
    if rest < 0: raise ValueError("stream lengths")
    lengths.append(rest)
    datas = {}
    for c, L in zip(order, lengths):
        datas[c] = payload[pos:pos + L]; pos += L
    g, sg, whole, source = decode_green(datas[1], w, h, tables)
    samples = {1: sg}
    for c in order[1:]:
        samples[c] = decode_other(c, datas[c], w, h, tables, g, whole, source)
    return np.stack([np.array(samples[c], np.uint8).reshape(h, w) for c in range(C)], axis=2)


# ------------------------------------------------------------------ container
def encode(img, z=None, detail=None):
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if C not in (3, 4): raise ValueError("channels")
    z = z or Zstd()
    tiles = [img[y0:y1, x0:x1] for (y0, y1, x0, x1) in tile_grid(W, H)] + [block_means(img)]
    views = [view_of(t) for t in tiles]
    costs = P.bit_costs(P.count_symbols([Q.tile_ops_greedy(t) for t in tiles]))   # initial code lengths as CNGM0005's encoder
    for _ in range(2):
        plans = [P.plan(v, costs) for v in views]
        streams = [S7.emit_streams(v, *p) for v, p in zip(views, plans)]
        costs = P.bit_costs(P.count_symbols([sum(s, []) for s in streams]))
    counts = P.count_symbols([sum(s, []) for s in streams])
    qs, listed = P.escape_tables(counts)
    tables = [P.Table(q) for q in qs]
    streams = [[P.escape_ops(st, counts, listed) for st in s] for s in streams]
    blob = P.serialize_tables(qs, z)
    payloads = [S7.pack_streams(s, tables, C) for s in streams[:-1]]
    mosaic = S7.pack_streams(streams[-1], tables, C)
    fields = struct.pack(">IIBBIIII", W, H, C, TILE, len(mosaic), crc(mosaic), len(blob), crc(blob)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    second = b"" if len(payloads) == 1 else blob
    if detail is not None:
        detail.update(tables=len(blob), mosaic=len(mosaic), payload=sum(len(x) for x in payloads),
                      whole_matches=int(sum(len(p[0]) for p in plans[:-1])), cached=int(sum(sum(bool(v) for v in p[1]) for p in plans[:-1])),
                      plane_matches=int(sum(sum(len(m) for m in p[2]) for p in plans[:-1])))
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
