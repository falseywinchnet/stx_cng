# One image, or a set

CNG supports two collection forms. Their difference is where encoding happens:
on each member separately, or on one canvas containing the whole set.

| | Packed images | Composite image |
| --- | --- | --- |
| Encoded payload | One complete standalone file per member | One complete standalone file for the canvas |
| Shared image coding | None across members | Within each 128×128 frame of the canvas |
| Decoder ownership | One decoded plane per member | One decoded canvas |
| Extraction | Copy the selected decoded plane | Copy its rectangle, restoring original channels |
| Typical reason to choose it | Preserve each image as its own encoding | Encode related controls, glyphs or artwork together |

The caller selects the form explicitly. There is no automatic competition between
forms and no separately loaded shared dictionary. The API decodes the complete
collection before extraction; it does not promise independently decoded members.

## Composite layout

Members appear in manifest order, stacked vertically at x = 0. Canvas width is
the largest member width; canvas height is the sum of member heights. Pixels to
the right of a narrower member are zero. No member is resized or resampled.

An all-RGB set stays RGB. If any member has alpha, the canvas is RGBA and RGB
members receive alpha 255. Extraction restores each member's original channel
count. Existing RGBA samples, including RGB below alpha zero, survive exactly.

The encoder synthesizes the canvas one row at a time, keeping one source file
open, and then encodes the complete canvas. The decoder owns the complete
canvas, as required for subsequent slicing. Arbitrary rectangle packing is
outside this release.

## Bounds and file access

A set contains 1–4,096 uniquely identified images. Members and the composite
canvas are limited to 65,536 on a side and, with dimensions rounded up to
multiples of 128, to 4,194,304 pixels and a width-plus-height of 65,536. A
canvas that is too large is rejected rather than silently switched to packed
form. Packed collections may exceed one canvas's total pixel count because each
member is encoded separately.

Source paths refer to headerless interleaved sample files with exactly the stated
extent. They must stay unchanged while encoding. Collection output is seekable
and begins at zero so the encoder can finalize chunk lengths, checksums and the
directory. Collection input is also seekable: the reader validates its directory
and file extents before decoding image bodies.

For exact layout fields, signatures and checksums, see the
[format reference](FORMAT.md#3-collections). For C++ ownership and failure behavior,
see the [API reference](API.md#collections).
