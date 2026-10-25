# Measurements and verification

Three separate questions are answered separately: whether every sample is
preserved, what the files cost in bytes against optimized PNG, and what the
codec costs in time and memory. Machine-readable records are in
[`experiments/version7/evidence`](../experiments/version7/evidence).

## Exactness

| Check | Result | Record |
| --- | --- | --- |
| C++ kernel against the Python reference, bit-exact vectors | 10 of 10 streams byte-identical, samples exact | `vectors/`, `native_phase_v7e_vectors.json` |
| C++ kernel against the Python reference, complete streams with mosaic | 12 smallest corpus images byte-identical | `native_phase_v7e_vectors.json` |
| C++ kernel against the Python reference, corpus frames | 80 smallest corpus images byte-identical (frame payloads, previous container) | `native_phase_v7_frames_pod_python.json` |
| Round trip of the 236-image corpus, arm64 | 236 of 236 exact | `native_phase_hashes_arm64_v7e.json` |
| Round trip of the 236-image corpus, x86-64 (GCC 13.3, Linux) | 236 of 236 exact; every stream's SHA-256 identical to the arm64 stream | `native_phase_hashes_x86_v7e.json` |
| Bundled round-trip test | 40 generated cases, both decode interfaces, both collection forms | `tests/roundtrip.cpp` |
| Malformed-input sweep under ASan/UBSan | 78,940 damaged inputs: 9,650 accepted, every unforged acceptance exact, salvage never throws on payload damage, no sanitizer report | `tests/malformed.cpp`, `malformed_sweep.txt` |

The corpus is 140 research images (24 Kodak photographs, 14 screen captures,
smooth, noisy, mixed and structured fixtures, 3 reference images) and 96 Noto
emoji at 128 px (32 faces and 64 hash-selected mixed glyphs). Third-party images
are identified by hash in the records and are not redistributed.

## Size against optimized PNG

Every corpus image was optimized with OptiPNG 7.9.1 at `-o7 -strip all` (its
maximum level: every zlib level, strategy and filter, metadata removed). CNG
sizes are complete files including the duplicated index. Ratios are summed
bytes; lower is better.

| Set | n | CNG / OptiPNG | Smaller | CNG / PNG level 9 |
| --- | ---: | ---: | ---: | ---: |
| Photographs (Kodak) | 24 | 0.762 | 24 | 0.733 |
| Screen captures | 14 | 0.699 | 13 | — |
| Smooth graphics | 10 | 0.607 | 9 | 0.529 |
| Mixed photographic and graphic | 13 | 0.838 | 7 | 0.821 |
| Noise | 23 | 1.019 | 6 | 0.830 |
| Emoji 128 px, faces | 32 | 1.094 | 3 | 0.930 |
| Emoji 128 px, mixed | 64 | 1.226 | 8 | 0.980 |
| Small periodic tile patterns | 56 | 1.943 | 1 | 1.570 |
| All | 236 | 0.789 | 71 | 0.764 |

PNG level 9 is Pillow's libpng output at compression level 9 with filtering;
the screen captures have no recorded PNG level 9 size. Per-image bytes are in
`optipng_reference.json`.

The losses are of two kinds. Noise and palette-like emoji sit near 1: there is
little to predict, and PNG's palette and byte matching are as good a model.
The small periodic tile fixtures (16 to 64 pixel tiles repeated across an
image of a few hundred pixels) are PNG's best case and CNG's worst: the
repetition predictor recovers exact repeats pixel by pixel, while PNG copies
whole rows by reference, and in a 128×128 frame the repetition history is
cut at every frame edge. The largest single loss is a 7-pixel tile over a ramp
at 34 times the OptiPNG size (70 KB against 2 KB). These are retained
counterexamples; no per-image mode exists to avoid them.

The duplicated header and directory cost `8 × frames + 30` bytes per file,
about 0.1% of all corpus bytes. The mosaic layer stored twice costs 1.5% of
all corpus bytes: 1.1% on the photographs, 2.2% on screen captures, 7.4–7.6%
on the 128 px emoji (one frame, where the layer is as large as the index) and
6.8% on the small smooth fixtures. Against the previous container the
whole corpus grew 1.52%.

## Time

Complete in-process encode and decode of fixed images, median of seven
alternating trials, paired with libpng level 9 through Pillow in the same
process on an Apple M4 (one thread, LLVM 23.1.2, idle machine). Times are
milliseconds for the whole image.

| Image | Size | CNG encode | CNG decode | PNG 9 encode | PNG 9 decode |
| --- | --- | ---: | ---: | ---: | ---: |
| Kodak 01 | 768×512 RGB | 44 | 43 | 45 | 5.0 |
| Kodak 23 | 768×512 RGB | 42 | 41 | 78 | 4.2 |
| Photograph (dog) | 512×512 RGB | 31 | 31 | 35 | 3.1 |
| Screen capture | 1259×760 RGB | 69 | 69 | 139 | 8.7 |
| Mail client capture | 1120×720 RGBA | 32 | 29 | 34 | 3.2 |
| Smooth gradient | 384×256 RGB | 5.8 | 6.0 | 36 | 0.7 |
| Smooth with noise | 512×512 RGB | 26 | 26 | 120 | 2.7 |
| Noise | 128×128 RGB | 2.1 | 2.1 | 0.9 | 0.2 |

The encoder and decoder perform the same modelling work, so the two times are
equal; the encoder is at or below PNG level 9 on every image but pure noise,
and the one-thread decoder is 8 to 11 times libpng's. OptiPNG at its maximum
level is many times slower than PNG level 9 to encode and decodes as PNG.

Frames are independent, so a decoder running N frames at once divides the
time. Measured with the research driver's threaded frame decode on an x86-64
pod (nanoseconds per pixel, minimum of five):

| Image | Frames | 1 thread | 2 threads | 4 threads |
| --- | ---: | ---: | ---: | ---: |
| Kodak 23, 768×512 | 24 | 159 | 82 | 57 |
| Photograph (dog), 512×512 | 16 | 174 | 89 | 62 |
| Screen capture, 1259×760 | 60 | 99 | 52 | 38 |
| Smooth, 512×512 | 16 | 95 | 55 | 38 |

The library's `decode` is single-threaded; the kernel's `decode_frame` and
`decode_frame_range` decode frames into their rectangles of a shared buffer for
callers that schedule frames themselves. The mosaic layer decodes in a few
hundred microseconds for a megapixel image (1/256 of the pixels).

## Progressive decode and damage

The four-axis tradeoff study that led to the mosaic layer is in
[`experiments/version7/TRADEOFF.md`](../experiments/version7/TRADEOFF.md):
one 16-cell layer stored twice gives a first picture after 3–5% of the file
(median 21 dB against the original on photographs, 19 dB on captures) and fills
the frames lost to burst damage; a second, 4-cell layer would cost 7% of the
file per copy and was not adopted. Under scattered 512-byte bursts amounting to
1% of a large file, about two thirds of the pixels decode exactly and the rest
take their mosaic colour.

## Memory

A call owns the complete compressed stream, the image samples (for `encode`
and the stream decoder) and the frame engine's state for one frame: row rings
and prefix sums at the frame width, a repetition table of at most 2¹⁴ slots
for a 128×128 frame, and 175 KiB of adaptive models, under 400 KiB in all. A
decoder running N frames concurrently owns N frame states. `Measurements`
reports the owned total per call; it is not process RSS.

## Frame size

The frame side is carried in the stream and the decoder honours 16 to 255. The
containment cost against a single-stream encoding of the same construction
halves with each step of frame size (+5.4% at 64, +1.5% at 128, +0.7% at 192,
+0.3% at 255 over the corpus); 128 is the written default. The record is
`native_phase_v7_frame_sides_pod.json`.
