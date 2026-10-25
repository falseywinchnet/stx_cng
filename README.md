# CNG

### Computational Network Graphics

CNG is a lossless image format for RGB8 and RGBA8 images and image sets, with a
portable C++20 reference implementation, **stx_cng**. Every sample is
reconstructed exactly, including the colour of fully transparent pixels. Files
are smaller than PNG, encode faster than PNG at any level, decode in about half
the time of PNG, and carry a checksum on every chunk.

[Format](docs/FORMAT.md) · [Collections](docs/COLLECTIONS.md) · [API](docs/API.md) ·
[Build](docs/BUILDING.md) · [Measurements](docs/MEASUREMENTS.md) · [MIT license](LICENSE)

## Features

| | |
| --- | --- |
| Samples | RGB8 and RGBA8, interleaved, reconstructed bit for bit |
| Alpha | Preserved as stored; the RGB beneath alpha 0 survives |
| Dimensions | Up to 65,536 on a side and 4,194,304 pixels per image |
| Coding | Exact pixel matches, exact gradient runs, reversible residual coding |
| Entropy coder | cngcompress: bounded matching with compact Huffman tables, no external compression library |
| Framing | Typed chunks with explicit lengths and a CRC32 each |
| Memory | Encoder and stream decoder work in 1 MiB bands; neither holds the whole image |
| Collections | A packed set of standalone images, or one composite canvas sliced into its members |
| Decoding | Into caller-owned storage, or streamed |
| Implementation | C++20, portable; no platform intrinsics |
| Dependencies | Bundled cngcompress (MIT); zlib for CRC32 |
| License | MIT |

## Size and speed against PNG

Complete files, all framing counted, against libpng at compression level 9 with
filtering, over a battery of 222 images.

| Image set | CNG / PNG size | Smaller than PNG | CNG / PNG decode time |
| --- | ---: | ---: | ---: |
| General images (131) | 0.88 | 130 of 131 | 0.55 |
| RGBA images (80) | 0.76 | 80 of 80 | 0.54 |
| Composite sets (11) | 0.74 | 9 of 11 | 0.24 |

Encoding is faster than libpng at compression levels 1, 6 and 9 on every image
of the battery. Encoder working memory is at most 10.5 MB for any admitted
image; the stream decoder's is at most 8.7 MB. Per-image byte counts, timings
and the measurement protocol are in [Measurements](docs/MEASUREMENTS.md).

## How an image is coded

An image is processed in full-width bands within a 1 MiB source budget. Within a
band, exact copies of earlier pixels and exact gradient-prediction runs are
written as instructions and reproduced directly by the decoder. The remaining
pixels become modular gradient residuals, pass through a reversible
mean/opponent colour transform, are packed into four-bit planes and coded by
cngcompress. A band whose literals would not compress stores them as they are.
Every image follows the same construction; there are no image-specific models
and no transmitted tables.

A standalone file is a signature, a 16-byte header, one chunk per band and a
terminator. Each chunk carries its length, type and CRC32. The decoder validates
framing before it produces pixels and rejects unknown versions, flags, chunks and
trailing bytes.

## Collections

A set of 1–4,096 images is stored in one of two forms, chosen by the caller:

| | Packed | Composite |
| --- | --- | --- |
| Payload | One standalone file per member | One standalone file for a canvas holding every member |
| Coding across members | None | Within the canvas's band history |
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

## Release

Release 0.2.0-rc1 writes standalone schema CSP v3 and collection schema SHDR v0.
Version 2 files are read by the prior release and re-encoded for v3. The format
carries image samples only: no colour profile, metadata, preview layer, 16-bit
samples or animation.

## In development: phase coding

The next coding method, phase-aware causal coding, predicts the rounding of
smooth content from a sub-integer phase estimate instead of coding it as noise.
On the research corpus it produces files of 0.74 the size of PNG level 9 and
0.85 the size of the current CNG release, and encodes in about the time of PNG
level 9. Its reference implementations are in
[`experiments/version6`](experiments/version6). The next step is a frame
structure of independently decodable, checksummed 128×128 blocks, measured at no
size cost, so that frames decode in parallel and damage to a file is confined to
one frame.

## License

The implementation and documentation are distributed under the
[MIT license](LICENSE). Dependency terms are listed in
[third-party notices](THIRD_PARTY_NOTICES.md).
