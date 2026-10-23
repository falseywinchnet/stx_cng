# Exact reductions in Huffman encoder work

Carry the combined construction forward for further whole-image experiments.
It removes work without changing any of the preceding candidate's 222 emitted
files. It does not resolve the remaining size regressions or decoder performance
gap, so it is not a completed Zstandard replacement.

## Two independent changes

`huffman_reduced.cpp` copies the previous `huffman_tail_split.cpp`; changes are
confined to the encoder's frequency counting and an exact early return. The
decoder, alphabet, tree construction for remaining cases, wire format, match
search and image reconstruction are unchanged. Compile-time variants isolate
the two changes; they are research comparisons, not user-selectable image modes.

**Uniform-depth proof.** After counting the complete input, if all 256 symbols
occur and the largest population is at most twice the smallest, the existing
Huffman tree has depth eight for every symbol. At the leaf level, the smallest
new pair weighs at least as much as the largest existing leaf. The implementation
prefers an existing leaf at equality. Thus it consumes all leaves before their
parents. Pair sums preserve the same maximum/minimum bound; later levels consume
older queue entries first at equality. Since 256 is a power of two, every level
pairs completely, yielding eight equal-depth levels. No frequency smoothing is
needed. A coded body would take the original number of bytes plus headers, so
the old encoder necessarily chooses raw storage.

The new path returns those same raw bytes immediately, skipping sorting, tree
construction, parent walks, canonical code construction and table serialization.
It still counts every byte. This is a sufficient condition for this particular
Huffman construction, not a claim that the input has no LZ matches or is
incompressible under every representation. It runs after the existing matcher.

**Split histogram.** Four independent 256-entry population arrays count four
interleaved positions. Their counts are combined once, then the existing coding
logic operates on the identical histogram. Repeated symbols no longer require
every increment to wait on the same counter. This adds 3,072 bytes of declared
temporary array storage and a fixed merge/initialization cost. It adds no owned
image or frame buffer. Compiled peak stack use is not established by this source
array count. The histogram preserves exact integer counts, not an approximation.

## Complete image results

Ratios divide candidate time by released CNG using Zstandard 1.5.7 level 3;
lower is better. These are complete operations, including codec allocations and
discarded compression attempts. Each image has seven alternating paired trials
after warm-up; totals sum the per-image medians. Build: M4, non-Apple LLVM 23.1.2,
C++20 Release. Execution: Apple A18 Pro. See the preceding
[`whole_codec` experiment](../whole_codec/README.md) for exact timing boundaries,
corpus composition and memory limitations.

| Encoder construction | General encode | RGBA encode | Composite-body encode |
|---|---:|---:|---:|
| Previous candidate | 0.955245 | 0.937221 | 1.021832 |
| Exact uniform-depth return | 0.949710 | 0.939980 | 1.082704 |
| Split histogram | 0.946011 | 0.923298 | 1.003076 |
| Both changes | **0.932535** | **0.916193** | 1.026081 |

For the combined construction, the corresponding entropy-encode ratios are
0.742745, 0.715856 and 1.030536. Its complete decode ratios are 1.024169, 1.026104
and 1.033020. There is no decoder algorithm change: timing differences between
variants are not evidence of a decoding improvement. These individual runs do
not provide confidence intervals for differences between encoder variants.

All three runs emit byte-identical candidate files to the preceding complete
corpus run: 131 general, 80 RGBA and 11 composite bodies. Consequently, all size
ratios and all previous size regressions remain. Every image reconstructs exact
pixels through both decode interfaces, and reference encoding still reproduces
the frozen release file. Combined owned-capacity measurements are unchanged;
the additional histogram arrays are stack storage, excluded from those counters.

In the combined run, random-image complete encoding takes 1.279 ms versus
1.209 ms for the release, about 5.8% slower. Its entropy stage takes 0.102 ms
versus 0.043 ms. The preceding candidate's individual run measured 1.476 versus
1.344 ms complete, and 0.133 versus 0.048 ms in entropy. Random-input cost has
improved in these observations, but parity is not established. Composite
encoding and individual small cases remain variable; the isolated shortcut alone
does not win across all groups.

## Paired kernel comparison

`kernel_probe.cpp` links each candidate against the preceding Huffman encoder.
Both see the same bytes and preallocated buffers. Seven paired batches alternate
order, each containing 64 encodes. Allocation and output verification are outside
timing. This measures the entire Huffman encode, including counting, tree work,
bit writing and raw fallback; it is not the complete image codec or a per-stage
profile. Exact packet equality is required before timing.

| 128 KiB input | Uniform-depth return | Split histogram | Both changes |
|---|---:|---:|---:|
| Uniform pseudorandom bytes | 0.879 | 0.987 | 0.886 |
| One repeated byte | 0.997 | 0.138 | 0.138 |
| Seven-symbol pseudorandom alphabet | 0.925 | 0.795 | 0.698 |
| Zeros with a pseudorandom byte every 64 positions | 1.015 | 0.510 | 0.490 |

The counter dependency matters strongly on repeated symbols; the uniform-depth
test primarily helps full-alphabet data. Raw pseudorandom 1 KiB inputs regress
about 5–6% in the isolated single-change runs, while the combined run differs.
Retain these results rather than assuming every block benefits. Absolute times
also varied between runs; paired ratios are the relevant comparison within each
run. The source makes the removed tree work and independent counting explicit,
but these measurements do not establish a hardware-counter attribution.

## Verification and reproduction

Each variant passes 368 direct byte-for-byte comparisons with the preceding
Huffman encoder and exact inverses. Cases include 128 full-alphabet populations
within the proven bound, distributions at the exact 2:1 boundary, changes beyond
the sufficient condition, empty/short inputs, short-stream boundaries, 128 KiB
boundaries, the maximum 2,097,160-byte input and seven alphabet sizes. The 128
in-bound cases also inspect the old tree and require every depth to equal eight.
These finite checks supplement the argument above; they are not an exhaustive
enumeration of possible histograms.

`evidence/{uniform,histogram,combined}.json.gz` retains complete corpus records,
source hashes and all timing samples. `kernel_*.json` retains paired kernel
samples. `checks.json` identifies binaries, compile definitions and check counts.
Source maps are observed at corpus start; the uniform run began before the kernel
probe source was added. None of the sources recorded by these runs was changed
after measurement. Preserve them when extending the experiment.

From the repository root, serialize mirror operations:

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/entropy_reduction -B .build/entropy-reduction -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/entropy-reduction -j 4
```

The direct kernel probe was compiled separately against each candidate archive:

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh -c 'set -e; for trial_variant in uniform histogram combined; do /opt/homebrew/opt/llvm/bin/clang++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -Iexperiments/entropy experiments/entropy_reduction/kernel_probe.cpp .build/entropy-reduction/libcandidate_${trial_variant}.a -o .build/entropy-reduction/cng_kernel_${trial_variant}; done'
```

After copying the binaries locally, run each proof executable and each corpus
sequentially. The corpus runner requires the previous complete-image baseline at
`.build/whole-codec/first-corpus/results.json` and refuses output overwrites:

```sh
.build/entropy-reduction/cng_proof_combined
python3 -B experiments/entropy_reduction/run.py --variant combined --binary .build/entropy-reduction/cng_reduced_combined --corpus /path/to/cng-research --output .build/entropy-reduction/new-combined
.build/entropy-reduction/cng_kernel_combined
python3 -B experiments/entropy_reduction/verify.py --artifacts .build/entropy-reduction
python3 -B experiments/whole_codec/verify.py
python3 -B tools/check_evidence.py
```

No automated memory integrity review was initiated. Public release sources and
the published CNG format remain unchanged. The remaining work includes reducing
the cost of failed attempts further, improving mixed structure/noise decoding,
and fixing the still-frequent size losses. Fewer tree operations alone do not
complete that objective.
