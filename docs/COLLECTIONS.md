# CNG collections

A collection stores 1 to 4,096 images in one file, in one of two forms the caller chooses:

| | Packed | Composite |
| --- | --- | --- |
| Payload | One complete CNG file per member | One complete CNG file of a canvas holding every member |
| Coding across members | None | Within the canvas's segments |
| Decoding | Each member on its own | The whole canvas, then each member's rectangle |
| Suited to | Independent images | Related glyphs, controls and artwork |

There is no automatic choice between the forms and no shared dictionary.

## Composite layout

Members are stacked vertically in directory order at x = 0. The canvas width is the largest member width and its
height the sum of the member heights; pixels to the right of a narrower member are 0. No member is resized. The
canvas is RGB when every member is RGB, otherwise RGBA, and RGB members then take alpha 255. Extraction restores
each member's own channel count and every sample, including the colour beneath alpha 0.

## Bounds

Every member, and a composite canvas, is at most 65,536 pixels on a side; with its dimensions rounded up to
multiples of 128, at most 4,194,304 pixels and a width plus height of at most 65,536. A canvas beyond these bounds
is rejected, not switched to the packed form. A packed collection may hold more pixels in total than one canvas.

## File layout

Integers are big-endian. The file is the signature followed by chunks. A chunk is its body length (u32), a
four-byte ASCII tag, the body, and the CRC32 (as in [FORMAT.md](FORMAT.md)) of the tag and body (u32).

| Part | Contents |
| --- | --- |
| Signature | `8A 43 4E 47 0D 0A 1A 0A` (distinct from an image's `89 43 4E 47 …`) |
| `SHDR` chunk | 24 bytes: schema version u32 (1), form u32 (0 packed, 1 composite), member count u32, canvas width u32, canvas height u32, canvas channels u32 (all 0 for packed) |
| `DIRE` chunk | 40 bytes per member: id u32, width u32, height u32, channels u32, x u32, y u32, file offset of its `IMAG` chunk u64, length of that chunk including its 12 bytes of framing u64 |
| `IMAG` chunks | Packed: one per member, in directory order, each body a complete CNG file. Composite: one, the canvas's CNG file, which every directory entry names |
| `SEND` chunk | Empty; the file ends with it |

A reader requires: the signature; schema version 1; a member count of 1 to 4,096; unique ids; members within the
bounds; for packed, every rectangle at (0, 0) and the `IMAG` chunks back to back after the directory; for composite,
rectangles at x = 0 stacked from y = 0 that cover the canvas height exactly, no wider and with no more channels than
the canvas, all naming one `IMAG` chunk; every chunk's CRC32; and the `SEND` chunk as the last 12 bytes of the file.
Each member file is then read as a CNG image, and its dimensions must match the directory.

## Access

Collection output must be seekable and start at offset zero, so that the encoder can complete the chunk lengths,
checksums and directory. The reader also seeks: it validates the directory and the extents before decoding any
member. Source images are headerless sample files of exactly their stated size and must not change while a
collection is written. See [API.md](API.md#collections) for the calls.
