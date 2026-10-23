# Measurements and verification

CNG's release evidence distinguishes three questions: whether every sample is
preserved, how much storage the implementation owns, and whether a measured
change improves complete encoding or decoding. These are separate claims.

## Current v3 integration

The production v3 library uses the public stx_cngcompress API and no Zstandard
runtime dependency. Two complete runs over the same 222 image bodies preserve
all samples through both decode interfaces and reproduce the selected candidate's
entropy bytes exactly. Only the public signature/header/CRC framing differs from
the private experimental files.

Ratios are v3 divided by the released Zstandard-based v2 library; lower is better.
Each image has seven alternating paired trials after warmup. Summed per-image
medians include codec allocation and discarded compression attempts, excluding
file I/O, caller pixel allocation and verification. Non-Apple LLVM 23.1.2 builds
on M4; measurements execute on Apple A18 Pro.

| Group | Bytes | Encode first / repeat | Decode first / repeat |
| --- | ---: | ---: | ---: |
| General, 131 | 0.992910 | 0.916669 / 0.922655 | 0.917105 / 0.913335 |
| RGBA derivatives, 80 | 0.990180 | 0.906523 / 0.905705 | 0.917470 / 0.924759 |
| Composite bodies, 11 | 1.004546 | 0.959251 / 0.995323 | 0.947886 / 0.923136 |
| All 222 | **0.991584** | **0.911137 / 0.913406** | **0.917460 / 0.920074** |

This is approximately 8.0–8.3% less complete decode time, 8.7–8.9% less encode
time and 0.84% fewer bytes in aggregate. There are still **116 larger files**,
and 52/62 slower decodes in the two runs. In the complete collection checks,
64 packed glyphs take 121/104 microseconds versus 89/75 microseconds for v2,
and grow from 16,622 to 16,889 bytes. Those losses remain part of the result.

On the measured 64-bit ABI, enumeration over all admitted widths/channel counts
bounds owned encoder capacity at 10,450,883 bytes, stream decoding at 8,614,818
bytes and supplied-output decoding at 7,566,276 bytes. These exclude caller
storage, stack, allocator metadata and process mappings. Decoder storage is
slightly higher than v2. The standalone compressor handles occupy 656,400 and
131,088 bytes respectively, already included in these totals.

The installed CNG consumer passes after moving its prefix and links only zlib,
the C++ runtime and the system runtime. The separate compressor consumer needs
no zlib. See the [integration report](../experiments/cngcompress_integration/README.md)
for source pins, complete records, exactness checks, collection scope and
reproduction commands. The historical v2 results below remain unchanged.

## Historical v2 release-source verification

The 0.1.0-rc1 source was built using non-Apple LLVM 23.1.2 with warnings treated as
errors, Zstandard 1.5.7, and zlib on macOS arm64. CMake configuration, build,
ordinary tests, installation and compilation of a consumer against the installed
headers/library succeeded. The immutable files in `experiments/release_v2` match the
verified implementation; [verification.json](../evidence/verification.json)
records their SHA-256 hashes.

The bundled test covers 40 generated image cases through two decoding interfaces,
then both collection forms. Separately, a frozen battery of 222 image bodies was
encoded and decoded using the installed library. Every encoded file matched its
accepted reference byte for byte, and every decoded sample matched the source.

| Group | Image bodies | Composition |
| --- | ---: | --- |
| General | 131 | Photographs and generated structure, alpha and noise controls |
| RGBA derivatives | 80 | Variants of 64 photographs, including smooth/noisy alpha |
| Composite bodies | 11 | Full canvases used by collection experiments |

The 80 RGBA variants are not 80 independent photographs. The 11 composite cases
measure inner image bodies, not the size or opening cost of a complete collection.
Third-party image files remain outside this source release; the evidence includes
names and hashes for identifying the frozen inputs. The bundled tests can be
reproduced from this checkout alone; the entire external battery cannot.

## Historical v2 complete image sizes

These sizes refer to accepted CSP v2 files reproduced exactly by the release
library. PNG references were generated with libpng 1.6.59 and zlib 1.2.12 at
compression level 9, with filtering enabled. They are not OxiPNG-optimized files.
All framing bytes in the standalone files are counted.

| Group | CNG bytes | PNG9 bytes | CNG smaller |
| --- | ---: | ---: | ---: |
| General | 98,399,047 | 111,663,405 | 130 / 131 |
| RGBA derivatives | 99,791,531 | 131,452,553 | 80 / 80 |
| Composite bodies | 741,327 | 1,000,504 | 9 / 11 |

The general battery retains a rectangle-on-texture counterexample; tiny image
bodies also expose framing costs. These results do not establish universal PNG
superiority or a result against optimized PNG encoders. Per-image byte counts
and source/file hashes are retained in the evidence.

## Historical v2 memory

The encoder's complete owned-array capacity was enumerated across the admitted
width/channel domain at maximum valid height. Including a 4 MiB ceiling for its
entropy context, the maximum is **14,000,967 bytes**. With the measured Zstandard
1.5.7 context requirement, the enumerated maximum is **11,110,239 bytes**.

These counts exclude caller stream buffers, stack, allocator overhead and process
mappings. They are not a universal process-memory bound. The decoder owns
band workspace for streaming; `decode_into` additionally relies on caller-owned
image storage, and collection decoding owns the entire decoded collection.

Fresh-process checks of the consolidated library observed:

| Quantity | Maximum bytes over 222 image bodies |
| --- | ---: |
| Encoder reported owned capacity | 11,015,234 |
| Encoder process peak RSS | 8,962,048 |
| Stream decoder reported owned capacity | 8,489,004 |
| Stream decoder process peak RSS | 7,356,416 |

Peak RSS may be lower than allocated capacity because allocation does not imply
that every page is resident or touched. These single-call checks establish
sample equality and observed resource use, not comparative throughput.

## Why literal unpacking remains separate

A final experiment fused nibble unpacking, inverse colour and reconstruction.
It removed approximately one MiB of scratch capacity on large bands and a logical
intermediate write/read pair for each compressed residual byte. It nevertheless
lost complete-decoder speed in both full repeats:

| Group | Fused / separate, first run | Fused / separate, repeat |
| --- | ---: | ---: |
| General | 1.02362 | 1.01119 |
| RGBA derivatives | 1.07544 | 1.04179 |
| Composite bodies | 1.03368 | 1.05445 |

Ratios compare sums of per-image median decode times. Each image received a
warm-up and 31 rotated trials per implementation in one binary. Output allocation
was included; equality checks followed timing. Both runs used the local Apple
A18 Pro and Zstandard 1.5.7. Host activity was recorded, but the machine was not
hardware-isolated. All images were exact, as were 108 additional directed cases
that forced compressed literals through small and odd-tail inputs.

Compiler remarks reported both the separate unpack loop and fused literal loops
as not loop-vectorized. A claim that fusion simply disabled SIMD is unsupported.
Fewer intermediate bytes did not translate to lower runtime. The release keeps
the faster separate stages. [Raw paired timings](../evidence/literal-fusion.json)
are published without local process inventories or machine paths.

## Historical interpretation

Earlier research measured useful speed gains against PNG using closely related
candidates. The consolidated source package has its own equality/resource check,
but no new full controlled encode/decode throughput comparison is claimed here.
The package should be measured in the consuming application with its actual input,
output ownership, allocator, compiler and workload.

No automated memory integrity review was completed for this release. Ordinary
round trips, compiler warnings and capacity accounting do not substitute for it.
