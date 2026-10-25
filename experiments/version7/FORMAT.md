# V7 stream (CNGPHI08)

The standalone stream of release 0.4.0. V7 is the V6 construction (see
`../version6/FORMAT.md` for the per-pixel coding) inside independently
decodable frames, with a checksummed, duplicated index and a duplicated
16-cell mosaic layer. Release 0.3.0 wrote the same frames without the mosaic
as `CNGPHI07`.

## Container

| Field | Bytes | Meaning |
| --- | --- | --- |
| Signature | 8 | ASCII `CNGPHI08` |
| Header fields | 18 | width u32, height u32 (big-endian, ≥ 1), channels u8 (3 or 4), frame side u8 (128 as written; a decoder honours 16–255), mosaic payload length u32, mosaic payload CRC32 |
| Header CRC32 | 4 | CRC32 of the eighteen header-field bytes |
| Directory | 8 per frame | payload length u32, CRC32 u32 of the payload, frames in raster order |
| Directory CRC32 | 4 | CRC32 of the directory entries |
| Mosaic | mosaic length | the mosaic layer's payload: the mean colour of every 16×16 cell (edge cells smaller; integer mean rounded half up) as an image of ceil(W/16) × ceil(H/16), coded as one CNGPHI05 frame of its own size without the 17-byte header |
| Payloads | | one per frame, raster order |
| Mosaic, again | mosaic length | the same bytes |
| Directory, again | 8 per frame + 4 | the same bytes as the directory and its CRC32 |
| Header fields, again | 22 | the same bytes as the header fields and their CRC32 |

A reader takes the header fields from the front copy when its CRC32 matches,
otherwise from the trailing copy, and the directory and the mosaic the same
way; a stream with neither copy of the header or directory intact is rejected,
and a stream with neither mosaic copy intact decodes without a mosaic. The
payload lengths must account for exactly the bytes between the two mosaic
copies. Damage confined to one copy of the index or mosaic therefore costs
nothing; the second index copy costs `8 × frames + 30` bytes per file and the
second mosaic copy its coded size.

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
