# Release evidence

The evidence for release 0.3.0 (the `CNGPHI07` frame codec) is in
[`experiments/version7/evidence`](../experiments/version7/evidence): bit-exact
vectors, corpus exactness and cross-platform hashes, OptiPNG reference sizes,
frame-side and frame-parallel measurements and the malformed-input sweep. The
files below are the evidence of releases 0.1 and 0.2.

`verification.json` records source hashes, the 222 frozen image identities,
complete-file sizes, exactness results and fresh-process resource measurements.
`literal-fusion.json` retains both 31-trial paired decoder experiments and their
aggregate ratios. See [Measurements](../docs/MEASUREMENTS.md) for the protocols
and their limits.

These are curated exports from the retained research reports. Each export names
and hashes its original report. Local paths, process listings and unrelated
machine state are omitted. Corpus images and the historical research workspace
are not part of this repository. Report hashes identify the original evidence;
they do not make an unavailable corpus independently reproducible.

`ORIGIN.json` at the repository root maps the consolidated implementation to its
historical source files. Those paths describe provenance in the research tree,
not dependencies to be resolved by this build. `prepared_sha256` identifies each
consolidated source file. Publication edits change presentation and licensing;
the checked C++ implementation remains unchanged.
