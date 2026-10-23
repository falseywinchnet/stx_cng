"""Independently reconstruct tail and stream-count boundary vectors."""
import struct
import sys
from pathlib import Path
from huffman_oracle import inverse


def run():
    data = Path(sys.argv[1]).read_bytes()
    position = 0
    cases = 0
    modes = set()
    while position < len(data):
        size, coded = struct.unpack_from('<II', data, position)
        position += 8
        raw = data[position:position + size]
        position += size
        packet = data[position:position + coded]
        position += coded
        assert inverse(packet) == raw
        modes.add(packet[0])
        cases += 1
    assert position == len(data) and cases == 405
    assert modes in ({0, 4, 35}, {0, 4, 36})
    print('PASS: 405 independent Huffman tail/stream-boundary inverses')


if __name__ == '__main__':
    run()
