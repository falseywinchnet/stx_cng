# Changelog

## 0.4.0 — 2026-10-25

Stream `CNGPHI08`: the frame codec of 0.3.0 with a mosaic layer.

- A mosaic layer, the mean colour of every 16×16 cell coded as a small image by
  the same engine, is stored twice: after the front directory and before the
  mirrored one. It is the first picture of a progressive decode (3–5% of the
  file) and the fill for frames lost to damage. It costs 1.5% of the corpus
  bytes (1.1% on photographs, 7% on 128 px emoji).
- `decode_mosaic` returns the layer alone; `decode_salvage` decodes every
  intact frame, fills the others from the mosaic and reports per frame.
- Header fields carry the mosaic length and CRC32; the header is 30 bytes and
  its trailing copy 22. `CNGPHI07` files of 0.3.0 are rejected at the header.
- Complete files are 0.79 of OptiPNG's maximum level over the 236-image corpus.
- The malformed-input sweep grows to about 79,000 inputs, including mosaic
  length claims and salvage decodes, and passes under ASan/UBSan.


## 0.3.0 — 2026-10-25

The standalone codec is now the phase-aware frame codec, stream `CNGPHI07`.

- Images are coded in independent 128×128 frames with a CRC32 each; frames
  decode in any order and in parallel, and a damaged frame is reported by index
  while the others decode.
- The header fields and the frame directory carry CRC32s and are stored twice
  (front and trailing copy); damage confined to one copy is repaired from the
  other. The index costs `8 × frames + 22` bytes per file.
- Per pixel: median-edge prediction, a sub-integer phase context from an
  extrapolated cleaned plane, activity-gated direct or reversible
  opponent-colour coding, a bounded repetition predictor and adaptive binary
  arithmetic coding. One fixed construction; no modes, tables or parameters.
- Complete files are 0.78 of OptiPNG's maximum level over the 236-image corpus
  (0.75 on photographs, 0.68 on screen captures, 0.57 on smooth graphics;
  larger on noise, 128 px emoji and small periodic tiles).
- Encoding is at or below PNG level 9 in time; one-thread decoding is 8 to 11
  times libpng's and divides by the frames decoded concurrently.
- The standalone codec no longer depends on `stx_cngcompress`; the library
  links zlib for collection checksums only.
- `Measurements` now reports `workspace_bytes`, `frames` and `codec_seconds`.
- New `stx_cng_malformed` test: about 70,000 damaged inputs through the public
  interfaces, run with the address and undefined-behaviour sanitizers
  (`-DSTX_CNG_SANITIZE=ON`).
- Collections embed `CNGPHI07` streams; SHDR v0 framing is unchanged.
- `CSP` v2 and v3 files of releases 0.1 and 0.2 are rejected at the header;
  migrate by decoding with the writing release and re-encoding the samples.


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
