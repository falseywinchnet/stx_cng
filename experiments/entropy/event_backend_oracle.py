"""Independent scalar inverse of the direct-event matching backend."""
import struct
import sys
from pathlib import Path
from huffman_oracle import inverse as huffman_inverse
from match_event_oracle import inverse as event_inverse
from blocked_oracle import inverse as blocked_inverse


def literal_inverse(packet, mode):
    size = struct.unpack_from('<I', packet, 1)[0]
    if mode == 34 and size > 131072:
        return blocked_inverse(packet)
    return huffman_inverse(packet)


def inverse(packet):
    mode, size = struct.unpack_from('<BI', packet)
    if mode == 0:
        assert len(packet) == size + 5
        return packet[5:]
    assert mode in (31, 32, 33, 34)
    block_size = 262144 if mode in (33, 34) else 131072
    output = bytearray()
    position = 5
    while len(output) < size:
        count = min(block_size, size - len(output))
        end = len(output) + count
        header = struct.unpack_from('<I', packet, position)[0]
        position += 4
        coded = header & 0x3fffffff
        if header & 0x80000000:
            assert coded == count
            output.extend(packet[position:position + coded])
        elif header & 0x40000000:
            output.extend(literal_inverse(packet[position:position + coded], mode))
        else:
            events, control_bytes = struct.unpack_from('<II', packet, position)
            records = event_inverse(packet[position + 8:position + 8 + control_bytes])
            assert len(records) == 12 * events
            literals = literal_inverse(packet[position + 8 + control_bytes:position + coded], mode)
            read = 0
            for index in range(events):
                count_literals, length, distance = struct.unpack_from('<III', records, 12 * index)
                output.extend(literals[read:read + count_literals])
                read += count_literals
                if length:
                    assert distance and distance <= len(output) and length >= (3 if mode == 31 else 8)
                    for byte in range(length):
                        output.append(output[-distance])
                else:
                    assert index + 1 == events and distance == 0
            assert read == len(literals)
        assert len(output) == end
        position += coded
    assert len(output) == size and position == len(packet)
    return bytes(output)


def run():
    data = Path(sys.argv[1]).read_bytes()
    position = 0
    cases = 0
    while position < len(data):
        size, coded = struct.unpack_from('<II', data, position)
        position += 8
        raw = data[position:position + size]
        position += size
        packet = data[position:position + coded]
        position += coded
        assert inverse(packet) == raw
        cases += 1
    assert position == len(data) and cases == 414
    print('PASS:', cases, 'independent event-backend inverses')


if __name__ == '__main__':
    run()
