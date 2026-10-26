# stx_cng library interface

stx_cng is the reference implementation of CNG 1.0 in portable C++20. Include `stx_cng/codec.hpp` for images and
`stx_cng/collection.hpp` for collections, and link `stx_cng::stx_cng`.

Every call is synchronous, borrows its arguments only for the duration of the call, and retains nothing afterwards.
Failures throw `stx_cng::Error`, a `std::runtime_error` whose `reason()` names the cause.

## Samples

`stx_cng::Dimensions` names its fields in this order:

```cpp
stx_cng::Dimensions dimensions{.height = 480, .width = 640, .channels = 4};
```

Samples are interleaved RGB8 (3 channels) or RGBA8 (4 channels) bytes, rows top to bottom without padding:
`height × width × channels` bytes. The codec preserves bytes exactly. It performs no colour conversion and no
premultiplication, and keeps the colour of pixels whose alpha is 0. Width and height are at least 1 and the sample
count is at most 2³¹ (`stx_cng::maximum_samples`).

## Images

| Call | Result |
| --- | --- |
| `encode(samples, dimensions, options)` | One complete file, as a `std::vector<std::uint8_t>` |
| `encode(sample_stream, file_stream, dimensions, options)` | Reads exactly the sample count, writes one file |
| `read_dimensions(file)` | The dimensions, after checking the header and the directory; decodes nothing |
| `decode(file, dimensions, options)` | The samples, as a `std::vector<std::uint8_t>`; sets `dimensions` |
| `decode_into(file, samples, options)` | Decodes into caller storage of exactly the sample count; returns the dimensions |
| `decode(file_stream, sample_stream, options)` | Reads the file to the end of the stream, writes the samples |
| `decode_mosaic(file, mosaic)` | The mosaic layer alone; sets `mosaic` to its dimensions |
| `decode_salvage(file, dimensions, report)` | Every intact segment exactly, the others from the mosaic, and a report |
| `mosaic_dimensions(image)`, `segment_count(image)` | ceil(width / 16) × ceil(height / 16); ceil(width / 128) × ceil(height / 32) |

```cpp
#include "stx_cng/codec.hpp"

std::vector<std::uint8_t> file = stx_cng::encode(samples, dimensions);

stx_cng::Dimensions decoded_dimensions;
std::vector<std::uint8_t> decoded = stx_cng::decode(file, decoded_dimensions);
```

`EncodeOptions::threads` and `DecodeOptions::threads` set the number of worker threads; 0 uses one per available
core. Segments are independent, so decoding divides across threads, and the encoder writes the same file for every
thread count.

`decode_into` requires storage of exactly the image's sample count that does not overlap the file; use
`read_dimensions` to size it. A decode that fails can leave the storage partially written.

## The mosaic and salvage

The mosaic is the mean colour of every 16 × 16 cell, decoded from whichever stored copy is intact. It is a small
part of the file and decodes first: a picture for a progressive display, or a thumbnail.

`decode_salvage` is the decoder for files that may be damaged. It decodes every segment whose checksum passes
exactly and fills the pixels of every other segment with the mosaic colour of their cell (mid grey when both mosaic
copies are lost). `SalvageReport` lists the result of every segment in raster order, the number lost and whether the
mosaic decoded. It tolerates a damaged signature and throws only when no copy of the header, the directory or the
tables can be read.

## Errors

| `stx_cng::Reason` | Cause |
| --- | --- |
| `dimensions` | Zero width or height, channels other than 3 or 4, or more than 2³¹ samples |
| `sample_count` | A sample buffer whose size does not match the dimensions |
| `not_cng` | Not a CNG file, or both copies of the header fields are damaged |
| `unsupported_version` | A format version this library does not read |
| `directory` | Both directory copies are damaged, or the payload extents do not match the file |
| `tables` | Both table copies are damaged or invalid |
| `damaged` | A segment or the mosaic failed its checksum or its decoding |
| `input` | A stream could not be read or written |
| `collection` | A collection's structure is invalid |

## Collections

`stx_cng/collection.hpp` stores 1 to 4,096 images in one file, in a form the caller chooses:

| | `Form::packed` | `Form::composite` |
| --- | --- | --- |
| Payload | One complete CNG file per member | One CNG file of a canvas holding every member |
| Decoding | Each member on its own | The canvas, then each member's rectangle |
| Suited to | Independent images | Related glyphs, controls and artwork |

`collection::encode(sources, form, destination)` reads each member's raw samples from its file and writes the
collection to a seekable stream at offset zero. `collection::decode(source, decoded)` decodes every member
(packed) or the canvas (composite), and `collection::write_image(decoded, id, destination)` writes one member's
samples with its original channels. The container is specified in [COLLECTIONS.md](COLLECTIONS.md).

## Threads and memory

The library starts threads only inside a call with more than one worker and joins them before returning. Memory
used by a call is proportional to the image: the encoder holds the samples, the planned symbol streams of every
segment and the file; the decoder holds the file and the samples, plus a small workspace per worker.
