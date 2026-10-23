# cngcompress frame v1

This specification describes the bytes accepted by the public v0.1 decoder and
emitted by its encoder. Integers are unsigned little-endian unless stated
otherwise. Byte counts and output extents are distinct from pixel counts: this
library operates only on bytes.

## Frame header

A frame begins with a one-byte mode and a four-byte uncompressed size `N`.
`0 <= N <= 2097160`. Mode 0 stores exactly N following bytes. Mode 16 contains
block records and requires N > 0. Other modes are invalid. Empty input is the
five-byte all-zero stored frame. The container supplies the complete frame
extent; concatenated frames are not implicitly parsed by this interface.

The encoder returns at most N+5 bytes, selecting stored framing when block
framing would be no smaller. Its output-capacity query includes temporary block
framing: `N + 4*ceil(N/131072) + 5` bytes. The caller supplies that capacity even
though the final result can be smaller. The decoder does not require a payload
to be the smallest possible representation.

## Blocks

The implied block output size is `min(131072, remaining frame output)`. Each
block starts with a four-byte word. Its low 30 bits give the following payload
size. The top two bits select:

| Bits 31..30 | Payload |
|---|---|
| 10 | Raw bytes, exactly the implied block output size |
| 01 | One Huffman packet decoding directly to the block output |
| 00 | Four-byte parsed-byte count, then a Huffman packet of those bytes |
| 11 | Invalid |

A parsed stream is at most block-output-size+3 bytes. Blocks exactly consume the
frame input and produce exactly N output bytes. Literal-only blocks need no
match reconstruction. The match history can cross block boundaries within the
frame, but resets between frames.

## Parsed stream

Each event begins with a literal count followed by that many literal bytes. If
those literals finish the block output, the stream ends. Otherwise, a positive
match distance and a match-length-minus-eight follow. All three integers use
canonical unsigned LEB128, at most four bytes (28 payload bits). Overlong forms
and a continuation bit on the fourth byte are invalid.

A match has length at least eight. Distance must not exceed the already decoded
frame output; length must not exceed the remaining block output. Matches may
overlap: the bytes are copied as if proceeding one byte at a time, so a distance
one match repeats its preceding byte. The implementation can seed a period then
double already reconstructed material. A block can end immediately after a match;
there is no required empty terminal literal event. Parsed bytes must be consumed
exactly.

## Huffman packet

A Huffman packet begins with a mode byte and four-byte decoded symbol count.
Mode 0 stores exactly that many bytes. Mode 4 requires a positive count and one
following byte, repeated to produce the output. Mode 36 contains a compact
length table and coded streams. Other modes are invalid. The containing block
supplies the packet's total encoded extent and exact decoded size.

The alphabet consists of byte symbols 0..255. Absent symbols have length zero;
present symbols have lengths 1..12. The nonzero lengths form a complete prefix
tree (Kraft sum exactly one). Canonical codes use increasing length, then
increasing symbol order. Bits are emitted least-significant first after reversing
the conventional canonical code within its declared length.

### Compact length table

Lengths are serialized in symbol order, low nibble first in each byte. The
previous length starts at zero. Nibbles 0..12 emit a literal length and update
the previous length. Nibble 13 is reserved and invalid. Nibble 14 consumes one
more nibble `x` and repeats the previous length `3+x` times. Nibble 15 consumes
two more nibbles `lo, hi` and repeats it `19+lo+16*hi` times. A run cannot pass
symbol 255. The table ends after exactly 256 lengths and occupies at most 128
bytes. If it ends after a low nibble, the unused high nibble must be zero.

### Coded streams

A symbol count at most 1,024 uses one stream. A larger count uses four contiguous
streams. Each of the first three decodes `floor(count/4)` symbols; the last
decodes the remainder. After the length table, three four-byte encoded lengths
specify the first three streams. The final stream uses the remaining packet
bytes. A one-stream packet has no stream-length array.

Each stream independently ends on a byte boundary. Unused high bits in the final
byte must be zero, and the number of consumed bytes must match its extent. The
short-table and grouped four-stream decoder routines implement this same grammar;
they are not extra wire modes.

## Integrity and API validation

Frames do not contain a checksum or authentication tag. Structural validation
checks headers, lengths, prefix trees, extents, padding, distances and exact
consumption. A byte change can still describe a different valid frame; callers
requiring content integrity provide it in the containing protocol.

`decoded_size` validates the header and stored-frame extent, not coded payloads.
It returns a size only within the admitted bound. `decompress` performs full
structural decoding and can leave invalid partial output if it returns an error.
Argument, buffer-capacity and header errors are rejected before output mutation.
Public statuses and resource ownership are specified in the installed header.

Frame-v1 output is deterministic for this implementation. Future encoder changes
may emit different valid v1 bytes while retaining this decoding grammar; any
incompatible grammar requires a new version/mode and corresponding documentation.
