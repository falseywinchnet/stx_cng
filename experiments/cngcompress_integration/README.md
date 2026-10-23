# Public CNG v3 integration

The production CNG target now uses the standalone stx_cngcompress C API. It links
no Zstandard library. Both entropy-frame uses change together: sequence bytes
and compressed literal bytes. Pixel parsing, prediction, colour arithmetic,
stored-literal fallback, reconstruction and collection layout are unchanged.

CHDR advances to version 3; both eight-byte signatures and SHDR v0 remain.
Version 2 inputs are explicitly rejected before pixel output. The frozen v2
implementation and its format are retained in `experiments/release_v2`; migration
requires decoding old samples and re-encoding, not editing a header. Production
does not build the legacy implementation. The comparison executable renames only
the reference namespace and links the real public v3 target normally.

## Complete measurements

Non-Apple LLVM 23.1.2 builds Release on the selected M4. Apple A18 Pro executes
the complete-image and collection comparisons. Seven alternating paired trials
follow a warmup. Complete image API timings include codec allocation and all
discarded compression attempts; file I/O, caller pixel allocation and equality
checks are outside timing. Both stream and supplied-buffer inverses are checked.

All **222 image bodies** reconstruct exactly in each of two runs. Every entropy
payload matches the selected experimental candidate byte for byte; the runner
allows only the public signature, CHDR version and its CRC to differ. Reference
encodings reproduce the frozen v2 files exactly. The same corpus identities and
dependencies are used as in the preceding gate.

Ratios below are v3 divided by released v2; smaller is better. Totals are sums of
per-image medians, not medians of ratios.

| Group | Images | Bytes | Encode first / repeat | Decode first / repeat |
| --- | ---: | ---: | ---: | ---: |
| General | 131 | 0.992910 | 0.916669 / 0.922655 | 0.917105 / 0.913335 |
| RGBA derivatives | 80 | 0.990180 | 0.906523 / 0.905705 | 0.917470 / 0.924759 |
| Composite bodies | 11 | 1.004546 | 0.959251 / 0.995323 | 0.947886 / 0.923136 |
| All | 222 | **0.991584** | **0.911137 / 0.913406** | **0.917460 / 0.920074** |

The integrated result retains approximately **8.0–8.3% less complete decode
time**, **8.7–8.9% less encode time**, and **0.84% fewer bytes**. It emits
197,257,667 bytes. This is an aggregate corpus result: 116 images are larger;
52/62 decode more slowly in the two runs. Smooth-image and tiny-image losses
remain. No claim of universal superiority or cross-machine throughput follows.

Actual collections cover 16 varying RGBA controls, 64 glyphs and eight RGB noise
images, each in packed and composite form. Both codecs and all seven trials
preserve exact extracted pixels, hidden RGB, member identities and plane
ownership. Collection encoding includes fixture file reads; decoding uses an
in-memory stream and includes owned image allocation. Timing variation is
material for these tiny operations, so both full records remain.

The packed 64-glyph set is a repeatable regression: v3 uses **121/104 microseconds**
versus **89/75 microseconds** for v2, and stores **16,889 versus 16,622 bytes**.
Other collection results are mixed; no blanket collection speed improvement is
claimed. Composite bodies alone are not collection-wrapper measurements.

## Storage and installation

The public handles occupy 656,400 encoder bytes and 131,088 decoder bytes on this
ABI. The maximum reported encoder capacity in the image corpus is 10,355,879
bytes; the supplied-output decoder reaches 7,471,722 bytes. Decoder storage is
slightly higher than v2, not a memory win.

`capacity.py` enumerates 65,536 admitted width/channel pairs at maximum admitted
height. Smaller heights cannot increase these monotone capacities. Maximum owned
capacities are 10,450,883 encoder bytes, 8,614,818 stream-decoder bytes and
7,566,276 supplied-output decoder bytes. The largest entropy input is 2,097,156
bytes, within the public 2,097,160-byte frame bound. These figures exclude caller
storage, stack, allocator metadata and process mappings; they are not peak RSS.

The native ordinary suite passes 40 generated image cases through both decode
interfaces, both collection forms and a CRC-valid v2 header rejection check.
The same suite passes in a separate installed consumer after relocating the
entire prefix and removing its original location. That consumer links only zlib,
the C++ runtime and the system runtime. CNG and the compressor both export CMake
package targets. No automated memory integrity review was initiated or claimed.

## Reproduction

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/cngcompress_integration -B .build/cng-v3-integration -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_PREFIX_PATH=/opt/homebrew -DBUILD_TESTING=ON
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/cng-v3-integration -j4
```

Copy the completed `probe` and `collections` executables back, then run
sequentially with other builds/measurements:

```sh
python3 -B experiments/cngcompress_integration/run.py --corpus /Users/ultimussecundai/cng --output NEW-IMAGES
.build/cng-v3-integration/collections NEW-COLLECTIONS
python3 -B experiments/cngcompress_integration/capacity.py
python3 -B experiments/cngcompress_integration/verify.py
```

The image runner also requires the preceding gate's retained candidate files.
Corpus images are not bundled. The installation helper `check.sh RECORD` uses
the separately configured `.build/cng-v3` production build and refuses existing
prefixes. Evidence retains both complete runs, collection rows, ordinary test and
installation logs, source identities and capacity accounting. Earlier studies
remain reproducible at their original commits; their hashes are not rewritten
to claim coverage of this production revision.
