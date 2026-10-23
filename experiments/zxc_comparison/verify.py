"""Verify zxc comparison provenance, completeness and paired layout records."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows(root, name):
    data = gzip.decompress((root / (name + '.csv.gz')).read_bytes()).decode()
    return list(csv.DictReader(io.StringIO(data)))


def run():
    root = Path('experiments/zxc_comparison/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, expected in checks['source_sha256'].items():
        assert digest(Path(name)) == expected, name
    for name, expected in checks['evidence_sha256'].items():
        assert digest(root / name) == expected, name
    frame_rows = rows(root, 'frames')
    control_rows = rows(root, 'controls')
    for records, count in ((frame_rows, 1413), (control_rows, 20)):
        assert len(records) == count * 7
        for frame in range(count):
            group = records[frame * 7:(frame + 1) * 7]
            assert [int(row['level']) for row in group] == [0, -3, 1, 3, 5, 6, 7]
            assert all(int(row['frame']) == frame for row in group)
            assert len({int(row['raw_bytes']) for row in group}) == 1
            assert all(int(row['coded_bytes']) > 0 for row in group)
    baseline = [row for row in frame_rows if row['level'] == '0']
    assert sum(int(row['raw_bytes']) for row in baseline) == 280540670
    assert sum(int(row['coded_bytes']) for row in baseline) == 180012867
    for level in (-3, 1, 3, 5, 6, 7):
        selected = [row for row in frame_rows if int(row['level']) == level]
        result = {}
        for field in ('coded_bytes', 'encode_seconds', 'decode_seconds'):
            result[field] = sum(float(row[field]) for row in selected) / sum(float(row[field]) for row in baseline)
        print('level', level, result)
    native = rows(root, 'huffman')
    portable = rows(root, 'huffman-no-intrinsics')
    assert len(native) == len(portable) == 2953
    assert sum(int(row['cap_changed']) for row in native) == 2322
    for first, second in zip(native, portable):
        for field in ('packet', 'symbols', 'common_bits', 'cap_changed', 'classic_bytes', 'pivco_bytes'):
            assert first[field] == second[field]
    for name, records in (('NEON', native), ('no explicit intrinsics', portable)):
        ratio = sum(float(row['pivco_decode']) for row in records) / sum(float(row['classic_decode']) for row in records)
        print(name, 'layout decode ratio', ratio)
    for name, expected in checks['completion_lines'].items():
        assert expected in (root / name).read_text(), name
    print('PASS: 1,413 retained frames, 20 controls, two 2,953-packet layout runs and source identities')


if __name__ == '__main__':
    run()
