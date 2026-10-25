# Third-party dependencies

The source and documentation in this repository are licensed under MIT, except
where an explicit notice states otherwise. No third-party source code, corpus
images or external manuscripts are bundled.

| Dependency | Use | Upstream terms |
| --- | --- | --- |
| zlib | CRC32 of collection chunks | zlib license |
| Zstandard 1.5.7 | Frozen release 0.1 sources in `experiments/release_v2` only; not a dependency of the library | BSD-3-Clause (the selected upstream licensing option) |

Upstream distributions carry their own notices. Linking a dependency does not
replace those terms with this repository's MIT license. Preserve applicable
notices when redistributing dependency binaries or source.

- [Zstandard repository and license](https://github.com/facebook/zstd/blob/v1.5.7/LICENSE)
- [zlib repository and license](https://github.com/madler/zlib/blob/v1.3.1/LICENSE)

The measurements additionally used libpng (through Pillow), OptiPNG 7.9.1 and externally sourced images.
Neither is needed to build the library or run its bundled generated-fixture test.
The published evidence contains measurements and image hashes, not those images.

The stx_cngcompress package in this repository is first-party MIT code with its own LICENSE; releases 0.1 and 0.2 used it, and release 0.3 does not build or link it.
