# Frozen CNG v2 reference

These files are copied unchanged from public release 0.1.0-rc1,
`a1797b840ad3a209241ad24790ff97cdf3cff9b1`. The original source hashes remain in
`evidence/verification.json`; `tools/check_evidence.py` now checks this snapshot.
The format description is retained as FORMAT.md. These are first-party MIT
sources, not a new third-party dependency.

The production v3 target does not build this directory. It remains available for
reading/converting old files and for comparisons. Its standalone CMake build
requires matching Zstandard 1.5.7 headers and library plus zlib. Old public C++
namespace names are preserved; do not link it beside the v3 library without a
separate symbol namespace. The integration probe applies a private namespace
rename to its reference target only, and links the public v3 library normally.

Historical experiment drivers and source-hash verifiers describe the source
layout at their recorded commits. Reproduce those studies from commit 5515d34
(or their individual earlier recorded commits), where production sources still
implement v2. Do not update old evidence hashes to make them appear to verify v3.
The new public integration has its own evidence and verifier.
