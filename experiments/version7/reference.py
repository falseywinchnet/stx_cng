"""V7 reference: stream CNGPHI08 = the CNGPHI05 construction inside independently decodable frames, with a mosaic layer.
The image is cut into a grid of FRAME x FRAME pixel frames in raster order (the last column and row of frames may be
smaller). Every frame is coded exactly as a CNGPHI05 image of its own (reference_frame_engine.py): the adaptive models,
the range coder and the bounded repetition table all start fresh, and the frame's top and left edges take the
image-edge boundary rules, so no frame depends on any other. Frames therefore decode in any order and in parallel, and
damage to a file is confined to the frames whose checksums fail.

The mosaic layer is the mean colour of every CELL x CELL cell (edge cells smaller; integer mean rounded half up), as
an image of ceil(W/CELL) x ceil(H/CELL) coded as one CNGPHI05 image without its header. It is the progressive first
picture and the fill for frames lost to damage, and is stored twice.

Container (all integers big-endian; CRC32 is zlib's):
    "CNGPHI08"
    fields: width u32  height u32  channels u8  frame side u8 (128; the decoder honours any side 16..255)
            mosaic payload length u32  CRC32 of the mosaic payload
    CRC32 of the eighteen field bytes
    directory: for each frame in raster order: payload length u32, CRC32 of the payload; then CRC32 of those entries
    mosaic payload
    frame payloads in raster order
    mosaic payload again (identical bytes)
    directory again (identical bytes)
    fields and their CRC32 again (identical bytes)
The header fields, the directory and the mosaic are each stored twice; the decoder takes the first copy whose
checksum matches, so damage confined to one copy costs nothing, and damage to both is reported rather than decoded.
Each frame payload is checked before its frame is reconstructed.
"""
import struct, zlib, numpy as np
import reference_frame_engine as P5
MAGIC = b"CNGPHI08"; FRAME = 128; CELL = 16
def frame_grid(W, H, frame=FRAME):
    """(y0, y1, x0, x1) of every frame in raster order."""
    return [(y0, min(H, y0 + frame), x0, min(W, x0 + frame)) for y0 in range(0, H, frame) for x0 in range(0, W, frame)]
def block_means(img, cell=CELL):
    """Mean colour of every cell x cell block, integer, rounded half up; edge blocks are smaller."""
    img = np.asarray(img); H, W, C = img.shape
    mh, mw = -(-H // cell), -(-W // cell); out = np.zeros((mh, mw, C), np.uint8)
    for my in range(mh):
        for mx in range(mw):
            block = img[my * cell:(my + 1) * cell, mx * cell:(mx + 1) * cell].astype(np.uint64)
            count = block.shape[0] * block.shape[1]
            out[my, mx] = (block.sum(axis=(0, 1)) + count // 2) // count
    return out
def fields_block(W, H, C, frame, mosaic):
    fields = struct.pack(">IIBBII", W, H, C, frame, len(mosaic), zlib.crc32(mosaic) & 0xFFFFFFFF)
    return fields + struct.pack(">I", zlib.crc32(fields) & 0xFFFFFFFF)
def directory_block(payloads):
    entries = b"".join(struct.pack(">II", len(p), zlib.crc32(p) & 0xFFFFFFFF) for p in payloads)
    return entries + struct.pack(">I", zlib.crc32(entries) & 0xFFFFFFFF)
def mosaic_payload(img):
    return P5.encode(block_means(img))[17:]
def assemble(W, H, C, frame, payloads, mosaic):
    fields = fields_block(W, H, C, frame, mosaic); directory = directory_block(payloads)
    return MAGIC + fields + directory + mosaic + b"".join(payloads) + mosaic + directory + fields
def encode(img, frame=FRAME):
    img = np.asarray(img); H, W, C = img.shape
    payloads = []
    for y0, y1, x0, x1 in frame_grid(W, H, frame):
        tile = np.ascontiguousarray(img[y0:y1, x0:x1])
        payloads.append(P5.encode(tile)[17:])
    return assemble(W, H, C, frame, payloads, mosaic_payload(img))
def _valid(block, body):
    return struct.unpack(">I", block[body:body + 4])[0] == (zlib.crc32(block[:body]) & 0xFFFFFFFF)
def parse(data):
    assert data[:8] == MAGIC and len(data) >= 52
    front, tail = data[8:30], data[-22:]
    fields = front if _valid(front, 18) else tail
    assert _valid(fields, 18), "header fields: both copies damaged"
    W, H, C, frame, mlen, mcrc = struct.unpack(">IIBBII", fields[:18]); grid = frame_grid(W, H, frame); n = len(grid)
    block = 8 * n + 4
    assert len(data) >= 30 + 2 * block + 2 * mlen + 22, "directory extent"
    first, mirror = data[30:30 + block], data[len(data) - 22 - block:len(data) - 22]
    directory = first if _valid(first, 8 * n) else mirror
    assert _valid(directory, 8 * n), "directory: both copies damaged"
    entries = [struct.unpack(">II", directory[8 * i:8 * i + 8]) for i in range(n)]
    ma, mb = data[30 + block:30 + block + mlen], data[len(data) - 22 - block - mlen:len(data) - 22 - block]
    mosaic = ma if (zlib.crc32(ma) & 0xFFFFFFFF) == mcrc else mb if (zlib.crc32(mb) & 0xFFFFFFFF) == mcrc else None
    pos = 30 + block + mlen; end = len(data) - 22 - block - mlen; frames = []
    for (y0, y1, x0, x1), (length, crc) in zip(grid, entries):
        payload = data[pos:min(pos + length, end)]; pos += length
        frames.append(dict(rect=(y0, y1, x0, x1), payload=payload, ok=(zlib.crc32(payload) & 0xFFFFFFFF) == crc and len(payload) == length))
    assert pos == end, "payload extent"
    return W, H, C, frames, mosaic
def decode_mosaic(data):
    """The mosaic layer as an image, or None when both copies are damaged."""
    W, H, C, frames, mosaic = parse(data)
    if mosaic is None: return None
    mh, mw = -(-H // CELL), -(-W // CELL)
    return P5.decode(P5.MAGIC + struct.pack(">IIB", mw, mh, C) + mosaic)
def decode_frames(data):
    """Decode every frame whose checksum passes; return (image, list of failed frame indices). Failed frames take the
    mosaic colour of their cells when the mosaic decodes, mid grey otherwise."""
    W, H, C, frames, mosaic = parse(data); out = np.full((H, W, C), 128, np.uint8); failed = []
    fill = decode_mosaic(data)
    if fill is not None: out[:] = np.repeat(np.repeat(fill, CELL, axis=0), CELL, axis=1)[:H, :W]
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
