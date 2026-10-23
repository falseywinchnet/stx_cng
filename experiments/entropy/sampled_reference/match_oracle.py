"""Scalar inverse: bit-by-bit Huffman followed by byte-at-a-time references."""
import struct
import sys
from pathlib import Path
from huffman_oracle import inverse as huffman_inverse


def integer(stream, position):
    value = 0
    shift = 0
    while True:
        byte = stream[position]
        position += 1
        value += (byte & 127) * (2 ** shift)
        if byte < 128:
            return value, position
        shift += 7
        assert shift <= 14


def reconstruct(stream, count):
    output = bytearray()
    position = 0
    while len(output) < count:
        literals, position = integer(stream, position)
        output.extend(stream[position:position + literals])
        position += literals
        if len(output) == count:
            break
        distance = struct.unpack_from('<H', stream, position)[0]
        position += 2
        length, position = integer(stream, position)
        assert distance and distance <= len(output)
        for byte in range(length + 6):
            output.append(output[-distance])
    assert len(output) == count and position == len(stream)
    return bytes(output)


def inverse(packet):
    mode, size = struct.unpack_from('<BI', packet)
    if mode == 0:
        assert len(packet) == size + 5
        return packet[5:]
    assert mode == 10
    position = 5
    output = bytearray()
    while len(output) < size:
        header = struct.unpack_from('<I', packet, position)[0]
        position += 4
        coded = header & 0x7fffffff
        count = min(131072, size - len(output))
        if header & 0x80000000:
            assert coded == count
            output.extend(packet[position:position + coded])
        else:
            parsed = struct.unpack_from('<I', packet, position)[0]
            stream = huffman_inverse(packet[position + 4:position + coded])
            assert parsed == len(stream)
            output.extend(reconstruct(stream, count))
        position += coded
    assert position == len(packet) and len(output) == size
    return bytes(output)


def run():
    vectors = Path(sys.argv[1]).read_bytes()
    position = 0
    cases = 0
    while position < len(vectors):
        size, coded = struct.unpack_from('<II', vectors, position)
        position += 8
        raw = vectors[position:position + size]
        position += size
        packet = vectors[position:position + coded]
        position += coded
        assert inverse(packet) == raw
        cases += 1
    assert position == len(vectors) and cases == 414
    print('PASS:', cases, 'independent match inverses')


if __name__ == '__main__':
    run()
