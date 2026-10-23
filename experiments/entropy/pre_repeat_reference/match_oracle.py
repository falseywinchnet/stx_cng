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
        assert shift <= 21


def reconstruct(stream, count, minimum, source=None, history=None):
    literal_position = 0
    output = bytearray() if history is None else bytearray(history)
    start = len(output)
    position = 0
    while len(output) < start + count:
        literals, position = integer(stream, position)
        if source is None:
            output.extend(stream[position:position + literals])
            position += literals
        else:
            output.extend(source[literal_position:literal_position + literals])
            literal_position += literals
        if len(output) == start + count:
            break
        if history is None:
            distance = struct.unpack_from('<H', stream, position)[0]
            position += 2
        else:
            distance, position = integer(stream, position)
        length, position = integer(stream, position)
        assert distance and distance <= len(output)
        for byte in range(length + minimum):
            output.append(output[-distance])
    assert len(output) == start + count and position == len(stream)
    if source is not None:
        assert literal_position == len(source)
    return bytes(output[start:])


def inverse(packet):
    mode, size = struct.unpack_from('<BI', packet)
    if mode == 0:
        assert len(packet) == size + 5
        return packet[5:]
    assert mode in (10, 12, 13, 15, 16)
    minimum = 6 if mode == 10 else 8
    position = 5
    output = bytearray()
    while len(output) < size:
        header = struct.unpack_from('<I', packet, position)[0]
        position += 4
        coded = header & (0x3fffffff if mode in (13, 15, 16) else 0x7fffffff)
        count = min(131072, size - len(output))
        if header & 0x80000000:
            assert coded == count
            output.extend(packet[position:position + coded])
        elif mode in (13, 15, 16) and header & 0x40000000:
            block = huffman_inverse(packet[position:position + coded])
            assert len(block) == count
            output.extend(block)
        elif mode == 15:
            parsed, literals, control_bytes = struct.unpack_from('<III', packet, position)
            controls = huffman_inverse(packet[position + 12:position + 12 + control_bytes])
            source = huffman_inverse(packet[position + 12 + control_bytes:position + coded])
            assert len(controls) == parsed and len(source) == literals
            output.extend(reconstruct(controls, count, minimum, source))
        else:
            parsed = struct.unpack_from('<I', packet, position)[0]
            stream = huffman_inverse(packet[position + 4:position + coded])
            assert parsed == len(stream)
            if mode == 16:
                output.extend(reconstruct(stream, count, minimum, history=output))
            else:
                output.extend(reconstruct(stream, count, minimum))
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
