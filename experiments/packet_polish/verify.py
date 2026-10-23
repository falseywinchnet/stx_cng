"""Verify bounded packet polishing evidence without rerunning timings."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    root = Path('experiments/packet_polish/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, expected in checks['source_sha256'].items():
        assert digest(Path(name)) == expected, name
    for name, expected in checks['evidence_sha256'].items():
        assert digest(root / name) == expected, name
    rows = list(csv.DictReader(io.StringIO(gzip.decompress((root / 'packets.csv.gz').read_bytes()).decode())))
    assert len(rows) == 2847 * 3
    totals = []
    preceding = {}
    for passes in range(3):
        selected = [row for row in rows if int(row['passes']) == passes]
        saved = 0
        for row in selected:
            packet = int(row['packet'])
            size = int(row['selected_bytes'])
            baseline = int(row['base_bytes'])
            assert size <= preceding.get(packet, baseline)
            assert size - int(row['lower_bound']) == int(row['gap']) >= 0
            assert int(row['proposals']) <= passes * 1017
            assert int(row['accepted']) <= passes
            assert 0 <= int(row['padding_bits']) <= 28
            preceding[packet] = size
            saved += baseline - size
        totals.append(saved)
    assert totals == [0, 1411, 1827]
    assert checks['sparse_scores'] == 30000 and checks['retained_packets'] == 2953
    print('PASS: 30,000 recorded sparse checks; 2,847 coded packets; savings', totals)


if __name__ == '__main__':
    run()
