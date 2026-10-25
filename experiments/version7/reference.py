"""V7 reference: stream CNGPHI07 = the CNGPHI05 construction inside independently decodable frames.
The image is cut into a grid of FRAME x FRAME pixel frames in raster order (the last column and row of frames may be
smaller). Every frame is coded exactly as a CNGPHI05 image of its own (reference_frame_engine.py): the adaptive models,
the range coder and the bounded repetition table all start fresh, and the frame's top and left edges take the
image-edge boundary rules, so no frame depends on any other. Frames therefore decode in any order and in parallel, and
damage to a file is confined to the frames whose checksums fail.

Container (all integers big-endian):
    "CNGPHI07"  width u32  height u32  channels u8  frame side u8 (128; the decoder honours any side 16..255)
    directory: for each frame in raster order: payload length u32, CRC32 u32 of the payload
    directory CRC32 u32 (over the directory bytes)
    payloads in raster order (each is a CNGPHI05 stream of the frame without its 17-byte header)
Checksums are zlib CRC32. The decoder validates the directory before reading payloads and each payload before
reconstructing its frame.
"""
import struct, zlib, numpy as np
import reference_frame_engine as P5
MAGIC = b"CNGPHI07"; FRAME = 128
def frame_grid(W, H, frame=FRAME):
    """(y0, y1, x0, x1) of every frame in raster order."""
    return [(y0, min(H, y0 + frame), x0, min(W, x0 + frame)) for y0 in range(0, H, frame) for x0 in range(0, W, frame)]
def encode(img, frame=FRAME):
    img = np.asarray(img); H, W, C = img.shape
    payloads = []
    for y0, y1, x0, x1 in frame_grid(W, H, frame):
        tile = np.ascontiguousarray(img[y0:y1, x0:x1])
        payloads.append(P5.encode(tile)[17:])
    directory = b"".join(struct.pack(">II", len(p), zlib.crc32(p) & 0xFFFFFFFF) for p in payloads)
    head = MAGIC + struct.pack(">IIBB", W, H, C, frame)
    return head + directory + struct.pack(">I", zlib.crc32(directory) & 0xFFFFFFFF) + b"".join(payloads)
def parse(data):
    assert data[:8] == MAGIC
    W, H, C, frame = struct.unpack(">IIBB", data[8:18]); grid = frame_grid(W, H, frame); n = len(grid)
    directory = data[18:18 + 8 * n]
    assert struct.unpack(">I", data[18 + 8 * n:22 + 8 * n])[0] == (zlib.crc32(directory) & 0xFFFFFFFF), "directory checksum"
    entries = [struct.unpack(">II", directory[8 * i:8 * i + 8]) for i in range(n)]
    pos = 22 + 8 * n; frames = []
    for (y0, y1, x0, x1), (length, crc) in zip(grid, entries):
        payload = data[pos:pos + length]; pos += length
        frames.append(dict(rect=(y0, y1, x0, x1), payload=payload, ok=(zlib.crc32(payload) & 0xFFFFFFFF) == crc and len(payload) == length))
    assert pos == len(data), "trailing bytes"
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
