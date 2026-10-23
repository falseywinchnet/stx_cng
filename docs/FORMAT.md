# CNG format reference

**Release:** 0.1.0-rc1 · **Standalone schema:** CSP v2 · **Collection schema:** SHDR v0

This document describes the bytes implemented by this release. CNG means
**Computational Network Graphics**. The standalone `CSP` signature is retained
from development to preserve compatibility with existing files; a CNG collection
has the `CNG` signature. This release does not silently rewrite either signature.

The format is lossless for interleaved RGB8 and RGBA8 samples. It carries neither
colour-profile metadata nor an implicit conversion to a different colour space.
The coding transform is reversible integer arithmetic on sample bytes.

## 1. Conventions and framing

Unless stated otherwise, integers are unsigned and big-endian. Offsets are byte
offsets from the beginning of the containing file. A chunk is:

| Field | Bytes | Meaning |
| --- | ---: | --- |
| Length | 4 | Payload length, excluding type and checksum |
| Type | 4 | Exact ASCII chunk identifier |
| Payload | Length | Chunk-specific data |
| CRC32 | 4 | CRC of type followed by payload |

The checksum uses the CRC32 convention supplied by zlib's `crc32`, initialized by
`crc32(0, type, 4)` and continued over the payload. Length bytes are excluded.
Checksums are stored big-endian. A checksum detects accidental corruption; it is
not authentication.

Chunk order is fixed. The current readers reject unexpected chunks and trailing
data. PNG-style framing does not imply PNG ancillary-chunk semantics or PNG
compatibility.

## 2. Standalone image

The eight-byte signature is `89 43 53 50 0D 0A 1A 0A` in hexadecimal.
It is followed by `CHDR`, one or more `BAND` chunks, and an empty `CEND`.

### CHDR: image header

The payload is exactly 16 bytes.

| Offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 1 | Version: 2 |
| 1 | 1 | Channels: 3 for RGB, 4 for RGBA |
| 2 | 1 | Flags: 0 |
| 3 | 1 | Reserved: 0 |
| 4 | 4 | Width |
| 8 | 4 | Height |
| 12 | 4 | Band budget: 1,048,576 |

Width and height are positive and at most 65,536. Let `PW` and `PH` be width and
height rounded upward to multiples of 128. Admission requires
`PW * PH <= 4,194,304` and `PW + PH <= 65,536`. This is a dimension limit;
rounded-up pixels are not encoded or reconstructed.

For width `W`, channels `C`, and budget `B`, the number of full rows per band is:

```text
rows = floor((B - C) / (W * C))
```

Bands follow raster order. The final band contains the remaining rows. There are
no per-band dimensions in the file; the header determines each band's extent.
Pixel-copy history resets at each band. Gradient prediction retains the previous
original row; above the first image row, that row is all zero.

### BAND: instructions and literals

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 4 | Sequence count `N` |
| 4 | 4 | Literal pixel count `L` |
| 8 | 4 | Compressed sequence-frame length `S` |
| 12 | S | Zstandard sequence frame |
| 12 + S | 1 | Literal coding: 0 stored, 1 compressed |
| 13 + S | Remainder | Literal payload |

The sequence frame decompresses to exactly `12 * N` bytes. Its records have
three unsigned 32-bit fields: literal count, run length, and distance, all measured
in **pixels**. The raw frame is byte-planar. For sequence `i`, field `f` and
little-endian byte position `b`, the byte is at `(4*f + b)*N + i`. This ordering
is the deliberate exception to the outer big-endian integer convention.

Each sequence emits its literals first, then its run:

- Positive length and positive distance copy earlier pixels in the current band.
  References may overlap; repeated output grows from the reconstructed prefix.
- Positive length and distance zero emit exact gradient predictions.
- The final sequence has length zero and distance zero. It may carry trailing
  literals. No earlier sequence may have zero length.

Nonterminal runs contain at least four pixels. A copy distance is at most 262,144
pixels and cannot exceed the number already produced in that band. Sequence
counts, literal consumption and total output must agree exactly with the band.
The reference decoder requires `1 <= N <= floor(band_pixels/4) + 1`.

### Gradient prediction

For each channel, prediction is `(left + above - upper_left) mod 256`.
At column zero, `left` and `upper_left` are zero. Above the first image row,
`above` is zero; later bands use the saved previous row.

Compressed literals encode `(sample - prediction) mod 256`. Prediction runs
supply zero residual. Stored literals instead contain **original samples**, so
they bypass residual inversion. Copy runs always copy original sample pixels.

### Stored literals: coding 0

The payload is exactly `L * C` original sample bytes, interleaved by pixel in
literal-consumption order. Zero literals use this coding with an empty payload.

### Compressed literals: coding 1

The literal payload is one Zstandard frame. After decompression, reverse-tail
run separation is inverted to obtain `2*C*ceil(L/2)` packed bytes. Those bytes
contain paired nibbles of zigzag-mapped colour residuals.

For each literal's three colour residuals, first interpret each modular byte as
a signed value in `[-128,127]`. Define `wrap(v) = ((v + 128) mod 256) - 128`,
where modulo gives a nonnegative remainder, and use floor division throughout:

```text
difference = wrap(red - green)
second     = wrap(blue - green - floor(difference / 2))
centre     = wrap(green + floor(difference / 2))
third      = floor((second + (difference mod 2) + 1) / 3)
brightness = wrap(centre + third)
```

The transformed coordinates are `brightness, difference, second`. Alpha, when
present, retains its signed residual. The inverse is:

```text
third  = floor((second + (difference mod 2) + 1) / 3)
centre = wrap(brightness - third)
green  = wrap(centre - floor(difference / 2))
red    = wrap(green + difference)
blue   = wrap(green + second + floor(difference / 2))
```

Map each signed coordinate `v` to `2*v` when nonnegative, otherwise `-2*v - 1`.
For `G = ceil(L/2)`, channel `c` has its low-nibble plane at offset `2*c*G` and
high-nibble plane at `(2*c + 1)*G`. Literal `i` uses byte `floor(i/2)` in each
plane, shifted by `4*(i mod 2)`. Even literals occupy the low nibble of each
packed byte; odd literals occupy the high nibble. The encoder zeroes unused
padding for an odd literal count. Only the declared `L` pixels are reconstructed.

### Reverse-tail run separation

The decompressed literal frame consists of un-repeated bytes at the front and
descriptors consumed backward from the end. A descriptor integer is read from
right to left using seven payload bits per byte, least-significant group first;
bit 7 requests another byte. The reference reader admits at most five groups and
rejects a redundant zero final group.

For descriptor value `code`, `length = floor(code/2) + 1`:

- Even `code`: copy `length` bytes from the front literal cursor.
- Odd `code`: consume one more byte backward as the repeated value and emit it
  `length` times. Repeated runs have length at least four.

Stop at the exact packed-byte count. The front and back cursors must then meet.
The decompressed frame is bounded by twice the packed size plus four bytes.

### CEND: termination

`CEND` has no payload and is followed immediately by end of input.

## 3. Collections

The signature is `89 43 4E 47 0D 0A 1A 0A`. Chunk order is `SHDR`, `DIRE`,
one or more `IMAG` chunks, then an empty `SEND`.

`SHDR` is six big-endian u32 fields: version 0, form, image count, canvas width,
canvas height, and canvas channels. Form 0 is packed; form 1 is composite.
Image count is between 1 and 4,096. Packed canvas fields are zero. Composite
canvas dimensions obey the standalone image limits.

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

Each `IMAG` payload is a complete standalone CSP v2 file, including its own
signature and checksums. In packed form, each entry refers to its own contiguous
IMAG chunk and has slice origin `(0,0)`. In composite form, all entries refer to
one IMAG chunk. The reference layout has `x = 0` and consecutive vertical slices
covering the canvas height. Slice width may be smaller than canvas width.

The composite encoder uses maximum member width and summed member heights.
It preserves RGB when all members are RGB, otherwise uses RGBA with alpha 255
for promoted RGB samples. Right-side padding is zero. The directory preserves
original dimensions and channels, so extraction removes only padding and
synthetic alpha. The current reader validates vertical layout rather than
arbitrary rectangles. Complete decoding precedes slicing.

`SEND` is empty and ends the collection. The directory and image chunks are
contiguous in declared order, with no trailing bytes. Complete framing overhead
is `68 + 52*N` bytes for packed form and `80 + 40*N` for composite form, in
addition to the complete embedded standalone file or files.

## 4. Compatibility boundary

This specification describes the release candidate, including its limits.
Unknown flags, versions and chunks are rejected. Wider dimensions, metadata,
preview layers and new sample types require deliberate format evolution.
Encoder match-search choices may change without changing the instruction grammar;
a decoder must implement the grammar, not depend on one encoder's greedy choices.
