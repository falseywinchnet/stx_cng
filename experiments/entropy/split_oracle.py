"""Independent scalar inverse for the four-stream rANS experiment."""
import bisect
import struct
import sys
from pathlib import Path


def inverse(packet):
    mode, size = struct.unpack_from('<BI', packet)
    if mode == 0:
        assert len(packet) == size + 5
        return packet[5:]
    assert mode == 3
    active = struct.unpack_from('<H', packet, 5)[0]
    position = 7
    starts = []
    frequencies = []
    symbols = []
    cumulative = 0
    for entry in range(active):
        symbol, frequency = struct.unpack_from('<BH', packet, position)
        position += 3
        starts.append(cumulative)
        frequencies.append(frequency)
        symbols.append(symbol)
        cumulative += frequency
    assert cumulative == 4096
    states = struct.unpack_from('<4I', packet, position)
    position += 16
    lengths = struct.unpack_from('<4I', packet, position)
    position += 16
    result = bytearray()
    for lane in range(4):
        end = position + lengths[lane]
        state = states[lane]
        count = size // 4
        if lane == 3:
            count += size % 4
        for index in range(count):
            slot = state % 4096
            entry = bisect.bisect_right(starts, slot) - 1
            result.append(symbols[entry])
            state = frequencies[entry] * (state // 4096) + slot - starts[entry]
            if state < 32768:
                word = struct.unpack_from('<H', packet, position)[0]
                position += 2
                state = state * 65536 + word
        assert state == 32768 and position == end
    assert position == len(packet) and len(result) == size
    return bytes(result)


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
    print('PASS:', cases, 'independent split-stream inverses')


if __name__ == '__main__':
    run()
