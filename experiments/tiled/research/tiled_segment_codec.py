"""CNGM0009: CNGM0008 with restart segments 128 pixels wide and 32 rows tall (owner decision, 2026-10-25, after the
recovery measurements in docs/TILED_RECOVERY.md).

Every segment is coded exactly as a CNGM0008 tile of its own size (research/tiled_green_cache_codec.py): MED with the
boundary rules at its top row and left column, one rANS stream per plane, whole-pixel and per-plane matches and the
green-keyed colour cache, all ending at the segment. The tables stay per image and are shared by every segment. A
damaged segment loses only its own 128 x 32 pixels, which the mosaic fills.

Container: as CNGM0008, with the segment grid in place of the 128 x 128 tile grid (segments in raster order, left to
right, top to bottom). The header's side byte is 128 (the segment width); the segment height, 32, is fixed by the
signature. Header fields, directory and mosaic are stored twice; the tables twice when there are two or more segments.
    signature, fields (>IIBBIIII + CRC), directory (per segment: length, CRC; then CRC), tables, mosaic, payloads,
    mosaic, tables (two or more segments), directory, fields
The encoder makes CNGM0008's decisions for every segment. Payloads are those of research/tiled_recovery_codec.py at
rows 32, group 0. Exact, allocating Python reference; sizes and correctness only."""
import struct
import numpy as np
from research.tiled_zstd_codec import CELL, TILE, Zstd, crc
from research import tiled_plane_codec as P
from research import tiled_green_cache_codec as G
from research import tiled_recovery_codec as R

MAGIC = b"CNGM0009"
ROWS = 32
FIELDS = ">IIBBIIII"
FIELD_BYTES = struct.calcsize(FIELDS)   # 26
HEAD = 8 + FIELD_BYTES + 4
TAIL = FIELD_BYTES + 4


def segment_grid(W, H):
    return R.segment_grid(W, H, ROWS)


def encode(img, z=None):
    parts = R.encode_parts(img, ROWS, z)
    W, H, C, blob, mosaic, payloads = (parts[k] for k in ("W", "H", "C", "blob", "mosaic", "payloads"))
    fields = struct.pack(FIELDS, W, H, C, TILE, len(mosaic), crc(mosaic), len(blob), crc(blob)); fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    second = blob if len(payloads) > 1 else b""
    return MAGIC + fields + directory + blob + mosaic + b"".join(payloads) + mosaic + second + directory + fields


def _valid(block, body):
    return len(block) >= body + 4 and struct.unpack(">I", block[body:body + 4])[0] == crc(block[:body])


def parse(data, z, require_signature=True):
    """Header, directory, tables and mosaic from intact copies; every segment's payload and checksum verdict.
    Salvage passes require_signature=False: the checksummed fields decide."""
    if (require_signature and data[:8] != MAGIC) or len(data) < HEAD + TAIL: raise ValueError("signature")
    front, tail = data[8:HEAD], data[-TAIL:]
    fields = front if _valid(front, FIELD_BYTES) else tail if _valid(tail, FIELD_BYTES) else None
    if fields is None: raise ValueError("header: both copies damaged")
    W, H, C, side, mlen, mcrc, tlen, tcrc = struct.unpack(FIELDS, fields[:FIELD_BYTES])
    if W == 0 or H == 0 or C not in (3, 4) or side != TILE: raise ValueError("header fields")
    grid = segment_grid(W, H); n = len(grid); block = 8 * n + 4
    copies = 1 if n == 1 else 2
    if len(data) < HEAD + 2 * block + 2 * mlen + copies * tlen + TAIL: raise ValueError("extent")
    first, mirror = data[HEAD:HEAD + block], data[len(data) - TAIL - block:len(data) - TAIL]
    directory = first if _valid(first, 8 * n) else mirror if _valid(mirror, 8 * n) else None
    if directory is None: raise ValueError("directory: both copies damaged")
    entries = [struct.unpack(">II", directory[8 * i:8 * i + 8]) for i in range(n)]
    second = tlen if copies == 2 else 0
    end = len(data) - TAIL - block - second - mlen; start = HEAD + block + tlen + mlen
    if start > end: raise ValueError("payload extent")
    candidates = [data[HEAD + block:HEAD + block + tlen]] + ([data[end + mlen:end + mlen + tlen]] if copies == 2 else [])
    blob = next((b for b in candidates if crc(b) == tcrc), None)
    if blob is None: raise ValueError("tables: damaged")
    tables = [P.Table(q) for q in P.parse_tables(blob, z)]
    mosaic = next((m for m in (data[HEAD + block + tlen:start], data[end:end + mlen]) if crc(m) == mcrc), None)
    pos = start; segments = []
    for rect, (length, checksum) in zip(grid, entries):
        payload = data[pos:min(pos + length, end)]; pos += length
        segments.append(dict(rect=rect, payload=payload, ok=len(payload) == length and crc(payload) == checksum))
    if pos != end: raise ValueError("payload extent")
    return W, H, C, tables, segments, mosaic


def decode_segments(data, z=None, require_signature=True):
    """Decode every intact segment; lost segments take their cells' mosaic colour (mid grey without a mosaic)."""
    z = z or Zstd(); W, H, C, tables, segments, mosaic = parse(data, z, require_signature)
    out = np.full((H, W, C), 128, np.uint8); failed = []
    if mosaic is not None:
        try:
            m = G.decode_tile(mosaic, -(-W // CELL), -(-H // CELL), C, tables)
            out[:] = np.repeat(np.repeat(m, CELL, axis=0), CELL, axis=1)[:H, :W]
        except (ValueError, IndexError):
            mosaic = None
    for index, s in enumerate(segments):
        y0, y1, x0, x1 = s["rect"]
        try:
            if not s["ok"]: raise ValueError("checksum")
            out[y0:y1, x0:x1] = G.decode_tile(s["payload"], x1 - x0, y1 - y0, C, tables)
        except (ValueError, IndexError):
            failed.append(index)
    return out, failed, mosaic is not None


def decode(data, z=None):
    out, failed, _ = decode_segments(data, z)
    if failed: raise ValueError(f"segments failed: {failed}")
    return out


def extra_vectors(rng):
    """Vectors that reach the segment boundaries: exact and partial segment heights, several rows of segments, RGBA."""
    y, x = np.mgrid[0:97, 0:260]
    ramp = np.stack([(x * 0.5 + y) % 256, (x * 0.25 + y * 2) % 256, (y * 0.75) % 256], 2).astype(np.uint8)
    yield "segment_exact_128x32", rng.integers(0, 256, (32, 128, 3), dtype=np.uint8)
    yield "segment_partial_129x33", rng.integers(0, 256, (33, 129, 3), dtype=np.uint8)
    yield "segment_ramp_260x97", ramp
    yield "segment_rgba_70x65", np.concatenate([rng.integers(0, 256, (65, 70, 3), dtype=np.uint8), (np.arange(65)[:, None, None] * 3 + np.zeros((65, 70, 1))).astype(np.uint8)], 2)
