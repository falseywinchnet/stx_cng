# Third-party notices

The source and documentation in this repository are licensed under MIT ([LICENSE](LICENSE)), except where an
explicit notice states otherwise.

| Component | Use | Terms |
| --- | --- | --- |
| Zstandard 1.5.7 | Compression of each image's tables. The official release tarball is checked in at `third_party/zstd-1.5.7.tar.gz` (SHA-256 `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`); its library sources are compiled into `libstx_cng` | BSD-3-Clause, the upstream licensing option this project uses; the licence is in [LICENSE-zstd.txt](LICENSE-zstd.txt) and is installed with the library |

Redistributions of `libstx_cng` in binary form contain Zstandard and must carry its licence.

- [Zstandard repository and licence](https://github.com/facebook/zstd/blob/v1.5.7/LICENSE)

The measurements also used libpng and libwebp (through Pillow and their C libraries), OptiPNG 7.9.1 and externally
sourced images. None of them is needed to build the library or run its tests, and no corpus image is included.

The `stx_cngcompress` package in this repository is first-party MIT code with its own LICENSE; releases 0.1 and 0.2
used it, and this release does not build or link it.
