# CNG

### Computational Network Graphics

**Exact pixels. Bounded workspace. Image collections.**

CNG is a lossless image format and a portable C++20 library. It preserves RGB8 and
RGBA8 samples exactly, including the colour beneath transparent pixels. Repeated
pixels and predictable structure become compact instructions; the remaining
samples pass through a reversible colour transform and entropy coding.

The reference implementation is **stx_cng**. This first public release,
**0.1.0-rc1**, provides standalone images and two collection forms: complete images
packed together, or one composite image decoded in full and sliced into its set.

[Format](docs/FORMAT.md) · [Build](docs/BUILDING.md) · [API](docs/API.md) ·
[Collections](docs/COLLECTIONS.md) · [Measurements](docs/MEASUREMENTS.md) ·
[MIT license](LICENSE)

## The construction

CNG processes full-width bands within a 1 MiB source-byte budget. Within each
band, the encoder looks for exact pixel matches and exact gradient prediction
runs. The decoder reconstructs those runs directly. Unmatched pixels become
modular gradient residuals, transformed into reversible mean/opponent colour
coordinates, packed into four-bit planes, and compressed with Zstandard.

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
| Entropy backend | Zstandard 1.5.7, level 3 for encoding |
| Framing | Typed chunks with lengths and CRC32 |
| Encoder storage | Band workspace; no complete image retained internally |
| Collections | Packed standalone images or one fully decoded composite |
| Implementation | C++20; no platform-specific SIMD intrinsics |
| Dependencies | Zstandard 1.5.7 and zlib for CRC32 |
| License | MIT |

## Build

Provide matching Zstandard **1.5.7** headers and library, plus zlib. CMake does not
fetch dependencies.

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

The consolidated library reproduced **222 frozen image files byte for byte** and
reconstructed every source sample. Its standalone tests cover 40 image cases
through both decoding interfaces and verify both collection forms. The published
[evidence summary](evidence/verification.json) identifies the checked sources and
separates local test coverage from the larger research battery.

The current encoder's enumerated owned-array capacity is at most **14,000,967
bytes**, including a 4 MiB entropy-context ceiling. This is an internal capacity
bound, not a bound on the process or its caller. Measured stream-encoder peak RSS
was approximately 9 MB on the release verification corpus. See
[measurements and methodology](docs/MEASUREMENTS.md) for the domain, exclusions,
comparison results and remaining limitations.

## Release scope

This is an integration candidate. Its existing standalone CSP v2 signature and
SHDR v0 collection schema remain unchanged; the public name does not rewrite
previously encoded files. The format and C++ ABI are not yet frozen.

The current canvas limit is 4,194,304 pixels **after dimension rounding to
multiples of 128**. Samples themselves are not padded in the file. The library
does not yet provide a progressive preview, colour-profile or metadata storage,
16-bit samples, animation, or arbitrary rectangle packing. The reference decoder
has not completed an automated memory integrity review for this release.

The implementation and documentation are distributed under the [MIT license](LICENSE).
Dependency terms are listed in [third-party notices](THIRD_PARTY_NOTICES.md).
