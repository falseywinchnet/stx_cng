# V7 results — 25 October 2026

Measured on the owner's M4 (idle) and on an x86-64 compute pod; machine-readable evidence is in `evidence/`. The design record and the work accounting that chose this structure are in the research tree (`docs/PHASE_WORK_ACCOUNTING.md`, `docs/PHASE_V7_FRAMES.md`).

## 1. Stream

| field | bytes | content |
|---|---|---|
| signature | 8 | `CNGPHI07` |
| width, height | 4 + 4 | big-endian u32 |
| channels | 1 | 3 or 4 |
| frame side | 1 | 128 |
| directory | 8 per frame | payload length u32, CRC32 u32 of the payload |
| directory checksum | 4 | CRC32 of the directory bytes |
| payloads | | one per frame, raster order |

Frames tile the image in raster order with a side of 128; the last column
and row of frames are smaller when the dimensions are not multiples of 128.
A frame's payload is the CNGPHI05 stream of the frame as an image of its
own, without that stream's header: every adaptive model, the range coder
and the bounded repetition table start fresh, and the frame's top and left
edges take the image-edge boundary rules. Nothing in a frame depends on
another frame. CRC32 is the IEEE polynomial as zlib computes it.

The reference is `research/phase_codec_v7.py` (it calls the CNGPHI05
reference per frame); the vectors are `results/phase_vectors_v7/`, nine
single-frame inputs plus a 300×200 crop of six frames with partial last
column and row.

## 2. Native kernel

`native/phase/phase_codec.cpp` is the CNGPHI05 engine with the frame placed
in the caller's image buffer by a row stride, a frame directory with
checksums, `frame_count`, `decode_frame` (one frame into its rectangle of
the caller's buffer, so callers decode frames concurrently into disjoint
rectangles) and `decode` (all frames). Per-frame working memory is the
engine's row rings at the frame width plus a repetition table of at most
2¹⁴ slots and the 175 KiB of models, so a frame's workspace is under 400
KiB and a decoder running N frames at once uses N of them. The driver's
`frames` mode decodes all frames with a given number of threads for the
parallel timing.

## 3. Measurements

**Exactness.** The native kernel matches the Python reference on all ten
vectors and on the 80 smallest corpus images (pod, x86-64,
`results/native_phase_v7_frames_pod_python.json`), round-trips all 236
corpus images exactly on x86-64 (`results/native_phase_v7_frames_pod_x86.json`),
and its streams have identical SHA-256 on x86-64 and on the arm64 Mini for
all 236 (`results/native_phase_hashes_arm64_v7.json`).

**Size and serial time against V6** (pod, scalar, frames decoded serially):

| group | n | V6 bytes | V7 bytes | V7 / V6 | exact | V7 encode ns/px | V7 decode ns/px |
|---|---|---|---|---|---|---|---|
| archive_composite | 1 | 164,071 | 173,906 | 1.0599 | 1/1 | 189 | 179 |
| archive_noise_mixed | 2 | 456,710 | 457,965 | 1.0027 | 2/2 | 235 | 218 |
| archive_photographs | 24 | 11,442,739 | 11,493,489 | 1.0044 | 24/24 | 169 | 159 |
| archive_smooth | 1 | 6,400 | 5,839 | 0.9123 | 1/1 | 91 | 89 |
| archive_structured | 55 | 177,027 | 280,357 | 1.5837 | 55/55 | 43 | 39 |
| guiforms_capture | 14 | 2,855,381 | 2,961,503 | 1.0372 | 14/14 | 87 | 82 |
| noto_faces | 32 | 201,860 | 202,276 | 1.0021 | 32/32 | 97 | 93 |
| noto_mixed | 64 | 341,917 | 342,749 | 1.0024 | 64/64 | 80 | 76 |
| reference_fixture | 3 | 836,022 | 835,190 | 0.9990 | 3/3 | 142 | 134 |
| research_mixed | 8 | 717,054 | 712,204 | 0.9932 | 8/8 | 161 | 152 |
| research_noisy | 21 | 1,007,121 | 1,008,440 | 1.0013 | 21/21 | 193 | 183 |
| research_photo | 1 | 381,716 | 384,567 | 1.0075 | 1/1 | 184 | 174 |
| research_smooth | 9 | 97,713 | 99,904 | 1.0224 | 9/9 | 99 | 96 |
| ALL | 236 | 18,685,867 | 18,958,538 | 1.0146 | 236/236 | 114 | 108 |

**Reading.** With the real model the 128×128 frames cost **+1.5%** over the
one-stream V6, not the −0.5% the design oracle predicted: the real rate
schedule adapts faster than the oracle's KT models, so restarting buys less
and costs more, and the repetition table restarts with the frame. The price
is uneven: photographs +0.4%, emoji +0.2%, smooth fixtures −9% to +2%, GUI
captures +3.7%, and the small structured fixtures +58%, where repetition
that crossed the image is cut at every frame edge. Serial time is unchanged
(114 ns/px encode, 108 decode on the pod, against 110/104 for V6): the
frame structure is free in work and the parallel decode divides it by the
frame count available to the caller.

The frame side is carried in the stream (`encode_with_frame`, the `encode-side`
driver mode; the decoder honours any side from 16 to 255), so the size cost
of larger frames is measured next.

**Frame-parallel decode and instruction counts** (pod, x86-64; `frames`
mode of the driver: all frames decoded by N threads into disjoint rectangles
of one buffer, min of five; cachegrind instructions per pixel of the full
decode, `results/native_phase_v7_frames_timing_pod.json`):

| image | frames | V6 dec ns/px | V7 1 thread | V7 2 threads | V7 4 threads | V6 bytes | V7 bytes | V6 dec Ir/px | V7 dec Ir/px | V6 D1 miss/px | V7 D1 miss/px |
|---|---|---|---|---|---|---|---|---|---|---|---|
| noise8 | 1 | 192 | 197 | 198 | 197 | 29,899 | 29,912 | 2,591 | 2,609 | 9.1 | 9.1 |
| kodim23 | 24 | 144 | 159 | 82 | 57 | 432,136 | 432,733 | 1,862 | 2,119 | 11.2 | 9.4 |
| smooth-half | 16 | 82 | 95 | 55 | 38 | 31,781 | 33,039 | 1,535 | 1,752 | 9.8 | 7.4 |
| dog-half | 16 | 158 | 174 | 89 | 62 | 381,716 | 384,567 | 2,044 | 2,286 | 11.2 | 9.5 |
| checked_list_box_screen | 60 | 88 | 99 | 52 | 38 | 450,828 | 463,584 | 1,364 | 1,545 | 9.4 | 6.7 |

**Reading.** On one thread the frame decoder is 7–16% slower than V6 (the
instruction count is 12–14% higher: each frame pays its setup, its CRC and
a larger share of border pixels, about 9% of a 128×128 frame against 2% of a
512-wide row image). On four threads it decodes the photographs and the GUI
screen in 0.36–0.43 of V6's time, and the 60-frame GUI screen has fifteen
times that parallelism still unused. First-level misses fall (6.7–9.5 against
9.4–11.2 per pixel) because a frame's working set is smaller. The serial
overhead is the next thing to tighten; the parallel gain is structural.

**Frame side** (native encoder, bytes against the one-stream V6,
`results/native_phase_v7_frame_sides_pod.json`; the decoder honours any
side the stream carries):

| group | n | V6 bytes | side 64 | side 128 | side 192 | side 255 |
|---|---|---|---|---|---|---|
| archive_composite | 1 | 164,071 | +6.79% | +5.99% | +5.69% | +5.67% |
| archive_noise_mixed | 2 | 456,710 | +0.79% | +0.27% | -0.01% | -0.03% |
| archive_photographs | 24 | 11,442,739 | +2.25% | +0.44% | +0.10% | -0.11% |
| archive_smooth | 1 | 6,400 | +16.12% | -8.77% | +5.94% | -0.19% |
| archive_structured | 55 | 177,027 | +227.37% | +58.37% | +34.02% | +15.92% |
| guiforms_capture | 14 | 2,855,381 | +8.40% | +3.72% | +2.17% | +1.58% |
| noto_faces | 32 | 201,860 | +2.84% | +0.21% | +0.21% | +0.21% |
| noto_mixed | 64 | 341,917 | +4.98% | +0.24% | +0.24% | +0.24% |
| reference_fixture | 3 | 836,022 | +2.60% | -0.10% | -0.36% | -0.55% |
| research_mixed | 8 | 717,054 | +1.02% | -0.68% | -0.68% | -0.83% |
| research_noisy | 21 | 1,007,121 | +1.19% | +0.13% | +0.07% | +0.05% |
| research_photo | 1 | 381,716 | +2.45% | +0.75% | +0.36% | +0.20% |
| research_smooth | 9 | 97,713 | +12.86% | +2.24% | +0.59% | +1.05% |
| ALL | 236 | 18,685,867 | +5.36% | +1.46% | +0.74% | +0.34% |

**Reading.** The containment price halves with each step of frame size:
+5.4% at 64, +1.5% at 128, +0.7% at 192, +0.3% at 255. Photographs and the
noise and reference groups are at or below V6 from 192 up; GUI captures fall
from +3.7% to +1.6%; the repetitive structured fixtures are the only group
that stays expensive (+16% at 255). Parallelism at side 255 is still 12
frames on a 768×512 photograph and 15 on a 1259×760 screen. The default side
is a one-constant choice (`kFrame`); 128 is the owner's stated preference,
255 is where the bytes stop moving.

## 4. Tightening

Changes to the kernel that leave every byte of the stream unchanged (verified
against the vectors and the stored corpus hashes):

1. **Prefix-sum moments at every pixel.** The per-row column sums hold
   exactly the rows available above y, so the two-prefix-difference form of
   the window moments is valid at every boundary configuration with the
   window clamped to the frame; the 84-tap direct sums and the
   interior/border split for the moments are gone. In a 128×128 frame about
   13% of pixels were border pixels under the old split (six columns on
   each side and six rows at the top); now only the first row and the first
   two columns take the generic gather, for the neighbour and energy bounds.
2. **Row-above key pass without per-element bounds.** U is a straight copy,
   UL and UR are shifts with one outside value at the edge.
3. **One workspace per call.** Frames reuse the rings, prefixes, table and
   models of one workspace, refilled per frame, instead of allocating them
   for each frame.

**Verified.** After the three changes the kernel produces the same bytes
as before on all 236 corpus images (`results/native_phase_hashes_arm64_v7c.json`
against the stored CNGPHI07 hashes) and on the ten vectors.

**Serial time on the idle Mini** (min of seven in-process repeats; V6 from
the quiet paired run, V7 from `results/native_phase_timing_v7c.json`; the
frames decoded serially, one thread):

| image | size | V6 encode ns/px | **V7 encode** | V7 encode ms | PNG9 encode ms | V6 decode ns/px | **V7 decode** | V7 decode ms | PNG9 decode ms |
|---|---|---|---|---|---|---|---|---|---|
| dog-half | 512×512 | 123 | 118 | 31.0 | 35.2 | 126 | 117 | 30.8 | 3.0 |
| smooth-half | 512×512 | 68 | 62 | 16.2 | 93.5 | 71 | 65 | 17.2 | 1.5 |
| reference | 1024×512 | 97 | 92 | 48.3 | 100.1 | 101 | 91 | 47.9 | 4.8 |
| kodim01 | 768×512 | 113 | 110 | 43.2 | 44.7 | 115 | 109 | 42.9 | 4.9 |
| kodim23 | 768×512 | 109 | 105 | 41.2 | 78.1 | 112 | 105 | 41.2 | 4.1 |
| smooth | 384×256 | 64 | 56 | 5.5 | 35.9 | 67 | 60 | 5.9 | 0.6 |
| checked_list_box_screen | 1259×760 | 71 | 70 | 67.2 | 138.5 | 73 | 71 | 68.0 | 8.2 |
| correspondence-macos | 1120×720 | 42 | 39 | 31.7 | 33.4 | 39 | 36 | 28.7 | 3.1 |
| retained_smooth_noise_one | 512×512 | 101 | 95 | 24.8 | 119.7 | 108 | 100 | 26.2 | 2.6 |
| noise8 | 128×128 | 140 | 128 | 2.1 | 0.8 | 141 | 126 | 2.1 | 0.2 |

The tightened frame kernel is now at or below V6's serial time on every
image (encode −2% to −12%, decode −3% to −10%) while being decodable frame by
frame in parallel; the per-frame overhead measured on the pod has been
absorbed by the prefix-sum moments at the borders. Encode is within 20% of
PNG9 on every photograph and 2–6× faster than PNG9 on smooth and GUI
content; decode is 9–13× PNG9's on one thread, and divides by the thread
count across frames. Instruction counts of this kernel are to be taken on
the pod when it is free.
