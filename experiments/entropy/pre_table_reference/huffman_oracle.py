"""Independent bit-by-bit canonical Huffman inverse for the paired decoder."""
import struct
import sys
from pathlib import Path


def inverse(packet):
    mode, size = struct.unpack_from('<BI', packet)
    if mode == 0:
        assert len(packet) == size + 5
        return packet[5:]
    if mode == 4:
        assert len(packet) == 6 and size
        return bytes([packet[5]]) * size
    assert mode == 7
    lengths = []
    for value in packet[5:133]:
        lengths.append(value & 15)
        lengths.append(value >> 4)
    counts = [0] * 13
    for length in lengths:
        if length:
            counts[length] += 1
    next_code = [0] * 13
    code = 0
    for length in range(1, 13):
        code = (code + counts[length - 1]) * 2
        next_code[length] = code
    lookup = {}
    for symbol, length in enumerate(lengths):
        if length:
            lookup[(length, next_code[length])] = symbol
            next_code[length] += 1
    stream_sizes = struct.unpack_from('<4I', packet, 133)
    position = 149
    output = bytearray()
    for lane, stream_size in enumerate(stream_sizes):
        stream = packet[position:position + stream_size]
        position += stream_size
        count = size // 4
        if lane == 3:
            count += size % 4
        bit = 0
        for index in range(count):
            code = 0
            for length in range(1, 13):
                code = code * 2 + ((stream[bit // 8] >> (bit % 8)) & 1)
                bit += 1
                if (length, code) in lookup:
                    output.append(lookup[(length, code)])
                    break
            else:
                raise AssertionError('unrecognized code')
        assert (bit + 7) // 8 + 8 == stream_size
        for padding in range(bit, 8 * stream_size):
            assert ((stream[padding // 8] >> (padding % 8)) & 1) == 0
    assert position == len(packet) and len(output) == size
    return bytes(output)


def run():
    vectors = Path(sys.argv[1]).read_bytes()
    position = 0
    cases = 0
    while position < len(vectors):
        size, coded_size = struct.unpack_from('<II', vectors, position)
        position += 8
        raw = vectors[position:position + size]
        position += size
        packet = vectors[position:position + coded_size]
        position += coded_size
        assert inverse(packet) == raw
        cases += 1
    assert cases == 96 and position == len(vectors)
    print('PASS:', cases, 'independent Huffman inverses')


if __name__ == '__main__':
    run()
