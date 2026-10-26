"""CNG 1.0: the frozen file format (format version 1). Python reference of the container around the CNGM0009
construction (research/tiled_segment_codec.py): every segment payload, the tables and the mosaic are coded exactly as
CNGM0009 codes them, except that the tables are a standard zstd frame (RFC 8878; no content size, checksum or
dictionary id) instead of a magicless one.

Sample order: a CNG image's samples are B, G, R (and A), interleaved. Arrays given to encode and returned by decode
are in that order; from_rgb and to_rgb convert from and to R, G, B (and A) order. The coding is the same for any order
(plane 1, green, is the plane with the special role); the order fixes what the stored planes mean.

File layout, all integers big-endian:
    signature   8 bytes: 0x89 'C' 'N' 'G' 0x0D 0x0A 0x1A 0x0A
    fields      width u32, height u32, channels u8 (3 or 4), version u8 (1), segment width u8 (128), segment height
                u8 (32), mosaic length u32, mosaic CRC32 u32, tables length u32, tables CRC32 u32; then their CRC32
    directory   per segment (raster order, 128 x 32, the last column and row smaller): payload length u32, CRC32 u32;
                then the CRC32 of the entries
    tables, mosaic, segment payloads, mosaic, tables (two or more segments only), directory, fields
The CRC32 is ISO-HDLC (zlib's crc32). Exact, allocating Python reference; sizes and correctness only.
    encode(bgra) -> bytes; decode(data) -> bgra; decode_segments(data, require_signature) -> bgra, lost, mosaic_ok"""
import struct
import numpy as np
from research.tiled_zstd_codec import CELL, Zstd, crc
from research import tiled_plane_codec as P
from research import tiled_green_cache_codec as G
from research import tiled_recovery_codec as R

SIGNATURE = bytes([0x89, 0x43, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
VERSION = 1
SEGMENT_WIDTH = 128
SEGMENT_ROWS = 32
FIELDS = ">IIBBBBIIII"
FIELD_BYTES = struct.calcsize(FIELDS)   # 28
HEAD = len(SIGNATURE) + FIELD_BYTES + 4
TAIL = FIELD_BYTES + 4
MAX_SAMPLES = 1 << 31
TABLE_LEVEL = 19


class FrameZstd(Zstd):
    """zstd with standard frames: level 19, no content size, checksum or dictionary id."""
    def __init__(self):
        super().__init__(TABLE_LEVEL)

    def _parameters(self):
        lib = self.lib
        lib.ZSTD_CCtx_reset(self.cctx, 3); lib.ZSTD_DCtx_reset(self.dctx, 3)
        for parameter, value in ((100, self.level), (200, 0), (201, 0), (202, 0)):   # level, no size, checksum or id
            self._check(lib.ZSTD_CCtx_setParameter(self.cctx, parameter, value), "compression parameter")


def from_rgb(img):
    """An (h, w, 3 or 4) R, G, B (A) array in CNG sample order B, G, R (A)."""
    order = [2, 1, 0] + ([3] if img.shape[2] == 4 else [])
    return np.ascontiguousarray(img[:, :, order])


def to_rgb(img):
    """CNG sample order B, G, R (A) back to R, G, B (A)."""
    return from_rgb(img)


def prepare(img):
    """The samples a corpus image (R, G, B order) is stored as: CNG sample order."""
    return from_rgb(np.ascontiguousarray(img, np.uint8))


SAMPLES_SUFFIX = ".raw"   # CNG-order samples beside each vector and corpus file


def segment_grid(W, H):
    return [(y0, min(H, y0 + SEGMENT_ROWS), x0, min(W, x0 + SEGMENT_WIDTH)) for y0 in range(0, H, SEGMENT_ROWS) for x0 in range(0, W, SEGMENT_WIDTH)]


def encode(img, z=None):
    img = np.ascontiguousarray(img, np.uint8); H, W, C = img.shape
    if W == 0 or H == 0 or C not in (3, 4) or W * H * C > MAX_SAMPLES: raise ValueError("dimensions")
    parts = R.encode_parts(img, SEGMENT_ROWS, z or FrameZstd())
    blob, mosaic, payloads = parts["blob"], parts["mosaic"], parts["payloads"]
    fields = struct.pack(FIELDS, W, H, C, VERSION, SEGMENT_WIDTH, SEGMENT_ROWS, len(mosaic), crc(mosaic), len(blob), crc(blob))
    fields += struct.pack(">I", crc(fields))
    entries = b"".join(struct.pack(">II", len(p), crc(p)) for p in payloads); directory = entries + struct.pack(">I", crc(entries))
    second = blob if len(payloads) > 1 else b""
    return SIGNATURE + fields + directory + blob + mosaic + b"".join(payloads) + mosaic + second + directory + fields


def _valid(block, body):
    return len(block) >= body + 4 and struct.unpack(">I", block[body:body + 4])[0] == crc(block[:body])


def parse(data, z, require_signature=True):
    if len(data) < HEAD + TAIL or (require_signature and data[:8] != SIGNATURE): raise ValueError("signature")
    front, tail = data[8:HEAD], data[-TAIL:]
    fields = front if _valid(front, FIELD_BYTES) else tail if _valid(tail, FIELD_BYTES) else None
    if fields is None: raise ValueError("header: both copies damaged")
    W, H, C, version, sw, sh, mlen, mcrc, tlen, tcrc = struct.unpack(FIELDS, fields[:FIELD_BYTES])
    if version != VERSION or sw != SEGMENT_WIDTH or sh != SEGMENT_ROWS: raise ValueError("version")
    if W == 0 or H == 0 or C not in (3, 4) or W * H * C > MAX_SAMPLES: raise ValueError("header fields")
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
    z = z or FrameZstd(); W, H, C, tables, segments, mosaic = parse(data, z, require_signature)
    out = np.full((H, W, C), 128, np.uint8); lost = []
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
            lost.append(index)
    return out, lost, mosaic is not None


def decode(data, z=None):
    out, lost, _ = decode_segments(data, z)
    if lost: raise ValueError(f"segments lost: {lost}")
    return out


def decode_mosaic(data, z=None):
    z = z or FrameZstd(); W, H, C, tables, _, mosaic = parse(data, z)
    if mosaic is None: raise ValueError("mosaic: both copies damaged")
    return G.decode_tile(mosaic, -(-W // CELL), -(-H // CELL), C, tables)


from research.tiled_segment_codec import extra_vectors   # noqa: E402  (the same segment-boundary vector cases)
MAGIC = SIGNATURE
SUFFIX = ".cng"
