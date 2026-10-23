"""Independent expansion of the compact code-length model grammar."""
import sys
from pathlib import Path


def run():
    data = Path(sys.argv[1]).read_bytes()
    position = 0
    cases = 0
    while position < len(data):
        size = data[position]
        position += 1
        expected = data[position:position + 256]
        position += 256
        packet = data[position:position + size]
        position += size
        tokens = []
        for byte in packet:
            tokens.extend((byte % 16, byte // 16))
        decoded = bytearray()
        cursor = 0
        previous = 0
        while len(decoded) < 256:
            token = tokens[cursor]
            cursor += 1
            if token <= 12:
                decoded.append(token)
                previous = token
            else:
                assert token in (14, 15)
                count = tokens[cursor]
                cursor += 1
                if token == 15:
                    count += 16 * tokens[cursor] + 19
                    cursor += 1
                else:
                    count += 3
                decoded.extend([previous] * count)
        assert bytes(decoded) == expected and (cursor + 1) // 2 == size
        if cursor % 2:
            assert tokens[cursor] == 0
        assert size <= 128
        cases += 1
    assert position == len(data) and cases == 43434
    print('PASS:', cases, 'independent model inverses')


if __name__ == '__main__':
    run()
