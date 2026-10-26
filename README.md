# CNG

### Computational Network Graphics

CNG is a lossless image format for 8-bit colour images with or without alpha, stored in blue, green, red, alpha
(BGRA) order. A CNG file is cut into independent segments of 128 × 32
pixels that decode in any order and in parallel, carries a mosaic of the image that displays before any segment,
and stores everything a reader needs to find the segments twice, so damage costs only the pixels it touches.
On photographs and screen content CNG files are smaller than the best PNG.

**stx_cng** is the reference implementation: a portable C++20 library that builds with nothing but CMake and a
C++ compiler.

[Specification](docs/FORMAT.md) · [Library](docs/API.md) · [Collections](docs/COLLECTIONS.md) ·
[Building](docs/BUILDING.md) · [Measurements](docs/MEASUREMENTS.md) · [MIT license](LICENSE)

## The format

| | |
| --- | --- |
| Samples | BGR8 and BGRA8, every sample reconstructed exactly, including the colour beneath alpha 0 |
| Dimensions | Any width and height, up to 2³¹ samples |
| Segments | 128 × 32 pixels, each coded on its own and checked by its own CRC32 |
| Parallelism | Segments decode in any order on any number of threads; the encoder is parallel too |
| Progressive | A mosaic of 16 × 16-cell mean colours, a small part of the file, decodes before any segment |
| Integrity | Header, directory, tables, mosaic and every segment checksummed; header, directory, tables and mosaic stored twice (the tables once in a single-segment image) |
| Damage | A damaged segment is reported, or filled from the mosaic while every other segment decodes exactly |
| Collections | Sets of images as packed standalone files or as one composite canvas |
| Status | Format version 1, frozen: every later release of the library reads it |

## Size against optimized PNG

Complete files, all framing and both index copies counted, against OptiPNG at its maximum optimization level
(`-o7`, metadata stripped), over 236 images.

| Image set | CNG / OptiPNG size | Smaller than OptiPNG |
| --- | ---: | ---: |
| Photographs (24) | 0.78 | 24 of 24 |
| Screen captures (14) | 0.76 | 13 of 14 |
| Smooth graphics (10) | 0.97 | 6 of 10 |
| Mixed photographic and graphic content (13) | 0.90 | 6 of 13 |
| Noise (23) | 1.06 | 1 of 23 |
| 128 px emoji (96) | 1.22 | 5 of 96 |
| Small periodic tile patterns (56) | 2.08 | 0 of 56 |
| All (236) | 0.82 | 55 of 236 |

CNG is smaller wherever an image has photographic or rendered content. It is larger on small palette-like images
such as emoji, where the per-image tables and the stored-twice index are a large part of a few-kilobyte file, and
on tiny images made of an exactly repeating tile, where PNG's byte matching across the whole image beats matching
within 128 × 32 segments. Per-image sizes are in [Measurements](docs/MEASUREMENTS.md).

## Speed

Median times over the 236 images on a Mac mini with an Apple M4 (macOS 26.5), relative to PNG level 9 (libpng
decode; libpng and zlib encode).

| | CNG / PNG level 9 |
| --- | ---: |
| Decode, one thread | 3.2 |
| Decode, four threads | 1.3 |
| Encode, one thread | 6.0 |
| Encode, four threads | 2.1 |

Segments are independent, so decoding divides across threads. The encoder plans every segment with a cost-aware
parse and takes longer than PNG level 9; it also divides across threads and writes the same file for every count.

## How an image is coded

Each segment is coded as an image of its own. Every pixel is predicted from its left, upper and upper-left
neighbours by the median edge detector, and the residuals of the green, blue and red planes are coded in contexts
chosen by the local activity of each plane; blue and red are coded relative to green where the activity is high.
A pixel can instead repeat an earlier pixel's residuals, every plane's or one plane's, over a run, or repeat the
colour of the last pixel with the same green value. Every plane has its own rANS stream with static tables shared
by the whole image, so the planes of a pixel decode side by side.

A file is a signature, the header with its CRC32, a directory of segment lengths and CRC32s, the tables, the
mosaic, the segments, and then the mosaic, tables, directory and header again. A reader takes whichever copy of
each checks out, verifies every segment before decoding it, and either reports a damaged segment or, when
salvaging, fills it with the mosaic colours of its cells. The [specification](docs/FORMAT.md) defines every byte.

## The library

```sh
git clone https://github.com/falseywinchnet/stx_cng.git
cd stx_cng
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Link `stx_cng::stx_cng` from CMake and include [`stx_cng/codec.hpp`](include/stx_cng/codec.hpp):

```cpp
stx_cng::Dimensions dimensions{.height = 480, .width = 640, .channels = 4};
std::vector<std::uint8_t> file = stx_cng::encode(samples, dimensions);

stx_cng::Dimensions decoded_dimensions;
std::vector<std::uint8_t> decoded = stx_cng::decode(file, decoded_dimensions, {.threads = 0});
```

Samples are interleaved BGR or BGRA rows without padding, the order CNG stores, so a BGRA pixel buffer is encoded
and decoded in place; the library performs no colour conversion and no premultiplication.
`read_dimensions` inspects a file without decoding it, `decode_into` decodes into caller storage, `decode_mosaic`
returns the mosaic alone, and `decode_salvage` decodes a damaged file as far as it goes and reports which segments
were lost. [Library](docs/API.md) describes every call.

The only third-party code is Zstandard 1.5.7, built from its checked-in, hash-pinned release into the library.
The conformance vectors, the malformed-input sweep and the sanitizer configuration are described in
[Building](docs/BUILDING.md). The exact Python reference of the format is in
[`experiments/tiled`](experiments/tiled).

## License

The implementation and documentation are distributed under the [MIT license](LICENSE). Zstandard's licence and
the other notices are in [third-party notices](THIRD_PARTY_NOTICES.md).
