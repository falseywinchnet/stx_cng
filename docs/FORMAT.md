# CNG format reference

**Release:** 0.4.0 · **Standalone stream:** `CNGPHI08` · **Collection schema:** SHDR v0

This document describes the bytes written and read by this release. CNG means
**Computational Network Graphics**. A standalone image is a `CNGPHI08` stream; a
collection has the `CNG` signature and embeds complete standalone streams.

The format is lossless for interleaved RGB8 and RGBA8 samples. It carries neither
colour-profile metadata nor an implicit conversion to a different colour space.
Every transform is reversible integer arithmetic on sample bytes.

## 1. Conventions

Integers are unsigned and big-endian. CRC32 is the IEEE polynomial as zlib's
`crc32` computes it over the stated bytes, stored big-endian. A checksum detects
accidental corruption; it is not authentication.

## 2. Standalone image

| Field | Bytes | Meaning |
| --- | ---: | --- |
| Signature | 8 | ASCII `CNGPHI08` |
| Header fields | 18 | Width u32, height u32, channels u8 (3 or 4), frame side u8, mosaic payload length u32, mosaic payload CRC32 |
| Header CRC32 | 4 | CRC32 of the eighteen header-field bytes |
| Directory | 8 per frame | Payload length u32, payload CRC32 u32; frames in raster order |
| Directory CRC32 | 4 | CRC32 of the directory entries |
| Mosaic | mosaic length | The mosaic layer's payload |
| Payloads | | One per frame, in raster order, back to back |
| Mosaic, again | mosaic length | The same bytes as the mosaic payload |
| Directory, again | 8 per frame + 4 | The same bytes as the directory and its CRC32 |
| Header fields, again | 22 | The same bytes as the header fields and their CRC32 |

Width and height are at least 1 and width × height × channels is at most 2³¹.
The frame side as written is 128; a reader honours any side from 16 to 255. The
frame grid is `ceil(width / side) × ceil(height / side)` frames in raster order,
the last column and row of frames being smaller when the dimensions are not
multiples of the side.

A reader takes the header fields from the front copy when its CRC32 matches,
otherwise from the trailing copy, and rejects the stream when neither matches.
It takes the directory and the mosaic the same way, requires the payload
lengths to account for exactly the bytes between the two mosaic copies, and
checks each payload's CRC32 before reconstructing its frame. A frame whose
checksum fails is reported by index; the other frames remain decodable, and a
salvaging decoder fills the lost frame with the mosaic colours of its cells.
Damage confined to one copy of the header, directory or mosaic therefore costs
nothing; the duplicated index costs `8 × frames + 30` bytes over a single copy
and the mosaic its coded size twice.

### Mosaic layer

The mosaic is the mean colour of every 16×16 cell of the image, cells at the
right and bottom edges being smaller: for each channel the integer sum over
the cell plus half the cell's pixel count, divided by the pixel count. It is an
image of `ceil(width / 16) × ceil(height / 16)` pixels with the image's channel
count, coded by the frame engine as a single frame of its own size (fresh
models, image-edge boundary rules), and its payload is stored twice. It is the
first picture of a progressive decode and the fill for lost frames.

### Frame payload

A frame's payload is the frame coded as an image of its own: the adaptive
models, the binary range coder and the repetition table start fresh for every
frame, and the frame's top and left edges take the image-edge boundary rules.
Nothing in a frame depends on another frame.

Within the frame, pixels are coded in raster order by one fixed construction:
median-edge prediction per plane; a sub-integer phase context from the
extrapolation of a cleaned causal plane, through exact window moments and a
fixed Q24 coefficient table; an activity measure from the causal residual
energy that gates direct RGB coding against an exactly reversible modular
opponent-colour coding; a hash of the four neighbouring pixels predicting
exact repetitions through a bounded direct-mapped table; and binarization of
every residual (zero flag, sign, exp-Golomb prefix, suffix bits) into adaptive
binary contexts coded by a 32-bit range coder that ends with five flush bytes.
Bytes read past the end of a payload are zero.

The normative description of the per-pixel construction is the Python
reference [`experiments/version7/reference.py`](../experiments/version7/reference.py)
with its frame engine `reference_frame_engine.py` and the moment coefficient
table `phase_moment_coefficients.json`; the field-by-field account is
[`experiments/version7/FORMAT.md`](../experiments/version7/FORMAT.md) together
with the per-pixel section of
[`experiments/version6/FORMAT.md`](../experiments/version6/FORMAT.md). The C++
kernel is byte-identical to the reference on the retained vectors and the
measurement corpus. There are no transmitted tables, no selectable modes and
no encoder parameters: a given image has exactly one `CNGPHI08` encoding at a
given frame side.

## 3. Collections

A collection uses PNG-style chunks: length u32, four-character type, payload,
CRC32 of type and payload. The signature is `89 43 4E 47 0D 0A 1A 0A`. Chunk
order is `SHDR`, `DIRE`, one or more `IMAG` chunks, then an empty `SEND`.

`SHDR` is six u32 fields: version 0, form, image count, canvas width, canvas
height and canvas channels. Form 0 is packed; form 1 is composite. Image count
is between 1 and 4,096. Packed canvas fields are zero.

`DIRE` contains exactly one 40-byte entry per image:

| Offset within entry | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 4 | Unique image identity |
| 4 | 4 | Original width |
| 8 | 4 | Original height |
| 12 | 4 | Original channel count |
| 16 | 4 | Slice x |
| 20 | 4 | Slice y |
| 24 | 8 | Absolute offset of the IMAG chunk's length field |
| 32 | 8 | Total IMAG chunk length, including its 12 framing bytes |

Each `IMAG` payload is a complete standalone `CNGPHI08` stream. In packed form,
each entry refers to its own IMAG chunk and has slice origin `(0,0)`. In
composite form, all entries refer to one IMAG chunk holding the canvas: members
are stacked vertically at x = 0, the canvas width is the largest member width
and its height the sum of member heights, pixels to the right of a narrower
member are zero, and an RGB member in an RGBA canvas receives alpha 255. The
directory preserves original dimensions and channels, so extraction removes only
padding and synthetic alpha. The reader validates the vertical layout and
decodes the complete canvas before slicing.

Collection members and the composite canvas are limited to 65,536 on a side;
with `PW` and `PH` the width and height rounded up to multiples of 128,
`PW × PH ≤ 4,194,304` and `PW + PH ≤ 65,536`. A canvas that exceeds these
limits is rejected rather than silently switched to packed form.

`SEND` is empty and ends the collection. The directory and image chunks are
contiguous in declared order, with no trailing bytes. Framing overhead is
`68 + 52N` bytes for packed form and `80 + 40N` for composite form, in addition
to the embedded standalone stream or streams.

## 4. Compatibility boundary

This release reads and writes `CNGPHI08` streams and SHDR v0 collections that
embed them. Streams with another signature, including the `CSP` v2 and v3
streams of releases 0.1 and 0.2 and the `CNGPHI07` streams of release 0.3,
are rejected at the header before any pixel is produced; a collection embedding such a stream is rejected when that
stream is reached. Migration is pixel-preserving: decode with the release that
wrote the file and encode the exact raw samples with this one.

Unknown signatures, channel counts, frame sides and chunks are rejected. Wider
sample types, metadata, preview layers and animation require deliberate format
evolution, as does any change to the per-pixel construction.
