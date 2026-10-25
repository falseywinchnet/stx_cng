# CNG

### Computational Network Graphics

CNG is a lossless image format for RGB8 and RGBA8 images and image sets, with a
portable C++20 reference implementation, **stx_cng**. Every sample is
reconstructed exactly, including the colour of fully transparent pixels. Images
are coded in independent 128×128 frames: frames decode in any order and in
parallel, every frame carries a checksum, and a mosaic layer of 16×16-cell mean
colours gives a first picture from the first few percent of the file and fills
any frame that damage has taken. The header, directory and mosaic are stored
twice, so damage to one copy is repaired from the other.

[Format](docs/FORMAT.md) · [Collections](docs/COLLECTIONS.md) · [API](docs/API.md) ·
[Build](docs/BUILDING.md) · [Measurements](docs/MEASUREMENTS.md) · [MIT license](LICENSE)

## Features

| | |
| --- | --- |
| Samples | RGB8 and RGBA8, interleaved, reconstructed bit for bit |
| Alpha | Preserved as stored; the RGB beneath alpha 0 survives |
| Dimensions | Any width and height with at most 2³¹ samples |
| Coding | Phase-aware causal prediction, repetition prediction, adaptive binary arithmetic coding |
| Construction | One fixed construction for every image: no image-specific models, modes or transmitted tables |
| Frames | Independent 128×128 frames, each with its own CRC32; decodable in any order and in parallel |
| Progressive | A 16×16-cell mosaic layer, decodable from the first 3–5% of the file |
| Integrity | Header, directory and mosaic checksummed and stored twice; a damaged frame is reported, or filled from the mosaic by the salvage decoder, while the others decode exactly |
| Collections | A packed set of standalone images, or one composite canvas sliced into its members |
| Decoding | Into caller-owned storage, or streamed; mosaic alone; salvage with per-frame report |
| Implementation | C++20, portable; no platform intrinsics |
| Dependencies | zlib for collection checksums |
| License | MIT |

## Size against optimized PNG

Complete files, all framing counted, against OptiPNG at its maximum
optimization level (every zlib level, strategy and filter; metadata stripped),
over 236 images.

| Image set | CNG / OptiPNG size | Smaller than OptiPNG |
| --- | ---: | ---: |
| Photographs (24) | 0.76 | 24 of 24 |
| Screen captures (14) | 0.70 | 13 of 14 |
| Smooth graphics (10) | 0.61 | 9 of 10 |
| Mixed photographic and graphic content (13) | 0.84 | 7 of 13 |
| Noise (23) | 1.02 | 6 of 23 |
| 128 px emoji (96) | 1.18 | 11 of 96 |
| Small periodic tile patterns (56) | 1.94 | 1 of 56 |
| All (236) | 0.79 | 71 of 236 |

CNG is smaller wherever an image has photographic, smooth or rendered
content. It is larger on pure noise, on palette-like emoji and on small
images made of an exactly repeating tile, where PNG's byte matching is the
better model. The mosaic layer, stored twice, is 1.5% of these bytes overall
and 7% on a 128 px emoji, where it is as large as the index. Per-image byte counts are in [Measurements](docs/MEASUREMENTS.md).

## Time against PNG

| | CNG / PNG level 9 |
| --- | ---: |
| Encode, photographs | 0.9–1.0 |
| Encode, smooth and rendered content | 0.2–0.5 |
| Decode, one thread | 8–11 |
| Decode, four threads | 3–5 |

OptiPNG's maximum level takes many times longer than PNG level 9 to encode and
produces files that decode in the same time as PNG. CNG's encoder and
decoder do the same work, so a decode costs about as long as an encode on one
thread; frames are independent, so decode time divides by the threads used.

## How an image is coded

The image is cut into 128×128 frames in raster order; the last column and
row of frames are smaller when the dimensions are not multiples of 128. Each
frame is coded on its own, with its adaptive state, arithmetic coder and
repetition table started fresh, and its top and left edges treated as image
edges, so no frame depends on another.

Within a frame, each pixel is predicted from its causal neighbours. The
residual's rounding is predicted from a sub-integer phase estimate obtained
by extrapolating a cleaned local plane, so smooth content costs a fraction
of a bit per sample instead of the bit that integer prediction leaves as
noise. Local activity selects the coding of the three colours directly or in
an exactly reversible opponent-colour form, a hash of the four neighbouring
pixels predicts exact repetitions, and every decision is coded by an adaptive
binary arithmetic coder. The construction is the same for every image.

The mosaic layer is the mean colour of every 16×16 cell, coded as a small
image by the same engine. A file is a signature, the header fields with their
CRC32, a directory of frame lengths and CRC32s with its own CRC32, the mosaic,
the frame payloads, and then the mosaic, directory and header fields again.
The decoder takes whichever copy of each checks, validates each frame before
reconstructing it, and either reports a frame whose checksum fails or, in the
salvage decoder, fills it with the mosaic colours of its cells while the other
frames decode exactly.

## Collections

A set of 1–4,096 images is stored in one of two forms, chosen by the caller:

| | Packed | Composite |
| --- | --- | --- |
| Payload | One standalone file per member | One standalone file for a canvas holding every member |
| Coding across members | None | Within the canvas's frames |
| Decoding | Each member on its own | The canvas, then each member's rectangle |
| Suited to | Independent images | Related glyphs, controls and artwork |

Members keep their original dimensions and channel counts. A composite canvas
is RGB when every member is RGB, otherwise RGBA. The directory records each
member's rectangle, so extraction restores the member exactly.

## Library

```sh
git clone https://github.com/falseywinchnet/stx_cng.git
cd stx_cng
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Link `stx_cng::stx_cng` from CMake. The public headers are
[`stx_cng/codec.hpp`](include/stx_cng/codec.hpp) and
[`stx_cng/collection.hpp`](include/stx_cng/collection.hpp).

```cpp
stx_cng::Dimensions dimensions{.height = 480, .width = 640, .channels = 4};

stx_cng::Measurements encoding{};
stx_cng::encode(raw_input, encoded_output, dimensions, encoding);

stx_cng::Measurements decoding{};
stx_cng::Dimensions restored = stx_cng::decode(encoded_input, raw_output, decoding);
```

Samples are interleaved rows with no padding. The codec performs no colour
conversion and no premultiplication. Arguments are borrowed for the duration of
a call only. `decode_into` writes into a caller-supplied span of the exact size;
the stream decoder serves the case where dimensions are not known in advance.
`decode_mosaic` returns the mosaic layer alone, and `decode_salvage` decodes a
damaged file as far as it goes, reporting which frames were lost.

The exact Python reference of the codec, its bit-exact vectors and the
measurement record are in [`experiments/version7`](experiments/version7).

## Release

Release 0.4.0 writes stream `CNGPHI08` and collection schema SHDR v0. The
format carries image samples only: no colour profile, metadata, preview layer,
16-bit samples or animation. Files written by releases 0.1 to 0.3 (`CSP` v2
and v3, `CNGPHI07`) are rejected by this release; re-encode them from their
decoded samples.

## License

The implementation and documentation are distributed under the
[MIT license](LICENSE). Dependency terms are listed in
[third-party notices](THIRD_PARTY_NOTICES.md).
