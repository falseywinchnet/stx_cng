"""Check the three deep-tree fixtures with the independent bit-by-bit inverse."""
import struct
import sys
from pathlib import Path
from huffman_oracle import inverse


def run():
    vectors = Path(sys.argv[1]).read_bytes()
    position = 0
    count = 0
    while position < len(vectors):
        size, coded = struct.unpack_from('<II', vectors, position)
        position += 8
        raw = vectors[position:position + size]
        position += size
        packet = vectors[position:position + coded]
        position += coded
        assert inverse(packet) == raw
        count += 1
    assert count == 3 and position == len(vectors)
    print('PASS: 3 independent deep-tree inverses')


if __name__ == '__main__':
    run()
