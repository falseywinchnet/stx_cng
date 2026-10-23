"""Check the retained portable-merge evidence and source identities."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def run():
    root = Path('experiments/cngcompress_bulk/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, expected in checks['source_sha256'].items():
        assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected, name
    for name, expected in checks['evidence_sha256'].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, name
    reference = None
    structural = ['packet', 'symbols', 'runs', 'constant_children', 'merge_bytes',
                  'bitmap_bytes', 'word_input_bytes', 'selector_bytes', 'spread_bytes',
                  'mixed_groups', 'pure_groups']
    for trial in checks['runs']:
        name = trial['name']
        data = gzip.decompress((root / ('merges-' + name + '.csv.gz')).read_bytes()).decode()
        rows = list(csv.DictReader(io.StringIO(data)))
        assert len(rows) == 2953
        assert [int(row['packet']) for row in rows] == list(range(2953))
        assert sum(int(row['symbols']) for row in rows) == 249948888
        assert sum(int(row['merge_bytes']) for row in rows) == 883150653
        for row in rows:
            assert int(row['selector_bytes']) == 9 * int(row['mixed_groups'])
            assert int(row['spread_bytes']) == 49 * int(row['mixed_groups'])
        shape = [[row[key] for key in structural] for row in rows]
        if reference is None:
            reference = shape
        else:
            assert shape == reference
        assert 'PASS: 73984 boundary/mask fixtures; 2953 fixed-code corpus preparations' in (root / ('run-' + name + '.log')).read_text()
        methods = [key for key in ('scalar', 'indexed', 'words', 'spread', 'lanes') if key in rows[0]]
        totals = {key: sum(float(row[key]) for row in rows) for key in methods}
        assert all(value > 0 for value in totals.values())
        print(name, totals)
    print('PASS: portable merge source identities, four records and equal tree work')


if __name__ == '__main__':
    run()
