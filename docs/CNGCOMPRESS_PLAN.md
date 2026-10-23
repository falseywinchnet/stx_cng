# cngcompress programme

The owner named the in-house method **cngcompress**. The requested deliverable is
a standalone **stx_cngcompress** library, comparable in role to zlib, integrated
into stx_cng and then published **if the measured result beats the current
Zstandard-based decoder**. The owner made that final delivery conditional; it is
not required if the completed investigation fails that comparison. This document
tracks the full objective; an
experiment or benchmark win alone does not complete it.

## Required work and evidence

1. Preserve the current bounded matcher and compact Huffman length table as the
   starting point. Any later change must account for noise, simple patterns and
   long-distance repetitions, not only aggregate image totals.
2. Investigate PivCo-style bulk reconstruction with portable common-width
   operations. Compare identical code lengths, inspect arm64 and baseline SSE2
   generated code, account for memory accesses and workspace, and measure full
   decoder behavior after primitive experiments. No platform-specific intrinsic
   forks or wider-register requirement is implied.
3. Test prefix-width integers and separated control data independently, preserving
   the matcher while attributing each grammar change's size, encoding, decoding
   and memory costs. Examine adjacent-query shared work under the actual packet
   costs before transferring any parser dominance claim.
4. Select the measured construction and implement stx_cngcompress in house style.
   Specify a stable standalone API, ownership, caller buffers/workspaces, format,
   bounds, expected failures, versioning, build/install/export behavior, license
   and provenance. A runnable independent installed consumer is required evidence
   of a standalone package, not merely a renamed experimental namespace.
5. Optimize the chosen implementation for the compiler and common SIMD width,
   with assembly and complete-operation measurements. Preserve negative results.
   A small encode increase is acceptable for a repeatable decode benefit with
   comparable or smaller files; do not require every operation to improve.
6. Wire the library into stx_cng, deliberately version its format as needed, and
   verify image reconstruction, resource bounds, standalone images and both
   admitted collection forms. Record the compatibility behavior of earlier CNG
   files and update format documentation and release evidence.
7. Finish packaging and documentation, complete the authorized verification gates,
   and publish the resulting work. No publication or completion is implied by
   local experiment commits. Do not initiate an automated memory integrity review
   without the owner's request; its PASS/STOP workflow remains separate.

## Current state

Steps 1–3 are measured and reviewed. The selected construction retains the
bounded matcher, LEB128 interleaved tokens and compact exact-length Huffman table.
Portable PivCo reconstruction loses at complete-packet scope. Prefix-width and
separated-control trials produce only sub-percent decode changes while adding
bytes/encode work; they are not selected. The adjacent-query cost review rejects
transferring zxc's width-only dominance premise to fitted byte-Huffman costs.

`experiments/cngcompress_gate` verifies the current GitHub release and performs
three fresh 222-image runs. The original candidate uses 7.2–8.3% less aggregate
complete decode time; the selected short-table variant uses 8.0% less decode time
and 0.84% fewer bytes. It preserves every candidate file byte. The short-table
routine reduces a packed 64-glyph regression from 72–74 microseconds to
19–25 microseconds per atlas. Both collection forms round-trip exactly through
actual wrappers. Size regressions, composite timing variation and higher decoder
workspace remain explicit in the gate report.

The owner's conditional performance threshold is met on the recorded corpus,
not universally across images. Proceed with standalone **stx_cngcompress** using
`experiments/cngcompress_gate/huffman_short.cpp` and the retained matcher as the
engineering starting point. Steps 4–7 are still outstanding: public API and
format/version contracts, standalone build/install/consumer evidence, final
optimization checks, CNG integration with compatibility behavior, documentation
and publication. This goal is active; the experimental wrapper is not shipping
integration and the existing release remains unchanged.
