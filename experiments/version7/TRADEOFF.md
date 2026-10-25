# The four-axis tradeoff front of the frame codec (2026-10-25)

The owner's objective for the shipped format is not the smallest file but the
best point among four things at once: progressive decode, chunkwise processing
with recoverability, speed, and size. This document records a harness that
scores a grid of fixed constructions on all four axes over the 236-image corpus
and reports the resulting front. Every point is one fixed construction with no
runtime selector; the shipped format is one point chosen from the front.

Harness: `tradeoff/tradeoff_harness.py` (research-tree module, included for reference) (sizes, damage, progressive, report;
run on the M4 with one niced job of four workers), `research/tradeoff_pod.py`
(cachegrind instruction counts and threaded decode on the x86-64 pod),
`research/tradeoff_variants.py` (kernel variants generated from
`native/phase/phase_codec.cpp`; each textual patch must match exactly once).
Driver additions: `salvage` (decode every frame whose checksum passes, write a
per-frame mask) and `decode_frame_range` (a worker's share of a threaded decode
with one workspace). Records: `evidence/tradeoff_sizes.json`,
`evidence/tradeoff_damage.json`, `evidence/tradeoff_pod.json`,
`evidence/tradeoff_front.json`.

## 1. Grid

| axis of the grid | values | what it is |
|---|---|---|
| kernel | `base`, `anchor` | `base` is the shipped CNGPHI07. `anchor` (signature CNGPHX1A) starts every frame after the first from the repetition table and adaptive models the top-left frame left behind, so frames depend on the anchor frame only; a worker decodes the anchor once and then its frames. |
| frame side | 128, 255 | carried in the stream |
| mosaic | none, m16, m16+m4 | redundant coarse layers: block means over 16×16 cells (m16) and 4×4 cells (m4), each coded as a small CNGPHI07 image by the base kernel and stored twice. The first layer is the progressive first picture and the fill for frames lost to damage. |

Not in the grid: zero-region skipping in the decoder. It is a stream change
whose speed effect cannot be estimated from the trace alone, and it is left
for a separate experiment. Forward error correction of frame payloads (parity
frames) is likewise not implemented; the damage axis measures containment and
mosaic fill, not reconstruction.

## 2. Scores

- **Size:** complete file bytes (stream plus both copies of every mosaic layer)
  over OptiPNG 7.9.1 `-o7 -strip all` bytes, summed over the corpus and per
  group. Every stream is decoded back and compared with the source.
- **Progressive:** fraction of the file read before the first picture (the
  index plus the first mosaic layer, or the index plus the first frame when
  there is no mosaic), its PSNR against the original; the same for the second
  layer; and the fraction of the file read before half the pixels are exact
  (frames in raster order). Medians over images.
- **Recoverability:** bursts of 512 random bytes placed at uniformly random
  positions over the whole file, `ceil(fraction × length / 512)` of them for
  fractions 1%, 5% and 20%, three seeds, the same positions (as file
  fractions) for both kernels. A frame is lost when its checksum fails; the
  anchor kernel loses every frame when the anchor frame is lost; a mosaic
  layer survives when either copy decodes, and the finest surviving layer
  fills the lost frames (grey 128 when none survives). Scores: fraction of
  pixels exact, fraction filled from a mosaic, PSNR of the restored image.
- **Speed:** cachegrind instructions and first-level data misses per pixel of
  a one-thread decode (deterministic), and threaded frame-decode time with 1,
  2 and 4 threads (minimum of five), on seven fixed images on the pod.
## 3. Size and progressive decode (Mini, all 236 images exact at every point)

Bytes are complete files over OptiPNG; the mosaic columns include both copies of each layer. Progressive
columns are medians over images: the file fraction read before the first picture and its PSNR, before the
second layer, and before half the pixels are exact (over the 109 images with at least four frames).

| kernel | side | mosaic | bytes / OptiPNG | photographs | captures | tiles | emoji | first picture: file % / PSNR dB | second layer | half the pixels exact: file % |
|---|---|---|---|---|---|---|---|---|---|---|
| base | 128 | none | 0.777 | 0.754 | 0.684 | 1.89 | 1.099 | 99.1 / - | - | 50.5 |
| base | 128 | m16 | 0.790 | 0.763 | 0.699 | 1.98 | 1.202 | 4.5 / 14.6 | - | 57.9 |
| base | 128 | m16+m4 | 0.919 | 0.872 | 0.845 | 2.21 | 1.791 | 3.1 / 14.6 | 21.8 / 21.0 | 66.9 |
| base | 255 | none | 0.768 | 0.750 | 0.670 | 1.38 | 1.099 | 99.1 / - | - | 54.5 |
| base | 255 | m16 | 0.781 | 0.758 | 0.685 | 1.47 | 1.202 | 4.5 / 14.6 | - | 68.7 |
| base | 255 | m16+m4 | 0.910 | 0.868 | 0.831 | 1.70 | 1.791 | 3.1 / 14.6 | 21.8 / 21.0 | 76.3 |
| anchor | 128 | none | 0.771 | 0.751 | 0.678 | 1.51 | 1.099 | 99.1 / - | - | 52.6 |
| anchor | 128 | m16 | 0.784 | 0.759 | 0.693 | 1.60 | 1.202 | 4.5 / 14.6 | - | 67.3 |
| anchor | 128 | m16+m4 | 0.912 | 0.869 | 0.839 | 1.83 | 1.791 | 3.1 / 14.6 | 22.0 / 21.0 | 76.5 |
| anchor | 255 | none | 0.766 | 0.748 | 0.665 | 1.26 | 1.099 | 99.1 / - | - | 64.0 |
| anchor | 255 | m16 | 0.778 | 0.757 | 0.680 | 1.35 | 1.202 | 4.5 / 14.6 | - | 72.8 |
| anchor | 255 | m16+m4 | 0.907 | 0.866 | 0.826 | 1.58 | 1.791 | 3.1 / 14.6 | 22.0 / 21.0 | 81.1 |

## 4. Recoverability under burst damage (Mini)

Mean fraction of pixels decoded exactly, over all images, pixel-weighted, and over the 109 multi-frame images;
the fraction filled from a surviving mosaic and the median PSNR of the restored image at 5% damage, multi-frame
images. A 512-byte burst in a file of a few hundred bytes destroys it whatever the construction, which is
why the all-image mean is low: 149 of the 236 files are under 8 KB.

| kernel | side | mosaic | exact @1%: all / pixel-weighted / multi | exact @5% multi | exact @20% multi | filled @5% multi | PSNR @5% multi |
|---|---|---|---|---|---|---|---|
| base | 128 | none | 0.209 / 0.596 / 0.453 | 0.244 | 0.123 | 0.000 | 11.9 |
| base | 128 | m16 | 0.220 / 0.604 / 0.469 | 0.257 | 0.132 | 0.428 | 16.2 |
| base | 128 | m16+m4 | 0.387 / 0.639 / 0.514 | 0.307 | 0.176 | 0.471 | 20.5 |
| base | 255 | none | 0.083 / 0.289 / 0.179 | 0.078 | 0.038 | 0.000 | 11.2 |
| base | 255 | m16 | 0.120 / 0.324 / 0.241 | 0.106 | 0.059 | 0.534 | 16.2 |
| base | 255 | m16+m4 | 0.298 / 0.360 / 0.281 | 0.177 | 0.110 | 0.512 | 18.1 |
| anchor | 128 | none | 0.127 / 0.428 / 0.276 | 0.061 | 0.024 | 0.000 | 11.1 |
| anchor | 128 | m16 | 0.147 / 0.472 / 0.313 | 0.091 | 0.034 | 0.537 | 14.8 |
| anchor | 128 | m16+m4 | 0.320 / 0.485 / 0.369 | 0.165 | 0.102 | 0.554 | 19.8 |
| anchor | 255 | none | 0.033 / 0.132 / 0.071 | 0.007 | 0.000 | 0.000 | 10.9 |
| anchor | 255 | m16 | 0.057 / 0.155 / 0.105 | 0.021 | 0.016 | 0.593 | 15.7 |
| anchor | 255 | m16+m4 | 0.249 / 0.195 / 0.176 | 0.125 | 0.082 | 0.546 | 19.6 |

By file size class (mosaic none, 1% damage, mean fraction of pixels exact):

| class | n | base 128 | base 255 | anchor 128 |
|---|---|---|---|---|
| under 8 KB | 149 | 0.055 (header lost in 23%) | 0.013 | 0.007 |
| 8–64 KB | 40 | 0.227 | 0.138 | 0.130 |
| over 64 KB | 47 | 0.681 | 0.257 | 0.508 |

At 5% damage the large files keep 0.238 (base 128), 0.062 (base 255) and
0.071 (anchor 128) of their pixels exact. Bursts are scattered, so what
decides survival is the number of frames a burst can miss: a 20 KB frame is
hit by one of 47 random bursts with near certainty, a 128×128 frame of a
photograph is hit by one of 10 about half the time.

## 5. Reading the front

**Frame side is the containment dial, and it is cheap.** Side 128 against 255
costs 1.2% of bytes (0.777 against 0.768) and keeps 2.6× the pixels at 1%
damage and 3.8× at 5% on the large files. The structured tile fixtures are
the one group that pays for the smaller frame (1.89 against 1.38).

**One mosaic layer is the progressive and anchoring layer; the second is not
affordable as redundancy.** m16 stored twice costs 1.3% overall (1.1% on
photographs, 9% on one-frame emoji, where the layer is as large as the index)
and gives a first picture at 4.5% of the file at a median 14.6 dB over the
corpus (21 dB on photographs, 19 dB on captures, 23 dB on the tiles), and it
fills 43–59% of the pixels of a 5%-damaged multi-frame image with its block
means. m4 is a sixteenth of the pixels coded at nearly full cost: 7% of the
file per copy on photographs, 27% on emoji; stored twice it moves the whole
front from 0.78 to 0.92 of OptiPNG for a 21 dB first picture. If a second
rung is wanted it has to be coded against the first, not stored beside it.

**The anchor kernel trades containment for the tile loss.** It removes a
fifth of the structured-fixture loss (1.89 to 1.51 at side 128; 1.26 at 255)
and 0.8% of bytes overall, at the price of every frame depending on the
top-left one: at 1% damage the large files keep 0.508 of their pixels against
0.681. Duplicating the anchor frame to buy that back costs 8% of the file
(the anchor is 3.6% of a photograph but 20% of a tile fixture and 99% of an
emoji), so the anchor is not on the front as a redundancy scheme; it is on
the front only where size beats recoverability.

**The knee** is base 128 with one m16 layer: 0.790 of OptiPNG, a first picture
at 4.5% of the file, half the pixels exact by 58% of the file, 0.60 of all
pixels exact (pixel-weighted) after 1% burst damage with the rest filled from
the mosaic, and the frame-parallel decoder unchanged. Base 128 without the
mosaic is the same point 1.3% smaller with no first picture and grey fill.
Nothing on the front improves speed; that axis is measured in §6 and is a
property of the per-pixel construction, which the grid does not change.

## 6. Decoder work (pod, x86-64, GCC 13.3, valgrind cachegrind; `evidence/tradeoff_pod.json`)

Instructions and first-level data misses per pixel of a one-thread decode of
the complete file, and the threaded frame decode (minimum of five) with 1, 2
and 4 threads. The frames mode uses the default side, so threaded times exist
for the side-128 points; side 255 has the serial decode only.

| image | size | point | bytes | Ir/px | D1 miss/px | ns/px 1T | 2T | 4T |
|---|---|---|---|---|---|---|---|---|
| dog-half | 512×512 | base 128 | 384,717 | 1,959 | 9.2 | 167 | 84 | 59 |
| dog-half | 512×512 | base 255 | 382,583 | 1,954 | 10.8 | 166 | | |
| dog-half | 512×512 | anchor 128 | 381,812 | 1,969 | 9.6 | 163 | 94 | 72 |
| kodim23 | 768×512 | base 128 | 432,947 | 1,795 | 8.9 | 150 | 77 | 53 |
| kodim23 | 768×512 | base 255 | 431,706 | 1,789 | 10.9 | 148 | | |
| kodim23 | 768×512 | anchor 128 | 430,263 | 1,805 | 9.3 | 146 | 82 | 61 |
| smooth-half | 512×512 | base 128 | 33,189 | 1,456 | 6.8 | 84 | 44 | 34 |
| smooth-half | 512×512 | base 255 | 32,559 | 1,453 | 9.1 | 83 | | |
| smooth-half | 512×512 | anchor 128 | 33,052 | 1,470 | 7.2 | 82 | 47 | 43 |
| checked_list_box_screen | 1259×760 | base 128 | 464,086 | 1,357 | 6.1 | 94 | 49 | 35 |
| checked_list_box_screen | 1259×760 | base 255 | 456,319 | 1,345 | 8.8 | 92 | | |
| checked_list_box_screen | 1259×760 | anchor 128 | 459,886 | 1,370 | 6.5 | 91 | 49 | 37 |
| noise8 | 128×128 | base 128 | 29,942 | 2,289 | 8.6 | 188 | 188 | 188 |
| noise8 | 128×128 | anchor 128 | 29,942 | 2,308 | 9.2 | 188 | 194 | 318 |
| tile_16_phase_0 | 384×256 | base 128 | 11,792 | 1,029 | 3.2 | 47 | 24 | 22 |
| tile_16_phase_0 | 384×256 | base 255 | 4,136 | 979 | 5.6 | 44 | | |
| tile_16_phase_0 | 384×256 | anchor 128 | 2,472 | 988 | 3.6 | 42 | 33 | 35 |
| emoji_u1f600 | 128×128 | base 128 | 6,824 | 1,593 | 9.0 | 98 | 103 | 105 |
| emoji_u1f600 | 128×128 | anchor 128 | 6,824 | 1,615 | 9.4 | 99 | 113 | 192 |

**Reading.** The grid does not move the instruction count: side 128 and 255
are within 0.5% of each other (the frame setup and border share is already
absorbed by the prefix-sum moments), and the anchor seed adds 0.5–1% for the
table and model copy. Side 128 has 15–30% fewer first-level misses than 255
because a frame's rings and table are smaller. The threaded decode divides
the time by 2.0 at two threads and 2.7–3.2 at four for images of 16 frames or
more; the anchor kernel loses part of that because every worker decodes the
anchor frame (72 against 59 ns/px at four threads on 16 frames, and 318
against 188 on a one-frame image, where four workers each decode the whole
image). On the tile fixture the anchor removes the frame-restart loss
completely: 2,472 bytes against 11,792 for base 128 and 2,003 for the
one-stream V6. Per-pixel work is 1,000–2,300 instructions and 3–11 misses,
set by the adaptive binary decisions of the construction, and no point of the
grid changes it; speed beyond frame parallelism is a different experiment.

## 7. What the front says

| point | bytes / OptiPNG | first picture | exact after 1% damage (large files) | 4-thread decode | note |
|---|---|---|---|---|---|
| base 128, none | 0.777 | none | 0.68 | 53–59 ns/px | the shipped 0.3.0 format |
| **base 128, m16** | **0.790** | **4.5% of file, 21 dB on photographs** | **0.68, lost frames filled** | **unchanged** | **the knee** |
| base 128, m16+m4 | 0.919 | 3.1%, second rung 21 dB at 22% | 0.68 | unchanged | second layer not affordable as redundancy |
| base 255, none | 0.768 | none | 0.26 | serial only | smallest; containment 2.6× worse |
| anchor 128, none | 0.771 | none | 0.51 | 61–72 ns/px | tiles 1.51 instead of 1.89; frames depend on the anchor |
| anchor 255, none | 0.766 | none | 0.13 | serial only | smallest overall; least recoverable |

The front has three usable points: the shipped format, the same with one
redundant 16-cell mosaic layer (+1.3% bytes for a first picture at 4.5% of
the file and a plausible fill of lost frames), and side 255 for 1.2% fewer
bytes at 2.6× worse containment. The anchor kernel is the right fix for
exact-tile content but costs recoverability wherever frame 0 is damaged, and
duplicating frame 0 costs more than it saves except on large photographs; it
should not be adopted as a general construction on this evidence. The second
mosaic rung is only worth having coded against the first, which is the
support-32 integration problem the research tree has measured negative three
times, so it stays out. Zero-region skipping and payload parity frames remain
the two unmeasured levers on the speed and recoverability axes.

Python/C++ byte identity is established for the base kernel only; the anchor
variant is verified by native round trip (236 of 236 exact at both sides) and
would need its reference and vectors before any promotion.
