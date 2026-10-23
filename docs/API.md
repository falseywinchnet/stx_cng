# Library interfaces

The public interfaces use standard C++20 streams, spans and owned containers.
Calls implement CSP v3 and are synchronous. Version 2 requires explicit migration;
see [compatibility](FORMAT.md#4-compatibility-boundary). Arguments are borrowed only for the duration of the call;
the library retains no caller buffers or streams afterward.

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

For example, with distinct, already-open binary streams:

```cpp
stx_cng::Measurements encoding{};
stx_cng::encode(raw_input, encoded_output, dimensions, encoding);

stx_cng::Measurements decoding{};
stx_cng::Dimensions restored = stx_cng::decode(encoded_input, raw_output, decoding);
```

The input must end where the operation expects it to end; trailing raw samples or
trailing encoded bytes are rejected. Standalone operations do not require
seekable streams. Internal source storage is band-sized rather than image-sized.

`decode_into` requires exactly the decoded sample count and storage disjoint from
all bytes backing its input stream. The caller is responsible for obtaining the
expected dimensions and allocating that storage. This release has no public
header-probe function. The stream decoder is available when dimensions are not
known ahead of time.

## Failure and ownership

Expected validation or stream failures are reported as exceptions. Allocation
failure may also propagate. The boundary contract is deliberately explicit:

- Stream operations may leave a partial output after failure.
- `decode_into` leaves output intact on header or output-extent failure; later
  failures may leave partial pixels.
- `Measurements` is reset at entry; a failed call does not provide a complete
  measurement record.
- Input and output must not alias. Separate operations may run concurrently when
  their streams and mutable storage are separate.

For file replacement, write to a temporary destination and commit it only after
successful completion and a successful flush/close. An exception is not an
implicitly rolled-back file transaction.

## Measurements

`Measurements` reports `workspace_bytes`, stored/compressed band counts, and
selected internal phase durations in seconds. Workspace includes owned array
capacities and entropy-context storage. It excludes caller storage, process
mappings, stack and allocator overhead. It is not process RSS.

Phase fields describe different internal portions of the call. Some aggregates
overlap with detailed fields; do not add every timing field together to obtain
wall-clock time. Measure the complete API call externally for throughput claims.

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
