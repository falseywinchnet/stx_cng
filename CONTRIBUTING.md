# Contributing

Read the [format reference](docs/FORMAT.md), [API contracts](docs/API.md) and
[programming house style](docs/PROGRAMMING_HOUSE_STYLE.md) before changing code.

Preserve exact samples, hidden RGB, explicit ownership and the two collection
forms. A codec change needs a concrete input, a measured reason and evidence that
it improves the complete operation. Retain counterexamples. Kernel timings alone
do not establish a complete encode/decode gain, and lower scratch capacity does
not establish lower runtime.

Build and run the ordinary tests with the commands in [Building](docs/BUILDING.md).
Record compiler, library versions, workload, timing method and memory-accounting
scope with any performance claim. Treat format or ABI changes as explicit design
changes; do not silently reuse an existing schema version for incompatible bytes.

Keep the public interface free of platform-specific types. Prefer operations the
compiler can understand over platform-specific intrinsics, and make allocation,
overlap and lifetime contracts visible. Do not add image-category selectors to
hide regressions in one fixed algorithm.

Changes are submitted under the repository's MIT license. Do not add third-party
images, papers or source without their applicable redistribution permissions.
