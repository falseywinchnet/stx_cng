# stx_cngcompress

This package builds independently of CNG. No source, header or runtime dependency
outside this directory is admitted by its build. The public header is a C ABI;
the implementation requires C++20. Follow docs/PROGRAMMING_HOUSE_STYLE.md.

The selected matcher, interleaved LEB128 grammar, exact length-limited Huffman
construction, compact length table and grouped decoder come from the measured
CNG study. Do not silently change frame bytes or introduce codec options.
Document any later format revision. Preserve caller/handle disjointness and
partial-output failure contracts. All C++ exceptions must remain behind the C
boundary. Successful operations reuse handle storage without allocation.

Ordinary local build:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure

Use the owner's m4host/m4build route for heavy builds. While staged inside the
CNG repository, run m4build from that repository with `-S stx_cngcompress` and
`.build/cngcompress-package`. The package itself does not consume the parent.
The installed C consumer is tests/installed; verify it after relocating a prefix.
No dependency installation is implied. Preserve experimental evidence outside
this staged package until export is complete.

Do not initiate an automated memory integrity review without the owner's request.
Ordinary round trips and status tests do not establish such review coverage.
