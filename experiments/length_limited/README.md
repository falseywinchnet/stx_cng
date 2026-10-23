# Exact bounded-depth Huffman construction

This candidate replaces repeated population rounding with package-merge when an
ordinary Huffman tree exceeds twelve bits. It preserves the current token grammar
and decoder. Across the full 222-image corpus it saves 109,611 bytes over the
preceding combined candidate: 149 images shrink and 73 are unchanged. The original
Zstandard-replacement goal remains incomplete: images larger than the released
codec are still frequent, and aggregate decoding remains slower.

## Construction and cost

The implementation follows the package-merge construction in Larmore and
Hirschberg, [A Fast Algorithm for Optimal Length-Limited Huffman Codes](https://ics.uci.edu/~dhirschb/pubs/LenLimHuff.pdf).
It is independently written C++ from the algorithm. The ordinary tree and its
sorted leaves are retained if its maximum depth is at most twelve. Otherwise,
the original populations feed package-merge; no rounded-frequency retry occurs.

At each level, adjacent lightest items are paired and merged with the original
sorted leaves. The final list contributes its first `2*n-2` items. During reverse
traversal, a selected prefix contains a prefix of packages, so their children
form a prefix of the preceding list. Leaf visits count code lengths. This avoids
recursive tree traversal and per-node selection flags.

The bounded implementation uses two 512-entry weight arrays and twelve 512-entry
origin arrays: 16,384 declared stack bytes. A separate sorted-symbol transfer
array adds 2,048 bytes. Thus the additional declared arrays are 18,432 bytes;
this is not a measured stack high-water mark. Heap workspace is unchanged, and
there is no allocation in construction. The stored-origin version uses O(nL)
space rather than the paper's more elaborate O(n) space variant. With at most
256 symbols and L=12, all extents are fixed. Each list's total weight is at most
its level count times the original total, within uint32_t for admitted frames.

Optimality concerns weighted payload bits under the depth limit. It does not
minimize serialized table size or independently rounded stream sizes. Of 2,953
retained Huffman packets, 1,963 shrink, 898 are unchanged and 92 grow by 1–16
bytes. The complete image results below include these local regressions. No
portfolio of coding models or extra format option was introduced.

## Complete image measurement

Ratios are candidate divided by released CNG with Zstandard 1.5.7 level 3; lower
is better. The existing whole-image probe runs seven alternating paired trials
after warm-up, includes codec allocation and discarded compression attempts,
and excludes file I/O and caller pixel allocation. Results sum per-image medians.
Execution is Apple A18 Pro; compilation is non-Apple LLVM 23.1.2 on M4, C++20
Release, using system zlib 1.2.12 for CRC.

| Corpus | Size ratio | Encode time | Decode time | Entropy encode time |
|---|---:|---:|---:|---:|
| General, 131 images | 0.992910 | 0.918203 | 1.024717 | 0.692041 |
| RGBA, 80 images | 0.990180 | 0.908330 | 1.028920 | 0.678499 |
| Composite bodies, 11 images | 1.004546 | 0.956144 | 1.031499 | 0.953302 |

General files total 97,701,434 bytes versus 98,399,047 released bytes. RGBA totals
98,811,536 versus 99,791,531. Composite bodies total 744,697 versus 741,327.
Smaller/equal/larger counts against the release remain 50/0/81, 51/0/29 and
3/2/6 respectively. Corpus totals do not meet the requirement that regressions
be rare. Collection-wrapper integration is still outside this experiment.

Smooth remains 13,822 bytes versus 11,712 released bytes; rectangle-on-gradient
remains 464 versus 383. The improvement therefore does not resolve the small
structured-image gap. Decoder algorithms are unchanged, but changed code lengths
and packets can affect their work; aggregate decode parity is not established.

## Direct comparison with the preceding candidate

The old combined implementation and this candidate are linked under distinct
names in one process. Seven selected images each receive five process runs of
seven alternating paired trials. The runner verifies exact pixels and the new
corpus file hashes, while the old encoder reproduces its frozen input file.
All samples are preserved in `evidence/direct.json.gz`. The comparison is against
the preceding in-house candidate, not Zstandard; small differences and inconsistent
directions must not be treated as universal improvements.

| Case | Median complete encode ratio | Median entropy encode ratio |
|---|---:|---:|
| Smooth | 0.999269 | 0.967719 |
| Random | 1.003579 | 0.998130 |
| Mixed order/noise | 1.007535 | 1.010746 |
| kodim01 | 0.994103 | 0.994713 |
| Rectangle on gradient | 1.006620 | 1.011581 |
| Large smooth RGBA | 0.972720 | 0.978741 |
| Composite control states | 0.995474 | 1.006632 |

These are medians of five within-process ratios, not confidence intervals.
`kodim01` and large smooth RGBA improve in four of five processes; the other
cases are mixed. Even unchanged random packets show decoder timing differences
between separately compiled implementations. Code layout and memory state are
not isolated, so such differences do not establish a decoder optimization.

## Verification and reproduction

An independent dynamic program enumerates the possible code lengths on Kraft
units for alphabets of 2–6 symbols, every weight vector with entries 1–4, and
each feasible depth limit through four: 11,264 optimum-cost comparisons pass.
All 2,953 retained packets reproduce their old bytes with the baseline and
round-trip through the unchanged decoder after new encoding. The match suite
passes 432 boundary/distribution cases, including maximum admitted frames;
414 retained vectors also pass the independent scalar inverse. All 222 images
pass both complete decoder interfaces and exact source hashes.

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/length_limited -B .build/length-limited -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/length-limited -j4
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/length_limited/build_comparison.sh
```

Compile `check.cpp` separately using the same compiler and flags
`-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1
-DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Iexperiments/entropy`.
Copy binaries from the selected M4 mirror for local execution, then run sequentially:

```sh
.build/length-limited/check .build/parser-review/corpus/packets.bin .build/length-limited/new-packets.csv
.build/length-limited/bounds .build/length-limited/new-vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/length-limited/new-vectors.bin
python3 -B experiments/length_limited/run.py --corpus /Users/ultimussecundai/cng --binary .build/length-limited/cng_whole_probe --output .build/length-limited/new-corpus
python3 -B experiments/length_limited/compare.py --output .build/length-limited/new-direct
python3 -B experiments/length_limited/verify.py
python3 -B tools/check_evidence.py
```

The direct runner checks against the retained `.build/length-limited/corpus`
results. Both runners refuse output-directory overwrites. The direct build
script was added after the full corpus run; only the later direct report includes
its identity. Sources and checks are pinned in the evidence. No automated memory
integrity review was initiated. Public sources, release signature and dependencies
are unchanged; this is a private CNE candidate, not a shipped format revision.
