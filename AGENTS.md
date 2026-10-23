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
`experiments/entropy/FOURTH_ROUND.md` for the current candidate, preserved
negative results, exactness commands and the next algorithmic requirement.
The study is not complete: small structured cases still lose severely and no
whole-image replacement has passed the original goal. Never substitute corpus
totals or entropy-only timings for that acceptance.
