"""Verify grammar trials, fixed matcher fragments and repeat-run sizes."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def segment(text, first, last):
    start = text.index(first)
    end = text.index(last, start)
    return text[start:end]


def run():
    root = Path('experiments/cngcompress_grammar/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, value in checks['source_sha256'].items():
        assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == value, name
    for name, value in checks['evidence_sha256'].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == value, name
    original = Path('experiments/whole_codec/match_bounded.cpp').read_text()
    variant = Path('experiments/cngcompress_grammar/grammar.cpp').read_text()
    for first, last in [('std::size_t extend(', 'struct ParseResult'), ('    while (end - position >= 8)', '        references = true;'), ('        position += length;', '    if (!references)'), ('void copy_match(', 'void reconstruct(')]:
        assert segment(original, first, last) == segment(variant, first, last), first
    shapes = {}
    for name, expected in [('frames', 1413), ('controls', 20), ('frames-repeat', 1413), ('controls-repeat', 20), ('frames-refined', 1413), ('controls-refined', 20)]:
        text = gzip.decompress((root / (name + '.csv.gz')).read_bytes()).decode()
        rows = list(csv.DictReader(io.StringIO(text)))
        assert len(rows) == expected * 4
        for frame in range(expected):
            group = rows[4 * frame:4 * frame + 4]
            assert [int(row['level']) for row in group] == [0, 1, 2, 3]
            assert all(int(row['frame']) == frame for row in group)
            assert len({row['raw_bytes'] for row in group}) == 1
        shape = [[row[key] for key in ('frame', 'level', 'raw_bytes', 'coded_bytes')] for row in rows]
        if name.endswith('-refined'):
            assert shape == shapes[name[:-8]]
        elif name.endswith('-repeat'):
            assert shape == shapes[name[:-7]]
        else:
            shapes[name] = shape
        baseline = [row for row in rows if row['level'] == '0']
        if name.startswith('frames'):
            assert sum(int(row['raw_bytes']) for row in baseline) == 280540670
            assert sum(int(row['coded_bytes']) for row in baseline) == 180012867
        for method in range(1, 4):
            selected = [row for row in rows if int(row['level']) == method]
            ratios = {field: sum(float(row[field]) for row in selected) / sum(float(row[field]) for row in baseline) for field in ('coded_bytes', 'encode_seconds', 'decode_seconds')}
            ratios['decode_regressions'] = sum(float(row['decode_seconds']) > float(base['decode_seconds']) for row, base in zip(selected, baseline))
            print(name, method, ratios)
        assert 'PASS: ' + str(expected) + ' frames, four grammars, five measured trials and warmup, exact inverses' in (root / (name + '.log')).read_text()
    assert 'PASS: 2097162 prefix integer fixtures' in (root / 'integer-test.log').read_text()
    assert 'PASS: 2097162 prefix integer fixtures' in (root / 'integer-test-refined.log').read_text()
    assert 'PASS: width-only dominance does not transfer' in (root / 'cost-witness.log').read_text()
    print('PASS: grammar evidence, unchanged matcher fragments, repeat sizes and deterministic fixtures')


if __name__ == '__main__':
    run()
