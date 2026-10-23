# stx_cng

CNG means Computational Network Graphics. This is the independent C++ library
repository, not the historical research workspace and not GUI.Forms.

Read docs/PROGRAMMING_HOUSE_STYLE.md before changing first-party code. Preserve
explicit types, named operations, buffer lifetimes and validation-before-mutation
contracts. No auto, lambdas, trailing returns, structured bindings or pointer
arrow spelling. Match existing naming and comment density.

The selected construction and wire grammar are in docs/FORMAT.md. Exactly two
collection forms exist: packed standalone images, or one whole composite decoded
then sliced. Do not add a shared-dictionary independent-access form. Support-32
preview is not implemented. Preserve current signatures until an explicit format
revision is approved.

Build and ordinary checks:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure

Use the configured non-Apple LLVM toolchain for comparable macOS measurements.
On the owner's local machine, follow its m4host/m4build policy and run the above
commands through m4build from this repository; do not edit its remote mirror.
No dependency installation is implied by a build task.

The initial release has ordinary exactness/resource evidence, not a completed
automated memory integrity review. Do not silently initiate that separate review
workflow. Do not claim public-input audit coverage from round-trip tests.

Preserve negative performance results and report complete-operation timings,
not just kernel timings. Zstandard is pinned to 1.5.7 because the encoder uses
its advanced context API. Recheck bytes, capacity and performance when changing
that dependency. Keep dependency notices and the MIT license intact.

## In-house entropy experiment

This study is isolated in `experiments/entropy`; it does not change the release
wire format. Build on the selected M4 host without installing dependencies:

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/entropy -B .build/entropy -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/entropy -j 4
```

Serialize mirror sync and builds. Completed arm64 binaries may be copied back
for A18 Pro measurements; record the execution CPU separately from the build
host. The corpus runner refuses to overwrite reports. See
`experiments/entropy/EIGHTH_ROUND.md` for the current candidate, preserved
negative results, exactness commands and the next algorithmic requirement.
The study is not complete: small structured cases still lose severely and no
whole-image replacement has passed the original goal. Never substitute corpus
totals or entropy-only timings for that acceptance.

The isolated whole-image experiment is in `experiments/whole_codec`. Its private
CNE signature is not a public format revision. See its README for build commands,
complete-operation measurements, retained failures and evidence verification.
The first 222-image pass is exact, but size regressions remain frequent and
complete decoding is slightly slower overall. Preserve its recorded source
versions when extending the study; published codec sources remain unchanged.

`experiments/entropy_reduction` carries the next encoder candidate: an exact
uniform-depth Huffman shortcut plus split frequency counting. All 222 emitted
files match the preceding candidate byte for byte. Its README records isolated
and complete-image measurements, short-input regressions and reproduction steps.
Keep size and decoder gaps explicit; this is further progress, not acceptance.

`experiments/raw_fallback` preserves a strict-size rejection and deferred-copy
trial. It improves random attempt cost but has mixed wider timing results; do not
declare it an overall win. `experiments/parser_review` reviews the sibling's MTF
continuation result, reproduces its native rANS order witness, and verifies exact
Huffman packet counting on 2,953 retained packets. That count is a next-parser
research building block, not a measured parser speed improvement.

`experiments/parse_edits` records a bounded single-match-removal negative result:
54,192 alternatives save only 0.005% of retained entropy-frame bytes. Do not
promote that search or mistake its checked diagnostic timing for production work.
`experiments/length_limited` replaces rounded Huffman populations with an exact
package-merge construction for overlong trees. Its 222-image candidate saves
109,611 bytes versus the preceding combined encoder, with no larger complete
image in that corpus. Read its measured size/encode/decode limits before extending
it. The owner accepted a separate investigation into total packet cost (length
table plus independently padded streams); that result is recorded in `experiments/packet_polish`. Keep the
Zstandard replacement goal open, especially frequent size regressions and decode
parity. These remain private CNE experiments, not a public format revision.

`experiments/decode_tables` records a grouped Huffman reader. Complete-image and
frame timings improve in aggregate, while separately linked packet timings
regress; retain that discrepancy and the negative short-table/tail variants.
Every emitted image remains byte-identical to the exact-length candidate. The
owner accepts a small encode-time increase for a repeatable decode improvement
with comparable or smaller files. Use that tradeoff rather than demanding every
operation improve; preserve individual-case regressions and measurement limits.

`experiments/packet_polish` independently rewrites and tests the sibling's exact
packet-cost scorer and bounded length edits. Two passes save only 1,827 bytes from
167.7 MB of retained compressed packets and add material search work. Preserve it
as a negative implementation experiment, retaining the exact objective and lower
bound for future parser research. It is not part of the working encoder.

`experiments/zxc_comparison` pins zxc 0.15.0 and records frame, noise/repetition,
and equal-code Huffman comparisons. Level 7 decodes retained frames faster but
encodes much slower and stores more bytes. PivCo with NEON wins the isolated
layout comparison; disabling explicit SIMD loses badly. Preserve that portability
limit, extra workspace and small-table overhead. The owner's intent includes
selectively improving our codec from the reference's ideas, not only evaluating
replacement. Keep the existing matcher and compact headers while investigating
portable bulk merges and simpler control decoding; no new public modes or runtime
dependency are implied. These are not whole-image comparison measurements.

`experiments/cngcompress_bulk` isolates portable binary reconstruction merges at
fixed 12-bit Huffman lengths. Its paired word expansion vectorizes at 128 bits
on arm64 and strict SSE2, but the measured runs exclude complete decoder setup,
flat decoding and constant-child specialization. Preserve the 3.53 merged bytes
per symbol and source-level traffic accounting; kernel improvements do not yet
establish a full-decoder gain. Read `docs/CNGCOMPRESS_PLAN.md` for the owner's
full standalone stx_cngcompress, integration and publication objective. Keep it
open until all deliverables are complete. The initial x86 assembly's default
SSE4.1 target is recorded explicitly; only corrected later flags establish an
SSE2 inspection target. No x86 execution or memory review is implied.
