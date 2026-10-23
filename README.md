# CNG

### Computational Network Graphics

**Exact pixels. Bounded workspace. Image collections.**

CNG is a lossless image format and a portable C++20 library. It preserves RGB8 and
RGBA8 samples exactly, including the colour beneath transparent pixels. Repeated
pixels and predictable structure become compact instructions; the remaining
samples pass through a reversible colour transform and entropy coding.

The reference implementation is **stx_cng**. Release candidate **0.2.0-rc1**
provides standalone images and two collection forms: complete images packed
together, or one composite image decoded in full and sliced into its set.

[Format](docs/FORMAT.md) · [Build](docs/BUILDING.md) · [API](docs/API.md) ·
[Collections](docs/COLLECTIONS.md) · [Measurements](docs/MEASUREMENTS.md) ·
[MIT license](LICENSE)

## The construction

CNG processes full-width bands within a 1 MiB source-byte budget. Within each
band, the encoder looks for exact pixel matches and exact gradient prediction
runs. The decoder reconstructs those runs directly. Unmatched pixels become
modular gradient residuals, transformed into reversible mean/opponent colour
coordinates, packed into four-bit planes, and compressed with the in-house
**cngcompress** algorithm.

When compressed literal data would cost more than its original samples, the band
stores those samples directly. This is a fixed part of the format, not a family
of image-specific models. Every image follows the same construction.

Chunks carry explicit lengths, four-byte types and CRC32 checksums. The library
keeps ownership and storage visible: streams for bounded processing, a supplied
buffer for direct decoding, and owned planes for decoded collections.

## At a glance

| Property | Current implementation |
| --- | --- |
| Samples | RGB8 or RGBA8, exact reconstruction |
| Alpha | Preserved exactly; hidden RGB is preserved |
| Coding | Pixel matches, gradient runs, reversible residual coding |
| Entropy backend | stx_cngcompress: bounded matching and compact Huffman coding |
| Framing | Typed chunks with lengths and CRC32 |
| Encoder storage | Band workspace; no complete image retained internally |
| Collections | Packed standalone images or one fully decoded composite |
| Implementation | C++20; no platform-specific SIMD intrinsics |
| Dependencies | Bundled MIT stx_cngcompress; external zlib for CRC32 |
| License | MIT |

## Build

Provide zlib and C/C++20 compilers. The independent stx_cngcompress package is
included in the source tree at a [recorded commit](docs/CNGCOMPRESS_SOURCE.json).
It is also available as [its own library](https://github.com/falseywinchnet/stx_cngcompress).
CMake does not fetch dependencies.

```sh
git clone https://github.com/falseywinchnet/stx_cng.git
cd stx_cng
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

For source integration, add this directory to your CMake project and link
`stx_cng::stx_cng`. The public headers are
[`stx_cng/codec.hpp`](include/stx_cng/codec.hpp) and
[`stx_cng/collection.hpp`](include/stx_cng/collection.hpp).
[Build and installation details](docs/BUILDING.md) include the measured non-Apple
LLVM toolchain and the dependency version contract.

## Evidence

The integrated library reconstructs all **222 frozen image bodies** exactly through
both decoding interfaces. Its entropy bytes match the selected experimental
encoder; only the public format header and its checksum change. The ordinary
suite also covers 40 generated image cases and both collection forms, including
explicit rejection of older standalone versions before pixel output.

Complete-image measurements retain the prior decoder improvement over the
Zstandard release, with slightly smaller aggregate files. Individual regressions
remain, including some smooth images and small packed glyph collections. See
[measurements and methodology](docs/MEASUREMENTS.md) for per-group results,
workspace accounting and the precise comparison scope.

The current encoder's enumerated owned capacity is at most **10,450,883 bytes**
on the measured 64-bit ABI. This includes its compressor handle; it excludes
caller storage, stack, allocator metadata and process mappings. Both the CNG
package and the standalone compressor pass independent relocated-install consumer
checks. The compressor itself has no external compression-library dependency.

## Release scope

This is an integration candidate. Its standalone header is **CSP v3** and its
collection schema remains SHDR v0. The eight-byte signatures are unchanged.
Version 2 files require decoding with the prior release and re-encoding; changing
a header is insufficient. The [compatibility contract](docs/FORMAT.md#4-compatibility-boundary)
describes standalone and collection migration. The format and C++ ABI are not yet
frozen.

The current canvas limit is 4,194,304 pixels **after dimension rounding to
multiples of 128**. Samples themselves are not padded in the file. The library
does not yet provide a progressive preview, colour-profile or metadata storage,
16-bit samples, animation, or arbitrary rectangle packing. The reference decoder
has not completed an automated memory integrity review for this release.

The implementation and documentation are distributed under the [MIT license](LICENSE).
Dependency terms are listed in [third-party notices](THIRD_PARTY_NOTICES.md).
