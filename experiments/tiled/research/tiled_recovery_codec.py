"""CNGR0001: CNGM0008 with restart segments inside tiles and XOR parity across segments (research constructions for
measuring recoverability; one fixed construction per (rows, group) pair, both carried in the header, never chosen per
image).

Segments. The image is cut into segments 128 pixels wide and `rows` tall (rows = 128 is CNGM0008's tiling). Every
segment is coded exactly as a CNGM0008 tile of its own size: MED with the boundary rules at its top row and left
column, its own rANS streams, matches and colour cache, all ending at the segment. A lost segment therefore costs only
its own pixels. The tables stay per image and are shared by every segment.

Parity. With group g > 0 and N segments there are P = N // g parity blocks (none when N < g). Segment s belongs to
group s mod P, so segments adjacent in the file, which one burst is likely to hit together, fall into different groups.
Parity block p is the XOR of its members' payloads, each zero-padded to the longest member; its length is that longest
member's, so the decoder derives it from the directory. A group with exactly one damaged member and an intact parity
block rebuilds that member, which must then match its checksum.

Layout: signature, header fields, directory, tables, mosaic, payloads, parity blocks, then the mosaic again, the
tables again (when there are two or more segments), the directory again and the header fields again.
  header fields  >IIBBBBIIII: width, height, channels, tile width (128), rows, group, mosaic length and CRC, tables
                 length and CRC; then their CRC
  directory      per segment: length, CRC; per parity block: CRC; then the CRC of all of it
Exact, allocating Python reference built on research/tiled_green_cache_codec.py; sizes and salvage logic only."""
import struct
import numpy as np
from research.tiled_zstd_codec import TILE, Zstd, block_means, crc
from research import tiled_cache_codec as Q
from research import tiled_plane_codec as P
from research import tiled_planes_codec as S7
from research import tiled_green_cache_codec as G

MAGIC = b"CNGR0001"
FIELDS = ">IIBBBBIIII"
FIELD_BYTES = struct.calcsize(FIELDS)   # 28
HEAD = 8 + FIELD_BYTES + 4
TAIL = FIELD_BYTES + 4


def segment_grid(W, H, rows):
    return [(y0, min(H, y0 + rows), x0, min(W, x0 + TILE)) for y0 in range(0, H, rows) for x0 in range(0, W, TILE)]


def group_members(n, group):
    p = n // group if group else 0
    return [[s for s in range(n) if s % p == q] for q in range(p)]


def parity_blocks(payloads, group):
    out = []
    for members in group_members(len(payloads), group):
        length = max(len(payloads[s]) for s in members)
        acc = np.zeros(length, np.uint8)
        for s in members:
            acc[:len(payloads[s])] ^= np.frombuffer(payloads[s], np.uint8)
        out.append(acc.tobytes())
    return out


def encode_parts(img, rows, z=None):
    """The segments' payloads, the tables blob and the mosaic, as CNGM0008's encoder makes them for this tiling."""
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if C not in (3, 4): raise ValueError("channels")
    z = z or Zstd()
    tiles = [img[y0:y1, x0:x1] for (y0, y1, x0, x1) in segment_grid(W, H, rows)] + [block_means(img)]
    views = [G.view_of(t) for t in tiles]
    costs = P.bit_costs(P.count_symbols([Q.tile_ops_greedy(t) for t in tiles]))
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
    return dict(W=W, H=H, C=C, rows=rows, blob=blob, mosaic=mosaic, payloads=payloads)


def assemble(parts, group):
    W, H, C, rows, blob, mosaic, payloads = (parts[k] for k in ("W", "H", "C", "rows", "blob", "mosaic", "payloads"))
    parity = parity_blocks(payloads, group)
    fields = struct.pack(FIELDS, W, H, C, TILE, rows, group, len(mosaic), crc(mosaic), len(blob), crc(blob)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads) + b"".join(struct.pack(">I", crc(p)) for p in parity)
    directory = entries + struct.pack(">I", crc(entries))
    second = blob if len(payloads) > 1 else b""
    return MAGIC + fields + directory + blob + mosaic + b"".join(payloads) + b"".join(parity) + mosaic + second + directory + fields


def encode(img, rows=128, group=0, z=None):
    return assemble(encode_parts(img, rows, z), group)


def _valid(block, body):
    return len(block) >= body + 4 and struct.unpack(">I", block[body:body + 4])[0] == crc(block[:body])


def salvage(data):
    """What survives damage, without decoding pixels. Returns header (an intact header copy and directory copy),
    tables, mosaic, ok (per segment: intact or rebuilt from parity), rebuilt (count) and payloads (the intact or
    rebuilt bytes, None for lost segments). The signature is not required."""
    lost = dict(header=False, tables=False, mosaic=False, ok=None, rebuilt=0, payloads=None)
    if len(data) < HEAD + TAIL: return lost
    front, tail = data[8:HEAD], data[-TAIL:]
    fields = front if _valid(front, FIELD_BYTES) else tail if _valid(tail, FIELD_BYTES) else None
    if fields is None: return lost
    W, H, C, side, rows, group, mlen, mcrc, tlen, tcrc = struct.unpack(FIELDS, fields[:FIELD_BYTES])
    if side != TILE or rows == 0: return lost
    grid = segment_grid(W, H, rows); n = len(grid); members = group_members(n, group); p = len(members)
    block = 8 * n + 4 * p + 4
    if len(data) < HEAD + 2 * block + TAIL: return lost
    first, mirror = data[HEAD:HEAD + block], data[len(data) - TAIL - block:len(data) - TAIL]
    directory = first if _valid(first, block - 4) else mirror if _valid(mirror, block - 4) else None
    if directory is None: return lost
    lengths = [struct.unpack(">I", directory[8 * s:8 * s + 4])[0] for s in range(n)]
    checks = [struct.unpack(">I", directory[8 * s + 4:8 * s + 8])[0] for s in range(n)]
    parity_checks = [struct.unpack(">I", directory[8 * n + 4 * q:8 * n + 4 * q + 4])[0] for q in range(p)]
    parity_lengths = [max(lengths[s] for s in m) for m in members]
    second = tlen if n > 1 else 0
    start = HEAD + block + tlen + mlen
    payload_end = start + sum(lengths); parity_end = payload_end + sum(parity_lengths)
    if parity_end + mlen + second + block + TAIL != len(data): return lost
    out = dict(header=True, tables=False, mosaic=False, ok=[False] * n, rebuilt=0, payloads=[None] * n, segments=grid, W=W, H=H, C=C)
    tables_front = data[HEAD + block:HEAD + block + tlen]; tables_back = data[parity_end + mlen:parity_end + mlen + second]
    out["tables"] = crc(tables_front) == tcrc or (second > 0 and crc(tables_back) == tcrc)
    if not out["tables"]: return out   # every segment and the mosaic need the tables
    out["mosaic"] = crc(data[HEAD + block + tlen:start]) == mcrc or crc(data[parity_end:parity_end + mlen]) == mcrc
    position = start
    for s in range(n):
        b = data[position:position + lengths[s]]; position += lengths[s]
        if crc(b) == checks[s]: out["ok"][s] = True; out["payloads"][s] = b
    for q, m in enumerate(members):
        missing = [s for s in m if not out["ok"][s]]
        if len(missing) != 1: continue
        at = payload_end + sum(parity_lengths[:q]); par = data[at:at + parity_lengths[q]]
        if crc(par) != parity_checks[q]: continue
        acc = np.frombuffer(par, np.uint8).copy()
        for s in m:
            if s != missing[0]: acc[:lengths[s]] ^= np.frombuffer(out["payloads"][s], np.uint8)
        b = acc[:lengths[missing[0]]].tobytes()
        if crc(b) == checks[missing[0]]:
            out["ok"][missing[0]] = True; out["payloads"][missing[0]] = b; out["rebuilt"] += 1
    return out


def decode(data, z=None):
    """Full decode of an undamaged (or fully salvaged) stream with the CNGM0008 tile decoder."""
    z = z or Zstd(); s = salvage(data)
    if not s["header"] or not s["tables"] or not all(s["ok"]): raise ValueError("damaged")
    W, H, C = s["W"], s["H"], s["C"]
    fields = data[8:HEAD] if _valid(data[8:HEAD], FIELD_BYTES) else data[-TAIL:]
    tlen = struct.unpack(FIELDS, fields[:FIELD_BYTES])[8]
    n = len(s["segments"]); group = struct.unpack(FIELDS, fields[:FIELD_BYTES])[5]; block = 8 * n + 4 * len(group_members(n, group)) + 4
    tables = [P.Table(q) for q in P.parse_tables(data[HEAD + block:HEAD + block + tlen], z)]
    out = np.zeros((H, W, C), np.uint8)
    for (y0, y1, x0, x1), payload in zip(s["segments"], s["payloads"]):
        out[y0:y1, x0:x1] = G.decode_tile(payload, x1 - x0, y1 - y0, C, tables)
    return out
