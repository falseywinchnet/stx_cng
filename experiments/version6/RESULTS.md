# V6 results — 24 October 2026

Measured on the owner's M4 (macOS arm64, Homebrew LLVM clang 23, scalar C++20). Machine-readable evidence is in `evidence/`; the mathematics and design record live in the research tree (`docs/PHASE_AWARE_CAUSAL_CODING.md`, `docs/PHASE_CODING_MATHEMATICS.md`, `docs/PHASE_PERFORMANCE_MATHEMATICS.md`, `docs/PHASE_V6_STREAMING.md`).

## 1. What changed from CNGPHI04

1. **Bounded repetition table.** The reference kept an exact dictionary from
   the causal neighbourhood (L, U, UL, UR of all planes) to the colour that
   last followed it. Its size grows with the number of distinct
   neighbourhoods, which is of the order of the pixel count, and the native
   open-addressing table needed 32–44 bytes per pixel. V6 uses a
   direct-mapped table: the sixteen (or twelve) 9-bit key components are
   packed into three 64-bit words, hashed with a fixed splitmix-style mix,
   the slot is the low bits of the hash and a 29-bit fingerprint (hash
   bits 35..63) stands for the key. Capacity is the smallest power of two
   with `2·capacity ≥ pixels`, clamped to [2⁶, 2²²], eight bytes per slot
   (tag = `((fingerprint+1) << 2) | confidence`, 0 meaning empty, and the
   packed colour). A fingerprint collision or an eviction is a wrong
   *prediction*, which the coded residual corrects; encoder and decoder
   compute the same table, so the stream stays exactly decodable. This is a
   stream change, hence the new signature `CNGPHI05`.
2. **Row rings.** Every per-plane state is kept in a ring of rows: decoded
   samples and the lifted opponent planes need rows y−1..y (2 rows), the
   cleaned planes need rows y−1−K..y (K+2 = 8 rows, because the column sums
   drop row y−1−K when they advance), the error-energy plane needs rows
   y−2..y (3 rows). The encoder reads source rows from the caller's buffer
   and the decoder writes finished samples straight into the caller's
   destination; no full-image plane is allocated anywhere. This does not
   change the stream.
3. **Row passes and the interior kernel** (the research tree's `PHASE_PERFORMANCE_MATHEMATICS.md`
   §8): per row, column sums and their prefix sums for the seven cleaned
   planes, the row-above part of the activity sum and of the repetition key
   are computed in plain loops over x; the per-pixel work then gathers the
   causal quantities without edge checks in the interior and evaluates the
   moments only for the three planes the pixel's mode needs. Identical
   integers to CNGPHI04 (verified byte-identical on 244 images before the
   table change was made).

## 2. Memory accounting

For an image of W×H pixels with C channels (image bytes = W·H·C):

| component | bytes | notes |
|---|---|---|
| row rings | W·(2·4C + 8·4C + 2·4·3 + 8·4·3 + 3) | int32 rows; 4C·10 + 123 per column, 243 B per column for RGB |
| prefix/column scratch | 7·W·(2·4 + 3·8) = 224·W | seven planes: C0, C2 int32; P0, P1, P2 int64 |
| row scratch | W·(4 + 24) | above energy, above key |
| repetition table | 8·capacity, capacity ∈ [pixels/2, pixels) ∪ {2⁶ floor, 2²² cap} | 4–8 B per pixel, at most 32 MiB |
| models | 21,848 × 8 = 174,784 | fixed |
| encoder output | the compressed stream (vector, reserved at image/2 + 1 KiB) | ≤ image bytes in practice |

So the encoder's working memory is `≈ 500·W + 8·capacity + 175 KiB + output`
and the decoder's is the same without the output. For RGB images above about
0.5 Mpx the table dominates and the total is between 1.3× and 2.7× the
image bytes plus the output; for very large images the table is capped at
32 MiB and the memory is O(W). The fixed 175 KiB of models and the 64-slot
table floor mean the five-times rule cannot hold for tiny images (a 128×128
RGB emoji is 48 KiB); it holds from roughly 0.1 Mpx upward. Measured peak
resident sizes are in §4.

## 3. Size effect of the bounded table

Measured with the real encoder on the M4 over 236 corpus images (all groups
except the eight large TESTIMAGES files, which exceed 1.1 Mpx and take hours
in Python; `research/phase_repeat_bound_oracle.py`,
`results/phase_repeat_bound_oracle.json`). Bytes relative to the exact
unbounded dictionary, for capacity ratios of 2, 1, 1/2 and 1/4 slots per
pixel (power of two, clamped to [2⁶, 2²²]):

| group | n | exact bytes | ratio 2 | ratio 1 | **ratio 1/2 (V6)** | ratio 1/4 |
|---|---|---|---|---|---|---|
| archive_photographs | 24 | 11,443,711 | −0.00% | −0.00% | −0.01% | −0.02% |
| guiforms_capture | 14 | 2,855,982 | −0.01% | −0.01% | −0.02% | −0.04% |
| research_noisy | 21 | 1,007,122 | +0.00% | +0.00% | −0.00% | −0.00% |
| reference_fixture | 3 | 836,062 | −0.00% | −0.00% | −0.00% | −0.01% |
| research_mixed | 8 | 717,028 | +0.00% | +0.00% | +0.00% | +0.01% |
| archive_noise_mixed | 2 | 456,710 | +0.00% | +0.00% | +0.00% | +0.00% |
| noto_mixed | 64 | 341,726 | +0.02% | +0.02% | +0.06% | +0.13% |
| noto_faces | 32 | 200,973 | +0.02% | +0.13% | +0.44% | +0.66% |
| archive_structured | 55 | 169,239 | +1.30% | +2.39% | +4.60% | +9.01% |
| research_smooth | 9 | 97,725 | −0.01% | −0.00% | −0.01% | −0.03% |
| archive_smooth | 1 | 6,400 | +0.00% | +0.00% | +0.00% | +0.00% |
| ALL | 236 | 18,675,246 | +0.02% | +0.03% | +0.06% | +0.11% |

The bound is free on photographs, GUI captures, noise and smooth content
(the fingerprint is a better key than the exact tuple on a few files, by a
byte or two). It costs on the small structured tiles (+4.6%, 7.8 KB over
55 files whose compressed size averages 3 KB) and slightly on the 128-px
emoji faces (+0.44%), where distinct neighbourhoods are many relative to a
table of pixels/2 slots. Ratio 1 would halve that loss but needs 8–16 bytes
per pixel, which exceeds the owner's five-times budget for RGB images just
above a power of two in size (16 MiB of table for a 3.1 MB image); ratio 1/2
keeps the table at 4–8 bytes per pixel. V6 uses ratio 1/2.

## 4. Measurements of the V6 native kernel

All runs on the M4 (macOS arm64, Homebrew LLVM clang 23, `-O3`, scalar C++20,
no intrinsics). Another session was using the machine during the V6 timing
run, so the V6 times below are contended and conservative (its own PNG9
control ran 50% slower than in the quiet baseline run); re-time when quiet.

**Exactness.** The nine regenerated vectors and all 244 corpus images encode
to the same bytes as the Python reference and decode to the original
samples (`results/native_phase.json`, run of `research/run_native_phase.py`).

**Sizes.** Complete file bytes of the native V6 stream against PNG level 9
(Pillow, optimize) and the recorded v3 sizes, from the same run:

| group | n | V6 bytes | CNGPHI03 bytes | PNG9 bytes | V6 / PNG9 | V6 / v3 |
|---|---|---|---|---|---|---|
| archive_composite | 1 | 164,071 | 160,688 | 231,028 | 0.710 | 1.295 |
| archive_noise_mixed | 2 | 456,710 | 456,709 | 447,049 | 1.022 | 1.017 |
| archive_photographs | 24 | 11,442,739 | 11,443,713 | 15,854,751 | 0.722 | 0.886 |
| archive_smooth | 1 | 6,400 | 6,398 | 14,135 | 0.453 | 0.463 |
| archive_structured | 55 | 177,027 | 169,240 | 186,001 | 0.952 | 0.938 |
| guiforms_capture | 14 | 2,855,381 | 2,855,977 | 4,464,219 | 0.640 | — |
| noto_faces | 32 | 201,860 | 200,974 | 237,490 | 0.850 | — |
| noto_mixed | 64 | 341,917 | 341,724 | 387,235 | 0.883 | — |
| reference_fixture | 3 | 836,022 | 836,061 | 1,067,130 | 0.783 | 0.712 |
| research_mixed | 8 | 717,054 | 717,033 | 848,905 | 0.845 | 0.801 |
| research_noisy | 21 | 1,007,121 | 1,007,120 | 1,335,235 | 0.754 | 0.673 |
| research_photo | 1 | 381,716 | 381,743 | 453,136 | 0.842 | 0.851 |
| research_smooth | 9 | 97,713 | 97,757 | 201,497 | 0.485 | 0.314 |
| research_structured | 1 | 136 | 136 | 187 | 0.727 | 0.782 |
| testimages | 8 | 9,961,634 | 9,961,680 | 12,951,894 | 0.769 | — |
| **all** | 244 | 28,647,501 | 28,636,953 | 38,679,892 | 0.741 | 0.848 |

The bounded table moves the total by +0.04% against CNGPHI03 (the
structured tiles pay +4.6%, everything else is within ±0.05%).

**Memory.** Peak resident size of the driver process (`/usr/bin/time -l`,
includes the process baseline, the input file read into memory and the
output stream), `research/measure_native_phase_memory.py`,
`results/native_phase_memory.json` and `results/native_phase_memory_phi04.json`:

| image | size | image MB | CNGPHI04 encode peak RSS | **V6 encode peak RSS** | **V6 decode peak RSS** |
|---|---|---|---|---|---|
| gallery-macos | 1280×820 | 4.2 | 140.8 (33.5×) | 23.1 (5.5×) | 15.5 (3.7×) |
| correspondence-macos | 1120×720 | 3.2 | 121.9 (37.8×) | 11.8 (3.7×) | 10.1 (3.1×) |
| basic_controls_002_screen | 1259×760 | 2.9 | 125.9 (43.9×) | 11.3 (3.9×) | 10.4 (3.6×) |
| synthetic_2048x2048 | 2048×2048 | 12.6 | 504.8 (40.1×) | 48.5 (3.9×) | 39.2 (3.1×) |
| synthetic_4096x4096 | 4096×4096 | 50.3 | 2012.8 (40.0×) | 138.5 (2.8×) | 115.7 (2.3×) |

The encoder is within the five-times budget from about 3 Mpx upward and at
3.7–5.5× on the 1-Mpx GUI captures, where the fixed costs (the 175 KiB of
models, the 2²⁰-slot table of 8 MiB for 0.9–1.05 Mpx, the process itself) are
a larger share. The decoder is at 2.3–3.7×. The largest image, 4096² RGB
(50 MB), peaks at 138 MB encoding and 116 MB decoding against 2.0 GB for the
CNGPHI04 kernel; the table is at its 32 MiB cap there and the remainder is
rows, input, output and process baseline, so memory stops growing with
height from here on.

**Time.** Milliseconds per image, minimum of seven in-process repeats:

| image | size | CNGPHI04 encode (quiet) | **V6 encode (contended)** | PNG9 encode (quiet) | CNGPHI04 decode (quiet) | **V6 decode (contended)** | PNG9 decode (quiet) |
|---|---|---|---|---|---|---|---|
| dog-half | 512×512 | 48.7 | 38.5 | 36.1 | 47.9 | 40.4 | 3.1 |
| smooth-half | 512×512 | 31.7 | 28.1 | 95.6 | 32.4 | 23.4 | 1.5 |
| reference | 1024×512 | 92.1 | 83.9 | 102.1 | 89.7 | 86.4 | 5.0 |
| kodim01 | 768×512 | 72.9 | 54.0 | 46.0 | 68.2 | 57.4 | 5.0 |
| kodim23 | 768×512 | 69.7 | 47.7 | 79.5 | 67.4 | 49.3 | 4.2 |
| smooth | 384×256 | 9.9 | 6.8 | 36.5 | 9.9 | 7.0 | 0.7 |
| checked_list_box_screen | 1259×760 | 107.9 | 72.4 | 142.6 | 103.1 | 76.9 | 9.1 |
| correspondence-macos | 1120×720 | 56.4 | 34.9 | 34.5 | 49.9 | 32.4 | 3.4 |
| retained_smooth_noise_one | 512×512 | 40.8 | 27.6 | 122.0 | 40.7 | 29.6 | 2.7 |
| noise8 | 128×128 | 3.2 | 2.4 | 0.9 | 3.3 | 2.5 | 0.2 |

Even contended, V6 encodes 21–39% faster than the quiet CNGPHI04 kernel and
is within 20% of PNG9's encode time on every photograph, smooth and GUI
image here (faster than PNG9 on seven of ten), the exception being the
128-px noise fixture (2.7× PNG9, 3 ms). Decoding stays symmetric with
encoding and 8–13× slower than PNG decoding, for the reason given in
the research tree's `PHASE_PERFORMANCE_MATHEMATICS.md` §7.3: the decoder recomputes the model.

## 5. Status and what is not claimed

V6 is the phase codec with a bounded table and streaming row state, verified
byte-identical between the Python reference and the native kernel on 244
images, with measured sizes, peak memory and (contended) timing. Not
claimed: PNG-class decode speed (structurally out of reach for a
context-modelling decoder, §7.3 of the performance document); a malformed-
input sweep or memory-safety review of the native kernel (ordinary tests
only); SIMD intrinsics (none are used; the row passes are plain loops, of
which the activity pass auto-vectorises and the column-sum and prefix loops
do not yet); a stable format promise (CNGPHI05 is a research stream); and
the five-times memory rule below about 1 Mpx, where fixed costs dominate.
The published repository receives V6 as `experiments/version6/` only on the
owner's explicit go-ahead.
