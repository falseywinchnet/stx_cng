# Changelog

## 0.1.0-rc1 — 2026-10-22

First public integration candidate of **CNG — Computational Network Graphics**.

- Lossless RGB8/RGBA8 encoding with exact hidden-colour preservation.
- Fixed-band pixel matching, gradient runs and reversible residual coding.
- Bounded Zstandard encoder context, pinned to version 1.5.7.
- Stream decoding and decoding into caller-owned storage.
- Packed standalone-image collections and full-composite decoding with slicing.
- Portable RGB and byte-word RGBA reconstruction kernels.
- CMake build/install support, ordinary round-trip tests, MIT licensing and format
  documentation.

The standalone CSP v2 and collection SHDR v0 bytes remain compatible with the
accepted research baseline. Literal unpacking remains separate from reconstruction:
full-decoder experiments found that fusion saved scratch capacity but lost speed.

Early preview, metadata/profile storage, wider sample types and arbitrary atlas
packing remain future work. This candidate does not freeze the format or ABI.
