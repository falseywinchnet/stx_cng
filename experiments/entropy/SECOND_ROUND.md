# Direct events and paired Huffman coding

The replacement is **not ready**. We now have an independently implemented
backend that improves aggregate size and entropy-encoding time, with decoding
close to the control on the larger groups. But it loses badly on several small,
structured images. That fails the intended requirement: usually as good or
better, rarely worse, and particularly effective on ordered images.

The public codec and its source evidence remain unchanged. All alternatives
here are isolated experiments on the same frozen 222-file corpus.

## What changed

**Direct sequence events.** Three fixed alphabets represent literal counts,
match lengths and distances. Values below 16 have exact symbols; larger values
use their exponent and two leading fraction bits, with remaining bits stored
directly. Positive match lengths subtract three, reserving zero for the final
literal-only record. A zero distance retains its prediction meaning; a repeated
nonzero distance has its own symbol. Three 1,024-slot models share four rANS
states. The candidate still converts the original byte planes on entry/exit,
so these measurements include that conversion cost.

This reduces sequence payload sizes by 10.9% on RGBA and 8.9% on composite
bodies. General sequences remain 14.3% larger. Decoding remains expensive.

**Separate rANS input streams.** Four independent input cursors did not improve
the result. Encoding became slower and decoding still took more than twice
the control's time. The source and measurements are retained.

**Paired Huffman decoding.** An independent canonical Huffman coder uses four
streams and a 4,096-entry table that emits one or two bytes per lookup. It uses
ordinary integer operations and no platform-specific intrinsics. Code lengths
are bounded at twelve bits by rebuilding with a common frequency floor when
needed. That is a measured heuristic, not an optimal length-limited construction.
The canonical construction follows the mathematical description in
[RFC 1951, section 3.2.2](https://www.rfc-editor.org/rfc/rfc1951.html#section-3.2.2);
no external implementation was copied. These packets are not DEFLATE packets.

The first Huffman trial reduced literal encoding time to roughly 58–60% of
Zstandard's on the larger groups, but decoding remained about 1.5–1.6 times as
expensive. Batching four rounds of output-bound checks made decoding slower.

**Local frequency tables.** One histogram per whole frame mixes the different
channel and low/high residual planes. Fixed 128 KiB blocks improve local
frequency estimates without an image classifier or a search among models.
Each block uses the same construction; incompressible packets have a stored
escape. Header, table, padding and stored-escape bytes are included in size.

**Visible reader lifetimes.** Assembly inspection showed hot reader state
repeatedly loaded and stored on the stack. Removing the per-lookup consumed-bit
counter and expressing the disjoint output contract reduced work. Giving the
four hot readers individual local variables, then publishing them back once
before the indexed tail traversal, allowed the inspected ARM build to retain
their main recurrence state in registers and combine adjacent byte stores.
This does not establish how another compiler or architecture allocates them.

## Current measurements

The current candidate is `cng_entropy_probe_scalars`: direct sequence events,
fixed-block Huffman literals, explicit reader locals and a restricted output
kernel. It uses the same encoded bytes as the blocked and restricted variants.

| Group | Current file bytes | Accounted candidate bytes | Size change | Encode-time ratio, two runs | Decode-time ratio, two runs |
|---|---:|---:|---:|---:|---:|
| General, 131 files | 98,399,047 | 97,995,597 | −0.410% | 0.696 / 0.708 | 1.048 / 1.043 |
| RGBA, 80 files | 99,791,531 | 98,969,204 | −0.824% | 0.670 / 0.670 | 1.033 / 1.012 |
| Composite bodies, 11 files | 741,327 | 734,221 | −0.959% | 0.805 / 0.833 | 1.200 / 1.214 |

Ratios are candidate time divided by Zstandard time; lower is better. Each run
uses seven paired trials per frame, alternating order, following a warm-up.
The table sums per-frame medians. The two runs are repeat observations, not
confidence intervals. The execution CPU is Apple A18 Pro; the build host is
the M4 with non-Apple LLVM 23.1.2, release flags, and Zstandard 1.5.7 level 3.

These are **entropy-operation** timings and file-size accounting. They do not
measure complete image encoding/decoding, discarded compression attempts on
stored-literal bands, or collection opening. No new-format CNG image was emitted.
All transmitted candidate bytes and the existing outer stored-literal fallback
are counted. The existing RGBA and composite corpus limitations still apply.

## The decisive failure: ordered images

Aggregate totals conceal the most important remaining problem:

| Image | Current bytes | Accounted candidate bytes |
|---|---:|---:|
| `rectangle_on_gradient` | 383 | 8,465 |
| `composite_control_states` | 1,083 | 3,573 |
| `smooth` | 11,712 | 14,070 |
| `edges_alpha` | 996 | 1,138 |
| `mixed_order_noise` | 150,930 | 154,925 |
| `nyquist_checker` | 133 | 156 |
| `random` | 295,006 | 295,024 |
| `kodim01` | 565,532 | 561,197 |

Only 34 of 131 general files improve; 97 worsen. RGBA has 45 improvements and
35 regressions; composites have three improvements and eight regressions.
The unchanged stored noise data does not establish candidate noise-encoding
speed. None of these limitations should be obscured by the aggregate gains.

For `rectangle_on_gradient`, the transformed literal stream is 28,912 bytes.
Zstandard represents it in 249 bytes, versus 8,129 for this candidate. Its
independent-byte empirical entropy is about 7,501 bytes. Even an optimistic
previous-byte conditional estimate is about 1,405 bytes before any model cost.
These measurements strongly point to repeated strings beyond simple symbol
frequency or adjacent-symbol correlation. Sequence bytes also regress there,
from 61 compressed bytes to 263.

## Next required work

The next algorithmic experiment should add a small deterministic match finder
for the repeated strings remaining in transformed literals and sequence data.
Start with a bounded, single-candidate short-word index, exact extension, compact
copy descriptors and a stored escape for incompressible blocks. Measure its
maintenance work on noise as well as its gain on the structured regressions.
Do not select it only for known test images or use Zstandard as a fallback.

After this recovers the simple-image losses, remove avoidable conversion at the
direct-event boundary: production CNG already holds sequence records, whereas
this probe converts them to byte planes and back. Compare like-for-like complete
operations, including the control's sequence packing/unpacking. Then integrate
an explicitly experimental image format and measure complete encode/decode,
exact pixels, size distributions and workspace before deciding whether the
original replacement goal has been achieved.

The present evidence does not justify declaring that goal complete or modifying
the published release. The promising pieces are the fast literal coder, local
statistics and explicit hot-state lifetimes; the missing capability is economical
representation of remaining repeated strings.

## Exactness and reproducibility

- Direct events: 112 native cases, including maximum record counts and bucket
  boundaries, plus 105 independent scalar inverse checks.
- Split rANS and the two initial Huffman decoders: 102 native cases each and
  96 independent inverse cases each.
- Fixed-block, restricted and named-reader variants: 138 native cases each,
  including both sides of 128 KiB and 256 KiB boundaries, and 132 independent
  scalar inverses each. Their emitted deterministic vectors are byte-identical.
- Three Fibonacci-weight populations exercise the bounded Huffman-depth rebuild;
  all pass native round trips and the independent bit-by-bit inverse.
- Every complete corpus experiment reconstructs all 1,410 retained entropy
  frames exactly and recreates the original Zstandard frames byte for byte.

These are valid-input checks, not an automated memory integrity review.

Build instructions are in the root `AGENTS.md`. Representative local checks,
after copying the completed arm64 executables from the M4 build, are:

```sh
.build/entropy/cng_entropy_checks_events .build/entropy/events_vectors.bin
python3 -B experiments/entropy/events_oracle.py .build/entropy/events_vectors.bin
.build/entropy/cng_entropy_checks_scalars .build/entropy/scalars_vectors.bin
python3 -B experiments/entropy/blocked_oracle.py .build/entropy/scalars_vectors.bin
.build/entropy/cng_entropy_depth_checks .build/entropy/depth_vectors.bin
python3 -B experiments/entropy/huffman_depth_oracle.py .build/entropy/depth_vectors.bin
python3 -B experiments/entropy/run.py --corpus /path/to/cng-research \
  --binary .build/entropy/cng_entropy_probe_scalars \
  --output .build/entropy/new-scalars-results.json
python3 -B experiments/entropy/summarize.py
python3 -B tools/check_evidence.py
```

`evidence/results_*.json.gz` retains all samples and source/binary identities.
The historical `rans_*` JSON timing keys mean the candidate operation even in
Huffman trials; the report name and pinned executable identify the algorithm.
`*_reference/` directories preserve measured source versions as subsequent
experiments change the harness. `summarize.py` checks every recorded source hash
against these files before presenting results. External image data is not
distributed here. Performance runs and native builds were serialized per host.
