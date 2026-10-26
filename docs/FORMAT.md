# CNG 1.0 file format

**Format version:** 1 · **File extension:** `.cng` · **Status:** frozen

This document specifies CNG (Computational Network Graphics) image files, format version 1. A CNG file holds one
lossless 8-bit BGR or BGRA image. It is cut into independently coded segments of 128 × 32 pixels, carries a coarse
mosaic of the image, and stores its header, directory, tables and mosaic twice, so that damage to part of a file
costs only the segments it touches.

The decoder defined here is normative: a conforming decoder reconstructs every sample of every valid file exactly,
and rejects every file this document does not define. The encoder is not normative. Any encoder whose output a
conforming decoder reads is conforming; section 11 describes the reference encoder.

Collections of images are a separate container, specified in [COLLECTIONS.md](COLLECTIONS.md).

## 1. Conventions

- Integers are unsigned and big-endian unless stated otherwise. `u8`, `u16` and `u32` are 1, 2 and 4 bytes.
- CRC32 is the ISO-HDLC CRC-32 (reflected polynomial `0xEDB88320`, initial value and final XOR `0xFFFFFFFF`), the
  value zlib's `crc32` returns, stored as `u32`. It detects accidental damage; it does not authenticate.
- `x / y` is integer division rounding toward zero; `ceil(x / y)` rounds up. `x mod 256` is the non-negative
  remainder. `wrap(v)` is the signed byte with the same low eight bits as `v`: `((v + 128) mod 256) - 128`.
- `zigzag(v)` maps 0, −1, 1, −2, 2, … to 0, 1, 2, 3, 4, …; `unzigzag(s)` is its inverse: `s / 2` when `s` is even,
  `-(s + 1) / 2` when odd.
- Pixel `(x, y)` has column `x` and row `y`, both from 0 at the top left. Raster order is row by row, top to bottom,
  each row left to right.

## 2. Image model

An image has a width and a height of at least 1 and either 3 channels or 4. Samples are 8-bit and stored in the
order blue, green, red (B, G, R) or blue, green, red, alpha (B, G, R, A), interleaved per pixel; a decoder delivers
them in that order. The total sample count, width × height × channels, is at most 2³¹. Samples are reconstructed exactly,
including the colour channels of pixels whose alpha is 0. The format carries no colour space, gamma, profile,
metadata or animation: the samples are the image.

## 3. File layout

| Offset | Part | Bytes |
| --- | --- | --- |
| 0 | Signature | 8 |
| 8 | Header fields | 28 |
| 36 | Header CRC32 | 4 |
| 40 | Directory | 8 × N + 4 |
| | Tables | T |
| | Mosaic | M |
| | Segment payloads | Σ |
| | Mosaic, again | M |
| | Tables, again (only when N ≥ 2) | T |
| | Directory, again | 8 × N + 4 |
| | Header fields and CRC32, again | 32 |

N is the number of segments (section 4). The second copies are byte-for-byte identical to the first.

### 3.1 Signature

The eight bytes `89 43 4E 47 0D 0A 1A 0A`: a byte with the high bit set, `CNG` in ASCII, CR LF, Ctrl-Z and LF.
Transfer that strips the high bit, translates line endings or truncates at Ctrl-Z changes the signature, and the
file is rejected.

### 3.2 Header fields

| Offset | Field | Type | Value |
| ---: | --- | --- | --- |
| 0 | Width | u32 | at least 1 |
| 4 | Height | u32 | at least 1 |
| 8 | Channels | u8 | 3 or 4 |
| 9 | Format version | u8 | 1 |
| 10 | Segment width | u8 | 128 |
| 11 | Segment height | u8 | 32 |
| 12 | Mosaic length M | u32 | |
| 16 | Mosaic CRC32 | u32 | CRC32 of the M mosaic bytes |
| 20 | Tables length T | u32 | |
| 24 | Tables CRC32 | u32 | CRC32 of the T tables bytes |

The header CRC32 follows the 28 bytes and covers them. A decoder of format version 1 rejects a file whose version
is not 1 or whose segment sizes are not 128 and 32 as unsupported, rejects a file whose width, height or channels
are out of range or whose sample count exceeds 2³¹, and otherwise reads the file as specified here.

### 3.3 Directory

One entry per segment, in segment order: the segment payload's length (u32) and its CRC32 (u32). The CRC32 of the
8 × N entry bytes follows.

### 3.4 Choosing copies

A decoder takes:

1. the header fields from the front copy (offset 8) when its CRC32 matches, otherwise from the trailing copy (the
   last 32 bytes) when its CRC32 matches; otherwise the file is rejected;
2. the directory from the front copy (offset 40) when its CRC32 matches, otherwise from the trailing copy (the
   8 × N + 4 bytes before the trailing header); otherwise the file is rejected;
3. the tables from the front copy when its CRC32 equals the header's tables CRC32, otherwise from the second copy
   when N ≥ 2 and it matches; otherwise the file is rejected;
4. the mosaic from the front copy when its CRC32 equals the header's mosaic CRC32, otherwise from the second copy;
   when neither matches, the mosaic is unavailable, which does not prevent decoding the segments.

The payload region begins after the first mosaic copy, at offset 40 + (8 × N + 4) + T + M, and ends where the
second mosaic copy begins, at the file length − 32 − (8 × N + 4) − (T when N ≥ 2, else 0) − M. The payloads lie
back to back in segment order and their directory lengths must account for exactly that region; otherwise the
file is rejected. A segment whose payload CRC32 does not match its directory entry is damaged.

## 4. Segments

The image is cut into segments of 128 columns and 32 rows: segment `(i, j)` covers columns `128 i` to
`min(128 i + 128, width) − 1` and rows `32 j` to `min(32 j + 32, height) − 1`, for `0 ≤ i < ceil(width / 128)` and
`0 ≤ j < ceil(height / 32)`. Segment order is raster order of `(i, j)`: segment index `j × ceil(width / 128) + i`.
N = ceil(width / 128) × ceil(height / 32).

Every segment is decoded on its own, as an image of its own dimensions: nothing in a segment depends on any other
segment, only on the tables. Section 7 defines the decoding of one segment, which this document calls a tile; the
mosaic (section 9) is decoded by the same procedure.

## 5. Tables

The tables part is one Zstandard frame (RFC 8878). It decompresses to the tables body, which is the concatenation of
38 context tables, in context order (section 7.4):

| Field | Type |
| --- | --- |
| Entry count minus one, E | u16, at most 572 |
| Quantized frequencies q[0], …, q[E] | E + 1 bytes, each at most 25 |

The body must be exactly this long; any other body, a q byte above 25, or a frame that does not decompress makes
the tables invalid. Entries 0 to E − 1 are the context's listed symbols 0 to E − 1; entry E is the escape.

### 5.1 Frequencies

The frequencies of a context are computed from its q bytes with integer arithmetic only:

1. `level[q] = 0` for q = 0, else the q-th value of 1, 1, 2, 3, 4, 6, 8, 11, 16, 23, 32, 45, 64, 91, 128, 181, 256,
   362, 512, 724, 1024, 1448, 2048, 2896, 4096 (`max(1, round(2^((q − 1) / 2)))`).
2. `total = Σ level[q[k]]`. When total is 0 the context is unused: it has no frequencies, and decoding any symbol
   in it makes the segment invalid.
3. `scaled[k] = 0` when `level[q[k]] = 0`, else `max(1, level[q[k]] × 4096 / total)`.
4. Order the entries by descending `scaled`, equal values in increasing entry index.
5. `difference = 4096 − Σ scaled`. Visiting entries in that order cyclically: while difference ≠ 0, at entry k, if
   difference > 0, add all of it to `scaled[k]` and stop; otherwise, if `scaled[k] > 1`, move
   `min(scaled[k] − 1, −difference)` from `scaled[k]` to difference.
6. The frequencies `f[k] = scaled[k]` sum to 4096. `start[k] = Σ f[i]` for `i < k`.

### 5.2 Escapes

Decoding symbol E (the escape) in a context means the symbol was not listed: a raw value of `b` bits follows,
where b is 10 for contexts 0–4 (green), 6 for context 37 (distances) and 9 for every other context. The raw value is
the decoded symbol. An escaped value that is not a valid symbol for its use makes the segment invalid.

## 6. Segment payloads and rANS streams

A tile with C channels has C rANS streams, one per plane, in the order green, blue, red, alpha. Its payload is the
byte lengths of the first C − 1 streams as unsigned LEB128 (little-endian base-128, at most five bytes each), then
the C streams back to back; the last stream takes the remaining bytes. Lengths that exceed the payload make the
segment invalid.

Each stream is decoded with a 32-bit state `x`:

- **Start:** `x` is the stream's first four bytes, big-endian. A stream shorter than four bytes is invalid.
- **Symbol** in a context with frequencies f and starts s: `slot = x mod 4096`; the symbol is the entry k with
  `s[k] ≤ slot < s[k] + f[k]`; then `x = f[k] × (x / 4096) + slot − s[k]`, and the state is renormalized.
- **Raw value of n bits** (1 ≤ n ≤ 12): `g = 4096 / 2ⁿ`, `slot = x mod 4096`, value `= slot / g`, then
  `x = g × (x / 4096) + slot − value × g`, and the state is renormalized. A read of 0 bits is the value 0 and does
  not touch the state. A read of more than 12 bits is invalid.
- **Renormalization:** while `x < 2²³`: `x = x × 256 + next byte`. Running out of bytes makes the segment invalid.
- **End:** after the tile's last pixel, every stream of the tile must have consumed all of its bytes and have
  `x = 2²³` exactly.

Every symbol of a plane is read from that plane's stream, so the streams interleave pixel by pixel as section 7
reads them.

## 7. Decoding a tile

A tile of width w, height h and C channels is decoded pixel by pixel in raster order. For each pixel, index
`i = y × w + x` within the tile, the decoder determines a residual `r_c` for every plane c and the sample
`s_c = (p_c + r_c) mod 256`, where `p_c` is the plane's prediction. Planes are numbered in sample order: 0 blue, 1 green,
2 red, 3 alpha. They are processed in the order green, blue, red, alpha.

### 7.1 Prediction

For each plane, with samples of the same plane within the tile:

- `U` = the sample above, or 0 in the tile's top row;
- `L` = the sample to the left, or `U` in the tile's left column;
- `UL` = the sample above and to the left, or `U` in the top row or the left column.

The prediction is the median edge detector: with `hi = max(L, U)` and `lo = min(L, U)`, `p = lo` when `UL ≥ hi`,
`p = hi` when `UL ≤ lo`, else `p = L + U − UL`.

### 7.2 Energies and classes

After a pixel is reconstructed, its residuals give three energies: `e_g = |r_1|`, `e_b = max(|r_1|, |r_0|)`,
`e_r = max(|r_1|, |r_2|)`. For an energy plane `e`, the activity of a pixel is the sum of the energies of six
earlier pixels of the tile: left, two left, above, two above, above-left and above-right, each 0 when outside the
tile. Its class is the number of the thresholds 0, 3, 12 and 48 that the activity exceeds: 0 to 4. The green class
`k_g` uses `e_g`, the blue class `k_b` uses `e_b`, the red class `k_r` uses `e_r`.

### 7.3 The cache

The tile keeps, for every value 0–255, the index of the last earlier pixel whose green sample has that value, or
none. Every reconstructed pixel, of any kind, then records its own index under its green sample.

### 7.4 Contexts

| Context | Use |
| ---: | --- |
| 0–4 | Green symbol: `k_g` |
| 5–19 | Blue symbol: `5 + 3 k_b + bucket` |
| 20–34 | Red symbol: `20 + 3 k_r + bucket` |
| 35, 36 | Alpha symbol: 35 when the alpha samples L, U and UL (section 7.1 rules) are equal, else 36 |
| 37 | Distance symbol |

`bucket` is 0 when the pixel's green residual is 0, 1 when it is ±1 or ±2, and 2 otherwise.

### 7.5 Alphabets

Green symbols, 572:

| Symbols | Meaning |
| --- | --- |
| 0–255 | Literal green residual `unzigzag(s)` |
| 256–285 | Whole-pixel match, length code `s − 256` |
| 286–541 | Cache hit, green residual `unzigzag(s − 286)` |
| 542–571 | Green match, length code `s − 542` |

Blue, red and alpha symbols, 286:

| Symbols | Meaning |
| --- | --- |
| 0–255 | Literal: `v = unzigzag(s)` |
| 256–285 | Plane match, length code `s − 256` |

A blue literal's residual is `wrap(v + r_1)` when `k_b ≥ 3`, else `v`; a red literal's is `wrap(v + r_1)` when
`k_r ≥ 3`, else `v`; an alpha literal's is `v`. Here `r_1` is the pixel's green residual.

### 7.6 Lengths and distances

A length code `c` (0–29) is followed by `extra(c)` raw bits: `extra(c) = 0` for `c < 4`, else `c / 2 − 1`. Its
value is `c` for `c < 4`, else `((2 + (c mod 2)) × 2^(c / 2 − 1)) + bits`; the match length is the value plus 2.

A match then reads one distance symbol `t` (context 37) from the same stream:

| t | Distance |
| --- | --- |
| 0–2 | The t-th entry of the match kind's repeat list |
| 3–8 | Entry t − 3 of the specials 1, w, w − 1, w + 1, 2, 2w |
| 9–38 | Prefix code `c = t − 9` with `extra(c)` raw bits, as for lengths: distance = value + 1 |

A distance symbol above 38 is invalid. Repeat lists hold three distances, most recent first, and start as
(1, w, 2) in every tile. Using distance d moves it to the front: if d is in the list, its first occurrence is
removed and d is inserted at the front; otherwise the last entry is dropped and d is inserted at the front. There
is one list for whole-pixel matches and one for each plane's own matches (the green match list belongs to green).

A match at pixel i with distance d and length L is invalid unless `1 ≤ d ≤ i` and `L ≤ w × h − i`.

### 7.7 Pixel procedure

Each plane has a match counter, and the tile has a whole-pixel counter, all 0 at the start of the tile. For pixel i:

1. **Whole-pixel copy.** If the whole-pixel counter is positive, every plane's residual is the residual of pixel
   `i − d_w` in that plane (`d_w` the whole-pixel distance), and the counter decreases by 1. Go to step 4.
2. **Green.** If green's match counter is positive, green's residual is green's residual at `i − d_1` and the
   counter decreases by 1. Otherwise read a green symbol:
   - a literal gives green's residual;
   - a whole-pixel match reads its length L and distance `d_w`, sets the whole-pixel counter to `L − 1`, and every
     plane's residual of this pixel is that plane's residual at `i − d_w`; go to step 4;
   - a cache hit gives green's residual `g`; the green sample `(p_1 + g) mod 256` must have a recorded pixel j
     (section 7.3), otherwise the tile is invalid; every other plane's sample is pixel j's sample in that plane and
     its residual is `wrap(sample − p_c)`; go to step 4;
   - a green match reads its length L and distance `d_1`, sets green's match counter to `L − 1`, and green's
     residual is green's residual at `i − d_1`.
3. **Blue**, then **red**, then **alpha** (4-channel images): if the plane's match counter is positive, the
   residual is the plane's residual at `i − d_c` and the counter decreases by 1; otherwise read a symbol from the
   plane's stream: a literal gives the residual (section 7.5); a plane match reads its length L and distance `d_c`,
   sets the counter to `L − 1`, and the residual is the plane's residual at `i − d_c`.
4. **Reconstruct:** `s_c = (p_c + r_c) mod 256` for every plane whose sample a cache hit did not supply; record the
   energies (section 7.2) from the residuals and the cache entry (section 7.3) from the green sample.

A pixel decoded by a whole-pixel copy, a whole-pixel match or a cache hit leaves every plane match counter
unchanged: a plane match pauses over such pixels and resumes after them. Every residual lies in −128 to 127.

After the last pixel, every counter must be 0 and every stream must have ended (section 6).

## 8. Decoding an image

A decoder reads the container (section 3), builds the 38 tables (section 5), and decodes every segment (section 7)
into its rectangle of the image. A segment that is damaged (section 3.4) or invalid makes the decode fail.
Segments are independent and may be decoded in any order or concurrently.

## 9. Mosaic

The mosaic is an image of `ceil(width / 16) × ceil(height / 16)` pixels with the image's channel count. Mosaic pixel
`(u, v)` holds, for every channel, the mean of the image samples in the cell of columns `16u` to `16u + 15` and
rows `16v` to `16v + 15` that lie inside the image: `(sum + count / 2) / count`, with count the number of image
pixels in the cell.

Its payload is coded exactly as one tile of the mosaic's dimensions (section 7), with the same tables, however wide
or tall the mosaic is. It is the first picture of a progressive display: it is read before any segment, and it is a
small fraction of the file.

## 10. Salvage

A decoder may salvage a damaged file. It needs one valid copy each of the header fields, the directory and the
tables; it ignores the signature. It decodes every segment whose payload CRC32 matches and that decodes validly,
and fills every pixel of every other segment with the mosaic pixel of the cell that contains it, or with the value
128 in every channel when the mosaic is unavailable. It reports which segments decoded exactly.

## 11. The reference encoder (informative)

The reference encoder makes one fixed sequence of decisions for every image; nothing is chosen per image beyond
what the data determines.

1. Every tile (the segments in order, then the mosaic) is parsed greedily under an older shared-class context model,
   and the symbol counts give a first set of code lengths.
2. Twice, every tile is planned under the current code lengths: each plane is parsed on its own by a cost-aware
   dynamic programme over literals and plane matches; whole-pixel matches are then parsed over the packed residuals
   and win where they cost less than the planes' parse, as do cache hits; per-plane matches are then parsed with
   those pixels excluded. The planned tiles are emitted as plane symbol streams, and their symbol counts give the
   next code lengths. Candidate matches come from the repeat lists, the specials and the 16 most recent positions
   of the same four values.
3. From the counts of the last emission, every context lists its symbols up to the last one used more than twice;
   symbols used at most twice are escaped. Each listed count and the escape count are quantized to
   `1 + clamp(round(2 log2(4096 × count / total)), 0, 24)` (1 when the share is below 1/4096; ties to even). The
   tables body is compressed into one Zstandard frame at level 19 with no content size, checksum or dictionary id.
4. Every plane stream is rANS coded in reverse, with byte renormalization, and the container is assembled.

The reference encoder's output does not depend on the number of threads it uses. Given the same Zstandard
release it writes the same bytes on every platform.

## 12. Conformance

The `tests/vectors` folder holds files with their exact samples. A conforming decoder decodes each to its samples
and decodes each file's mosaic to the block means of those samples. A conforming encoder's output decodes to the
samples it was given.

## 13. Identification

Files use the extension `.cng`. The signature identifies the format; the format version field identifies the
revision. A later revision that a format version 1 decoder must not read will change the version field.
