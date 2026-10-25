# Library interfaces

The public interfaces use standard C++20 streams, spans and owned containers.
Calls write and read `CNGPHI08` streams and are synchronous. Arguments are
borrowed only for the duration of the call; the library retains no caller
buffers or streams afterward.

## Standalone images

Include `stx_cng/codec.hpp`. `stx_cng::Dimensions` names its fields in this order:

```cpp
stx_cng::Dimensions dimensions{
    .height = 480,
    .width = 640,
    .channels = 4
};
```

Samples are interleaved RGB or RGBA bytes in row-major order, with no row padding.
Alpha is neither applied nor discarded. The codec preserves bytes; it performs no
colour-profile conversion or premultiplication.

| Operation | Input | Output |
| --- | --- | --- |
| `encode` | Exactly `height * width * channels` raw bytes and dimensions | One complete standalone file |
| `decode` | One complete standalone file | Raw interleaved samples and returned dimensions |
| `decode_into` | One complete standalone file and an exactly sized destination span | Samples in the supplied storage and returned dimensions |
| `decode_mosaic` | One complete standalone file | The mosaic layer's samples (`mosaic_dimensions` of the image) and the image dimensions |
| `decode_salvage` | One complete standalone file and an exactly sized destination span | Every intact frame exactly, lost frames filled from the mosaic, a per-frame report |

For example, with distinct, already-open binary streams:

```cpp
stx_cng::Measurements encoding{};
stx_cng::encode(raw_input, encoded_output, dimensions, encoding);

stx_cng::Measurements decoding{};
stx_cng::Dimensions restored = stx_cng::decode(encoded_input, raw_output, decoding);
```

`encode` reads exactly the stated sample count and fails on a short input.
`decode` and `decode_into` read the input stream to its end; the whole file is
the unit of decoding, and its trailing index copy is part of the file.
Standalone operations do not require seekable streams.

`decode_into` requires exactly the decoded sample count and storage disjoint from
all bytes backing its input stream. The caller is responsible for obtaining the
expected dimensions and allocating that storage. This release has no public
header-probe function; the stream decoder serves the case where dimensions are
not known ahead of time.

`decode_mosaic` writes the mean colour of every 16×16 cell (`mosaic_cell`) as
an interleaved image of `mosaic_dimensions(image)`, decoded from whichever
stored copy is intact. `decode_salvage` is the decoder for a file that may be
damaged: it decodes every frame whose checksum passes, fills the cells of the
others with their mosaic colour (mid grey when both mosaic copies are lost),
sets one byte per frame in `frames_ok` and `mosaic_ok`, and throws only when
neither copy of the header or of the directory can be read.

## Failure and ownership

Validation and stream failures are reported as `std::runtime_error`. Allocation
failure may also propagate. The messages name the boundary that failed: the
header (not a `CNGPHI08` stream, or both header copies damaged), the frame
directory (both copies damaged, or lengths that do not account for the file),
a frame checksum, or an exhausted stream.

- Stream operations may leave a partial output after failure.
- `decode_into` leaves its output intact on header, directory or output-extent
  failure. A frame checksum failure is detected before that frame is
  reconstructed, so frames earlier in raster order are complete and the failed
  frame and later frames are untouched.
- `Measurements` is reset at entry; a failed call does not provide a complete
  measurement record.
- Input and output must not alias. Separate operations may run concurrently when
  their streams and mutable storage are separate.

For file replacement, write to a temporary destination and commit it only after
successful completion and a successful flush/close. An exception is not an
implicitly rolled-back file transaction.

## Measurements

`Measurements` reports `workspace_bytes`, `frames` and `codec_seconds`.
`workspace_bytes` is the storage the call owned: the sample buffer (for `encode`
and `decode`), the complete compressed stream, and the frame engine's state for
one frame, which is at most a few hundred kilobytes. It excludes caller
storage, process mappings, stack and allocator overhead; it is not process RSS.
`frames` is the number of frames of the image. `codec_seconds` is the time
spent inside the frame engine, excluding stream reads and writes; measure the
complete call externally for throughput claims.

## Collections

Include `stx_cng/collection.hpp`; the namespace is `stx_cng_collection`.
`ImageSource` associates a unique `ImageId`, dimensions and a path to headerless
raw samples. Paths and file contents must remain stable throughout encoding.

```cpp
stx_cng_collection::Measurements measured{};
stx_cng_collection::encode(images, stx_cng_collection::Form::composite,
                           collection_output, measured);

stx_cng_collection::Decoded decoded{};
stx_cng_collection::decode(collection_input, decoded, measured);
stx_cng_collection::write_image(decoded, requested_id, member_output);
```

Both collection input and output must be seekable; output starts at byte zero.
The encoder validates source extents and identities, then writes a directory and
image chunks. The decoder owns the complete result. A failure may leave that
result partial and invalid. `write_image` extracts bytes from an already decoded
collection; it does not perform a separate member decode.

See [collections](COLLECTIONS.md) for layout, channel promotion, memory ownership
and the distinction between the two forms.
