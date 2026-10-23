# Third-party dependencies

The source and documentation in this repository are licensed under MIT, except
where an explicit notice states otherwise. No third-party source code, corpus
images or external manuscripts are bundled.

| Dependency | Use | Upstream terms |
| --- | --- | --- |
| Zstandard 1.5.7 | Frozen v2 comparison/migration only; not a v3 runtime dependency | BSD-3-Clause (the selected upstream licensing option) |
| zlib | CRC32 checksums | zlib license |

Upstream distributions carry their own notices. Linking a dependency does not
replace those terms with this repository's MIT license. Preserve applicable
notices when redistributing dependency binaries or source.

- [Zstandard repository and license](https://github.com/facebook/zstd/blob/v1.5.7/LICENSE)
- [zlib repository and license](https://github.com/madler/zlib/blob/v1.3.1/LICENSE)

The research measurements additionally used libpng and externally sourced images.
Neither is needed to build the library or run its bundled generated-fixture test.
The published evidence contains measurements and image hashes, not those images.

The bundled stx_cngcompress package is first-party MIT code with its own LICENSE.
No zxc implementation is bundled or linked.
