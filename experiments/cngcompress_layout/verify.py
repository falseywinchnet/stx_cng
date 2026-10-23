"""Verify fixed-code complete-layout records and provenance."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def run():
    root = Path('experiments/cngcompress_layout/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, value in checks['source_sha256'].items():
        assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == value, name
    for name, value in checks['evidence_sha256'].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == value, name
    assert 'PASS: 147968 constant-child mask/boundary fixtures' in (root / 'constant-test.log').read_text()
    reference = None
    for name in checks['runs']:
        text = gzip.decompress((root / (name + '.csv.gz')).read_bytes()).decode()
        rows = list(csv.DictReader(io.StringIO(text)))
        assert len(rows) == 2953
        shape = [[row[key] for key in ('packet', 'symbols', 'common_bits', 'classic_bytes', 'layout_bytes')] for row in rows]
        if reference is None:
            reference = shape
        else:
            assert shape == reference
        assert [int(row['packet']) for row in rows] == list(range(2953))
        assert sum(int(row['symbols']) for row in rows) == 249948888
        for row in rows:
            bits = int(row['common_bits'])
            classic = int(row['classic_bytes'])
            layout = int(row['layout_bytes'])
            # At most 255 coded tree nodes, each with <=7 padding bits.
            assert (bits + 7) // 8 + 5 <= layout <= (bits + 7) // 8 + 5 + 128 + 254
            assert abs(classic - layout) <= 270
        for group, selected in [('all', rows), ('short', [row for row in rows if int(row['symbols']) <= 1024]), ('long', [row for row in rows if int(row['symbols']) > 1024])]:
            result = {'count': len(selected)}
            for field in ('bytes', 'encode', 'decode'):
                first = sum(float(row['classic_' + field]) for row in selected)
                second = sum(float(row['layout_' + field]) for row in selected)
                result[field + '_ratio'] = second / first
            result['decode_regressions'] = sum(float(row['layout_decode']) > float(row['classic_decode']) for row in selected)
            print(name, group, result)
        assert 'PASS: 280 flat/boundary fixtures; 2953 complete packet comparisons' in (root / (name + '.log')).read_text()
    print('PASS: complete fixed-code layout evidence')


if __name__ == '__main__':
    run()
