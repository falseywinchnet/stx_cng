"""Independent scalar inverse for experimental direct-event packets."""
import bisect
import struct
import sys
from pathlib import Path


def take_bits(packet, position, count):
    value = 0
    for bit in range(count):
        value += ((packet[position // 8] >> (position % 8)) & 1) * (2 ** bit)
        position += 1
    return value, position


def inverse(packet):
    mode, size = struct.unpack_from('<BI', packet)
    assert mode in (26, 29)
    if size == 0:
        assert len(packet) == 5
        return b''
    models = []
    position = 5
    model_bit = 40
    for field in range(3):
        if mode == 29:
            active, model_bit = take_bits(packet, model_bit, 7)
        else:
            active = packet[position]
            position += 1
        starts = []
        symbols = []
        frequencies = []
        total = 0
        for index in range(active):
            if mode == 29:
                symbol, model_bit = take_bits(packet, model_bit, 7)
                if index + 1 == active:
                    frequency = 1024 - total
                else:
                    frequency, model_bit = take_bits(packet, model_bit, 10)
            else:
                symbol, frequency = struct.unpack_from('<BH', packet, position)
                position += 3
            starts.append(total)
            symbols.append(symbol)
            frequencies.append(frequency)
            total += frequency
        assert total == 1024
        models.append((starts, symbols, frequencies))
    if mode == 29:
        position = (model_bit + 7) // 8
        for padding in range(model_bit, position * 8):
            assert ((packet[padding // 8] >> (padding % 8)) & 1) == 0
    suffix_bytes = struct.unpack_from('<I', packet, position)[0]
    position += 4
    states = list(struct.unpack_from('<4I', packet, position))
    position += 16
    suffix = packet[position:position + suffix_bytes]
    position += suffix_bytes
    suffix_bit = 0
    previous_distance = 0
    count = size
    output = bytearray(12 * size)
    for field in range(3):
        starts, symbols, frequencies = models[field]
        for index in range(count):
            lane = (field * count + index) % 4
            state = states[lane]
            slot = state % 1024
            entry = bisect.bisect_right(starts, slot) - 1
            state = frequencies[entry] * (state // 1024) + slot - starts[entry]
            if state < 32768:
                word = struct.unpack_from('<H', packet, position)[0]
                position += 2
                state = state * 65536 + word
            states[lane] = state
            symbol = symbols[entry]
            value = symbol
            if symbol >= 16:
                bits = 2 + (symbol - 16) // 4
                value = (4 + (symbol - 16) % 4) * (2 ** bits)
                for bit in range(bits):
                    value += ((suffix[suffix_bit // 8] >> (suffix_bit % 8)) & 1) * (2 ** bit)
                    suffix_bit += 1
            if field == 1 and value:
                value += 2
            if field == 2 and value:
                if value == 1:
                    value = previous_distance
                    assert value
                else:
                    value -= 1
                previous_distance = value
            for byte in range(4):
                output[12 * index + 4 * field + byte] = (value >> (8 * byte)) & 255
    assert states == [32768] * 4 and position == len(packet)
    assert (suffix_bit + 7) // 8 == len(suffix)
    for bit in range(suffix_bit, 8 * len(suffix)):
        assert ((suffix[bit // 8] >> (bit % 8)) & 1) == 0
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
    assert cases == 48 and position == len(vectors)
    print('PASS:', cases, 'independent direct-event inverses')


if __name__ == '__main__':
    run()
