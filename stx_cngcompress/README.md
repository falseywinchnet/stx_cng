# stx_cngcompress

**cngcompress** is a lossless byte compressor with bounded reusable storage,
independent frames and a C interface. Its implementation is portable C++20.
The library has no compression-library dependencies.

It combines an eight-byte LZ match finder with compact, length-limited Huffman
coding. The decoder processes four independent Huffman streams together where
useful, and builds a smaller lookup table for short streams. Ordinary word
operations expose parallel work to the compiler; there are no platform-specific
SIMD implementations or wider-register requirements.

The construction was developed for Computational Network Graphics. Complete CNG
measurements selected it over the project's Zstandard-based implementation; those
image results are not a claim that cngcompress beats Zstandard on arbitrary data.
The library retains a single deterministic construction rather than compression
levels or model-selection options.

## Frames and ownership

A frame contains up to **2,097,160 input bytes**. It reconstructs exactly those
bytes and can reference previous output anywhere in the same frame. A stream can
contain a sequence of independently framed pieces. Frame lengths belong to the
surrounding container: this API does not resume a partially consumed input or
output buffer and does not infer concatenated frame boundaries.

Create an encoder and decoder, reuse them, then destroy them. Their declared
storage is **656,400 bytes** and **131,088 bytes** respectively on the measured
64-bit build, excluding allocator metadata and bounded call-stack storage.
Queries report the actual build's sizes. Successful operations allocate no
additional storage. Distinct handles can run concurrently; each handle has one
active caller at a time. Input, output and handle storage must be disjoint.

[`stx_cngcompress.h`](include/stx_cngcompress.h) defines the complete public
interface and failure contract. Operations return a status and a size. A capacity
failure reports the required capacity without touching output. A later invalid
packet can leave partial output, which must be discarded. The handle remains
usable. C++ exceptions do not cross the C interface.

The frame carries no checksum; a container can provide integrity framing. The
header-size query checks the header only. See [the wire specification](docs/FORMAT.md)
for the exact distinction between framing, validation and content integrity.

## Build and use

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

A CMake consumer uses the exported target:

```cmake
project(example LANGUAGES C CXX)
find_package(stx_cngcompress 0.1 CONFIG REQUIRED)
add_executable(example main.c)
set_target_properties(example PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(example PRIVATE stx_cngcompress::stx_cngcompress)
```

The header is C99-compatible. Link with the C++ runtime because the archive is
implemented in C++20. The [installed C consumer](tests/installed/main.c) shows
creation, size queries, compression, decompression, status checks and cleanup.
CMake 3.21 or later builds the package; no dependency download is performed.
An enclosing CMake project can instead call `add_subdirectory` and link the same
target. Tests default on for a top-level package build and off when embedded.

## Format and provenance

This is library version **0.1.0**, implementing **cngcompress frame v1**. The frame
bytes preserve the selected CNG experimental entropy grammar. The standalone
image/container format is separate and belongs to the CNG library.

The matcher, compact table, exact length construction and grouped decoder are
first-party implementations developed in `falseywinchnet/stx_cng`. PivCo-style
reconstruction, prefix integers and separated controls were measured and not
selected. No zxc implementation is linked or copied into this package. The
package uses the MIT license; see [LICENSE](LICENSE).
