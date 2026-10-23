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

The prior goal turn made progress: commit `68f28b9` pinned zxc and produced actual
frame, noise/repetition and fixed-code layout measurements. Its NEON PivCo result
is promising; the no-explicit-intrinsics result regresses. This motivates testing
the primitive itself before assuming a portable implementation has the same gain.

`experiments/cngcompress_bulk` now contains first-party portable merge candidates
and a corpus-derived primitive comparison. This is step 2 in progress. It is not
a complete entropy decoder and does not yet establish a replacement format.
Prefix/control experiments, standalone packaging, integration and publication
remain outstanding.

The first primitive round is exact across 73,984 fixtures and 2,953 corpus
preparations. Paired expansion takes 42.00% of the paired scalar merge time in
the final run and produces common-width SIMD on both inspected targets. However,
non-flat merges alone write 3.5333 bytes per source symbol. Full reconstruction,
constant-child handling and flat decoding must be measured before selecting it.
See the experiment README for all revisions, negative cases and traffic limits.

The complete layout follow-up is now measured. It retains the compact table,
uses n-byte decode scratch, collapses flat subtrees, and specializes constant
children. The best tested complete variant takes 1.7145 times the current Huffman
decoder time at essentially equal size. Do not promote it. Prefix-width integers,
separated controls and the adjacent-query cost review remain outstanding; after
those, compare the selected candidate with the released Zstandard decoder at
complete-image scope to resolve the owner's conditional delivery requirement.

The independent grammar trials are now measured at complete-frame scope. Prefix
integers preserve LEB widths; splitting retains one Huffman table. Initial,
repeat and explicit-width-reader runs show only sub-percent aggregate decode
changes, while all variants add bytes and encode work. Splitting also adds
131,088 bytes of encoder workspace and a literal copy. Retain the original
grammar. The adjacent-query review demonstrates why zxc's width-only static
cost dominance does not transfer to our Huffman-coded byte costs, and observes
that the current greedy matcher already skips committed-match interiors. No
parser change is selected. Next: fresh complete-image comparison of the retained
candidate against the released Zstandard implementation, then resolve the
conditional standalone/integration/publication step from that evidence.
