# Avoid materializing rejected entropy packets

This experiment reduces the work of failed compression attempts while preserving
the preceding candidate's encoded bytes. The complete 222-image corpus is exact
and byte-identical. Random-input entropy encoding improves consistently in a
direct paired comparison, but an overall encoder improvement is not established:
several other cases are neutral or regress. Preserve the preceding combined
candidate as the comparison rather than declaring this an accepted replacement.

## Exact rejection and deferred copies

The limited entropy interfaces return the old packet's exact size and bytes only
when that size is strictly below the caller's limit. Zero means rejected; output
is partial scratch and must not be decoded or transmitted. Input, output and
scratch retain the original disjointness and capacity contracts. The unlimited
interfaces choose a limit above the maximum emitted packet size.

Three operations change:

1. The Huffman coder rejects uncompetitive raw packets before copying input into
   them. Its exact lower bound `header + floor(total_code_bits/8)` can reject a
   coded attempt before bit emission. Canonical codes are constructed only after
   those checks. Actual stream rounding and final raw fallback are still checked
   before acceptance; a lower bound alone never accepts a packet.
2. The match coder records raw-block source/destination extents instead of copying
   immediately. If the whole-frame raw fallback wins, it copies the source once.
   If the block representation wins, it fills the deferred raw bodies before
   returning. At most 17 descriptors fit the admitted frame; each descriptor has
   three size fields, adding 408 declared stack bytes on this target.
3. The image codec passes its exact stored-literal size as the limit. If the
   accumulated block packet and the alternative whole-frame raw packet both
   cannot meet that limit, the matcher stops. Later blocks only increase the
   accumulated candidate size. The caller then uses the original literal bytes,
   exactly as before, without materializing an entropy packet it would discard.

This gate compares byte counts, not a probability estimate. It does not change
match search, the selected representation, the decoder, or image reconstruction.
All other encoder behavior retains the preceding exact uniform-depth shortcut
and split histogram. No runtime image-model selection is added.

## Complete comparison with released CNG

Ratios are candidate divided by released CNG with Zstandard 1.5.7 level 3.
Each image has seven alternating paired trials after warm-up; totals sum their
medians. Execution is Apple A18 Pro; binaries were built on M4 with non-Apple
LLVM 23.1.2, C++20 Release. Timing scope is inherited from
[`whole_codec`](../whole_codec/README.md), including codec allocations and failed
attempts but excluding file I/O and caller pixel allocation.

| Corpus | Encode | Decode | Entropy encode |
|---|---:|---:|---:|
| General, 131 images | 0.938144 | 1.023195 | 0.750852 |
| RGBA, 80 images | 0.921761 | 1.022158 | 0.727852 |
| Composite bodies, 11 images | 1.058859 | 1.008137 | 1.063322 |

These totals are slightly worse for encoding than the previous combined
candidate's separate run. They do not establish an aggregate improvement.
Random-image encoding in this run takes 1.241 ms versus 1.204 ms for the release,
about 3.1% more time. Its entropy stage takes 0.091 versus 0.042 ms. File sizes
and the still-frequent size regressions are unchanged from the earlier candidate.

## Direct comparison with the preceding candidate

`build_comparison.sh` compiles the preceding combined codec under separate
namespaces, then links both candidates into the same executable using the
existing probe. The reference is the preceding CNE implementation, not Zstandard.
Input is an actual previously emitted CNE file. Both implementations must reproduce
that file and its exact pixels. Shared image helper sources are linked once.

For seven selected cases, five separate processes each run seven alternating
paired trials. The table reports the median of the five per-process ratios.
All samples are retained; these values are not confidence intervals.

| Case | Complete encode ratio | Entropy encode ratio |
|---|---:|---:|
| `smooth` | 1.008775 | 0.979157 |
| `random` | 0.950233 | 0.867839 |
| `mixed_order_noise` | 0.976773 | 0.989743 |
| `kodim01` | 1.027866 | 1.046024 |
| `rectangle_on_gradient` | 1.002722 | 1.000000 |
| `large_smooth_rgba` | 0.989307 | 0.998978 |
| `composite_control_states` | 0.997618 | 0.990099 |

Random entropy encoding improves in all five processes, with ratios from 0.810
to 0.873. Complete random encoding improves in four of five. `kodim01` complete
encoding regresses in four of five. Other small differences vary across runs.
Decoder timing also differs despite unchanged decoder algorithms; do not credit
those differences as a decoding optimization. Code layout, memory state and
measurement variability are not isolated by this experiment.

The source removes definite copies and permits early rejection, but no hardware
memory-traffic counters were collected. The result supports retaining the raw
attempt optimization for further work, not claiming a universal speed win.

## Checks and reproduction

The native match check passes 432 deterministic cases at exact output capacity.
Each also tests five limits: zero, five, one below its encoded size, equal to its
encoded size, and one above. Accepted packets must be byte-identical; rejected
ones must return zero. Its vector file is identical to the previous match vectors,
and 414 of those vectors pass the independent scalar inverse.

The Huffman checks pass 368 old-packet comparisons and inverses, each with seven
limits covering zero, short headers and the exact packet boundary. They also
retain the preceding 128 uniform-depth distribution checks. All 222 images pass
both decoder interfaces and match the prior candidate file hashes. The direct
comparison additionally checks both candidates in 35 process runs.

`evidence/corpus.json.gz` and `direct.json.gz` retain every sample and source/input
identity. `checks.json` records binary and vector hashes, compile definitions and
case counts. The direct build script and comparison runner were added after the
full corpus run, so only the direct report's source map includes them. Preserve
the recorded source versions when extending the experiment.

From the repository root, serialize these mirror/build commands:

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/raw_fallback -B .build/raw-fallback -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/raw-fallback -j 4
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/raw_fallback/build_comparison.sh
```

After copying binaries from the selected mirror, run locally and sequentially:

```sh
.build/raw-fallback/cng_fallback_huffman
.build/raw-fallback/cng_fallback_checks .build/raw-fallback/vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/raw-fallback/vectors.bin
cmp .build/raw-fallback/vectors.bin .build/whole-codec/bounds_vectors.bin
python3 -B experiments/raw_fallback/run.py --binary .build/raw-fallback/cng_fallback_probe --corpus /path/to/cng-research --output .build/raw-fallback/new-corpus
python3 -B experiments/raw_fallback/compare.py --output .build/raw-fallback/new-direct
python3 -B experiments/raw_fallback/verify.py --artifacts .build/raw-fallback
python3 -B tools/check_evidence.py
```

Both runners refuse output-directory overwrites. No automated memory integrity
review was initiated. Published source hashes and the release format remain
unchanged. The original replacement goal remains incomplete.
