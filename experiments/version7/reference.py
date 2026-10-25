"""V7 reference: stream CNGPHI07 = the CNGPHI05 construction inside independently decodable frames.
The image is cut into a grid of FRAME x FRAME pixel frames in raster order (the last column and row of frames may be
smaller). Every frame is coded exactly as a CNGPHI05 image of its own (reference_frame_engine.py): the adaptive models,
the range coder and the bounded repetition table all start fresh, and the frame's top and left edges take the
image-edge boundary rules, so no frame depends on any other. Frames therefore decode in any order and in parallel, and
damage to a file is confined to the frames whose checksums fail.

Container (all integers big-endian; CRC32 is zlib's):
    "CNGPHI07"
    fields: width u32  height u32  channels u8  frame side u8 (128; the decoder honours any side 16..255)
    CRC32 of the ten field bytes
    directory: for each frame in raster order: payload length u32, CRC32 of the payload; then CRC32 of those entries
    payloads in raster order (each is a CNGPHI05 stream of the frame without its 17-byte header)
    directory again (identical bytes)
    fields and their CRC32 again (identical bytes)
The header fields and the directory are each stored twice; the decoder takes the first copy whose checksum matches,
so damage confined to one copy costs nothing, and damage to both is reported rather than decoded. Each payload is
checked before its frame is reconstructed.
"""
import struct, zlib, numpy as np
import reference_frame_engine as P5
MAGIC = b"CNGPHI07"; FRAME = 128
def frame_grid(W, H, frame=FRAME):
    """(y0, y1, x0, x1) of every frame in raster order."""
    return [(y0, min(H, y0 + frame), x0, min(W, x0 + frame)) for y0 in range(0, H, frame) for x0 in range(0, W, frame)]
def fields_block(W, H, C, frame):
    fields = struct.pack(">IIBB", W, H, C, frame)
    return fields + struct.pack(">I", zlib.crc32(fields) & 0xFFFFFFFF)
def directory_block(payloads):
    entries = b"".join(struct.pack(">II", len(p), zlib.crc32(p) & 0xFFFFFFFF) for p in payloads)
    return entries + struct.pack(">I", zlib.crc32(entries) & 0xFFFFFFFF)
def encode(img, frame=FRAME):
    img = np.asarray(img); H, W, C = img.shape
    payloads = []
    for y0, y1, x0, x1 in frame_grid(W, H, frame):
        tile = np.ascontiguousarray(img[y0:y1, x0:x1])
        payloads.append(P5.encode(tile)[17:])
    fields = fields_block(W, H, C, frame); directory = directory_block(payloads)
    return MAGIC + fields + directory + b"".join(payloads) + directory + fields
def _valid(block, body):
    return struct.unpack(">I", block[body:body + 4])[0] == (zlib.crc32(block[:body]) & 0xFFFFFFFF)
def parse(data):
    assert data[:8] == MAGIC and len(data) >= 36
    front, tail = data[8:22], data[-14:]
    fields = front if _valid(front, 10) else tail
    assert _valid(fields, 10), "header fields: both copies damaged"
    W, H, C, frame = struct.unpack(">IIBB", fields[:10]); grid = frame_grid(W, H, frame); n = len(grid)
    block = 8 * n + 4
    assert len(data) >= 22 + 2 * block + 14, "directory extent"
    first, mirror = data[22:22 + block], data[len(data) - 14 - block:len(data) - 14]
    directory = first if _valid(first, 8 * n) else mirror
    assert _valid(directory, 8 * n), "directory: both copies damaged"
    entries = [struct.unpack(">II", directory[8 * i:8 * i + 8]) for i in range(n)]
    pos = 22 + block; end = len(data) - 14 - block; frames = []
    for (y0, y1, x0, x1), (length, crc) in zip(grid, entries):
        payload = data[pos:min(pos + length, end)]; pos += length
        frames.append(dict(rect=(y0, y1, x0, x1), payload=payload, ok=(zlib.crc32(payload) & 0xFFFFFFFF) == crc and len(payload) == length))
    assert pos == end, "payload extent"
    return W, H, C, frames
def decode_frames(data):
    """Decode every frame whose checksum passes; return (image, list of failed frame indices). Failed frames are zero."""
    W, H, C, frames = parse(data); out = np.zeros((H, W, C), np.uint8); failed = []
    for i, f in enumerate(frames):
        y0, y1, x0, x1 = f["rect"]
        if not f["ok"]: failed.append(i); continue
        tile = P5.decode(P5.MAGIC + struct.pack(">IIB", x1 - x0, y1 - y0, C) + f["payload"])
        out[y0:y1, x0:x1] = tile
    return out, failed
def decode(data):
    out, failed = decode_frames(data)
    assert not failed, f"frames failed their checksum: {failed}"
    return out
