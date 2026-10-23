"""Independent packet and scalar Huffman inverse for fixed 128 KiB blocks."""
import struct
import sys
from pathlib import Path
from huffman_oracle import inverse as huffman_inverse


def inverse(packet):
    mode, size = struct.unpack_from('<BI', packet)
    if mode == 0:
        assert len(packet) == size + 5
        return packet[5:]
    assert mode == 8
    output = bytearray()
    position = 5
    while len(output) < size:
        coded = struct.unpack_from('<I', packet, position)[0]
        position += 4
        block = huffman_inverse(packet[position:position + coded])
        assert len(block) == min(131072, size - len(output))
        output.extend(block)
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
    assert cases == 132 and position == len(vectors)
    print('PASS:', cases, 'independent blocked Huffman inverses')


if __name__ == '__main__':
    run()
