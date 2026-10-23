# Complete-image decision gate for cngcompress

The retained in-house construction beats the current released Zstandard-based
codec in aggregate complete-image decoding on this corpus. This satisfies the
owner's condition for proceeding with standalone packaging and integration.
It does not establish superiority for every image or collection. Those remaining
deliverables are not complete, and no release source or public signature changes
in this experiment.

The release comparator is GitHub `main` commit
`a1797b840ad3a209241ad24790ff97cdf3cff9b1`, checked with `git ls-remote` during the
run. Its source hashes match the published verification record. It uses
Zstandard 1.5.7 level 3 and system zlib 1.2.12 for CRC. The candidate keeps the
bounded matcher, LEB128 token grammar and compact exact-length Huffman tables.
The PivCo and grammar alternatives are not selected.

## Complete-image results

Non-Apple LLVM 23.1.2 builds on the selected M4 host; Apple A18 Pro executes.
Every image receives seven alternating paired trials after warmup. Codec
allocation, all transform/parse work and discarded compression attempts are
included. File I/O and caller pixel allocation are outside the image timing.
Each emitted candidate must match the preceding exact-length candidate bytes;
both decode interfaces reproduce the original source hashes. All 222 images
pass in each of three runs.

Times are sums of per-image medians, expressed as candidate/release ratios:

| Corpus | Initial decode | Repeat decode | Short-table decode | Short-table encode | Bytes |
|---|---:|---:|---:|---:|---:|
| General, 131 images | 0.919745 | 0.916124 | 0.912584 | 0.917035 | 0.992910 |
| RGBA, 80 images | 0.933098 | 0.917128 | 0.923728 | 0.912699 | 0.990180 |
| Composite bodies, 11 | 0.939078 | 0.953022 | 1.011562 | 0.997968 | 1.004546 |
| All 222 | 0.927663 | 0.916859 | **0.919528** | **0.914916** | **0.991584** |

The short-table variant is selected for packaging: approximately 8.0% less decode
time and 8.5% less encode time in its complete-image run, with 0.84% fewer bytes.
The decoder-only edit cannot cause an algorithmic encode improvement; the encode
comparison is the entire retained construction versus release, with compiler and
runtime variation included. It does not credit the short-table change for all
observed differences. Composite-body timing regresses 1.16% in that run; retain
that result rather than replacing it with an earlier favorable ratio.

There are 116 larger files: 81 general, 29 RGBA and six composites. For example,
`smooth` grows from 11,712 to 13,822 bytes, `rectangle_on_gradient` from 383 to 464,
and `large_smooth_rgba` from 21,527 to 29,178. The largest absolute growth is the
opaque socks photograph, from 2,296,537 to 2,405,372 bytes. Aggregate size is not
a claim of uniformly better compression on simple or natural images.

The original construction's first run has 70 slower image decodes; individual
rows for all runs are retained. Timing wins also appear on images whose entropy
is stored raw, so not every improvement can be attributed solely to the Huffman
symbol loop. Selection is based on complete codec behavior.

## Both collection forms

`candidate_collection.cpp/.hpp` are the released collection implementation with
only codec namespace/header and private outer signature substitutions. The
verifier checks that source transformation. Packed collections contain complete
independent candidate images. Composite collections decode one whole canvas,
then extract slices. No independently decodable shared-basis form is added.

Three deterministic fixture sets exercise both forms: 16 varying-width RGBA
controls with hidden RGB, 64 RGBA glyphs, and eight RGB noise images. Every trial
checks exact extracted bytes and the correct number of owned planes. Collection
encode includes reading fixture files; decode uses an in-memory input and includes
owned output allocation. Slice verification is outside the timer. These are
collection-wrapper tests, distinct from the 11 composite image bodies above.

The packed glyph set exposed a repeatable short-image penalty. With the original
reader, its decode time is 169 versus 95 microseconds initially, and 164 versus
92 microseconds in the repeat. `huffman_short.cpp` puts one-stream table creation
in its own routine and fills only the extent required by the maximum actual code
length. Four-stream table generation stays fixed-width. No code lengths, wire
bytes or runtime format options change. This differs from the earlier negative
width experiment, which introduced variable bounds into the shared table loop.

With this change, the packed glyph set takes 115 versus 90 microseconds initially
and 98 versus 78 microseconds in the repeat: a remaining 19–25 microsecond cost
for all 64 images, rather than 72–74 microseconds. The packed glyph container is
16,889 versus 16,622 bytes. Other collection results remain mixed, with full
per-case times and sizes in the retained CSVs. The change reduces this regression;
it does not eliminate it.

## Storage and validation

The bounded entropy workspaces remain 656,400 encode bytes and 131,088 decode
bytes. Complete-image decoder declared workspace ranges from 131,178 to 7,471,722
bytes in the candidate, versus 96,136 to 7,440,684 for release on this corpus.
Thus the aggregate timing gain does not imply lower decoder memory use. The
measurements exclude caller pixels; stream output has its separately recorded
owned buffer. Process peak RSS is for the paired process containing both codecs
and must not be presented as separate candidate/release peaks.

The short-table ordinary checks require 1,178 exact Huffman packets, 128 uniform
proof checks and 432 deterministic match boundary/distribution cases. Generated
match vectors are compared with the preceding candidate. House-style and
lifetime checks apply to the new source. Baseline SSE2 and arm64 assembly are
retained; x86 execution is not claimed. No automated memory integrity review is
initiated by this gate.

## Reproduction

Configure and build sequentially using m4build from this repository:

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/cngcompress_gate -B .build/cngcompress-gate-short -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ -DCNG_GATE_SHORT_TABLE=ON
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/cngcompress-gate-short -j4
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/cngcompress_gate/check.sh
```

Use `CNG_GATE_SHORT_TABLE=OFF` in a separate build tree for the original reader.
This is an experimental build switch, not an option exposed by the codec.
Copy completed executables from the selected mirror, then run sequentially:

```sh
python3 -B experiments/decode_tables/run.py --corpus /Users/ultimussecundai/cng --binary .build/cngcompress-gate-short/cng_whole_probe --output NEW-WHOLE
.build/cngcompress-gate-short/collections NEW-COLLECTION-DIRECTORY
.build/cngcompress-gate-short/check
.build/cngcompress-gate-short/bounds NEW-MATCH-VECTORS.bin
python3 -B experiments/cngcompress_gate/verify.py
```

The image runner verifies release/source hashes and candidate byte identities;
it refuses existing report directories. Generated raw images are removed only
after exact source-hash comparison, while encoded images remain in the local
build directory. Curated evidence retains all reports, logs, source and binary
identities and assembly. Packaging, public format/version decisions, installed
consumer checks, final integration and publication remain outstanding.
