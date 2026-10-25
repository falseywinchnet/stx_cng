# V7 stream (CNGPHI07)

An experimental representation, not the published CNG grammar. V7 is the V6
construction (see `../version6/FORMAT.md` for the per-pixel coding) inside
independently decodable frames.

## Container

| Field | Bytes | Meaning |
| --- | --- | --- |
| Signature | 8 | ASCII `CNGPHI07` |
| Width, height | 4 + 4 | big-endian u32, ≥ 1 |
| Channels | 1 | 3 (RGB8) or 4 (RGBA8) |
| Frame side | 1 | 128 as written; a decoder honours 16–255 |
| Directory | 8 per frame | payload length u32, CRC32 u32 of the payload, frames in raster order |
| Directory checksum | 4 | CRC32 of the directory bytes |
| Payloads | | one per frame, raster order |

Frames tile the image in raster order; the last column and row of frames
are smaller when the dimensions are not multiples of the side. A frame's
payload is the CNGPHI05 stream of the frame taken as an image of its own,
without that stream's 17-byte header: the adaptive models, the range coder
and the bounded repetition table start fresh for every frame, and the
frame's top and left edges take the image-edge boundary rules. No frame
depends on another, so frames decode in any order and in parallel, and
damage to a payload is confined to its frame and named by its checksum.
CRC32 is the IEEE polynomial as zlib computes it. Width × height × channels
must not exceed 2³¹.

The exact reference is `reference.py` (which codes each frame with
`reference_frame_engine.py`, the CNGPHI05 reference); the C++20 kernel is
byte-identical to it on the retained vectors and corpus.
