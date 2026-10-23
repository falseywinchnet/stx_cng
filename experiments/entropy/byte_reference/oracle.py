"""Independent scalar inverse of the experimental four-state rANS packets."""
import argparse
import bisect
import struct
from pathlib import Path


def decode(packet, expected):
    mode = packet[0]
    size = int.from_bytes(packet[1:5], 'little')
    if size != len(expected):
        raise AssertionError('declared raw size')
    if mode == 0:
        if packet[5:] != expected:
            raise AssertionError('stored samples')
        return
    if mode != 1:
        raise AssertionError('coding')
    count = int.from_bytes(packet[5:7], 'little')
    symbols = []
    starts = [0]
    position = 7
    for unused in range(count):
        symbols.append(packet[position])
        frequency = int.from_bytes(packet[position+1:position+3], 'little')
        starts.append(starts[-1]+frequency)
        position += 3
    if starts[-1] != 4096:
        raise AssertionError('population')
    states = list(struct.unpack_from('<4I',packet,position))
    position += 16
    output = bytearray()
    for index in range(size):
        lane = index%4
        state = states[lane]
        bucket = state%4096
        symbol = bisect.bisect_right(starts,bucket)-1
        output.append(symbols[symbol])
        frequency = starts[symbol+1]-starts[symbol]
        state = frequency*(state//4096)+bucket-starts[symbol]
        while state < 8388608:
            state = state*256+packet[position]
            position += 1
        states[lane] = state
    if bytes(output) != expected or position != len(packet) or states != [8388608]*4:
        raise AssertionError('scalar inverse')


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('vectors', type=Path)
    args = parser.parse_args()
    raw = args.vectors.read_bytes()
    position = 0
    cases = 0
    while position < len(raw):
        size,packed = struct.unpack_from('<II',raw,position)
        position += 8
        expected = raw[position:position+size]
        position += size
        packet = raw[position:position+packed]
        position += packed
        decode(packet,expected)
        cases += 1
    if position != len(raw) or cases != 96:
        raise AssertionError('vector coverage')
    print('PASS:',cases,'independent scalar inverses')


if __name__ == '__main__':
    run()
