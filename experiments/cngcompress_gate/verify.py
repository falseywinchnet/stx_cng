"""Validate complete-image and collection gate records against release evidence."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def run():
    root = Path('experiments/cngcompress_gate/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, expected in checks['source_sha256'].items():
        assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected, name
    for name, expected in checks['evidence_sha256'].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, name
    expected_collection = Path('src/collection.cpp').read_text().replace('#include "stx_cng/collection.hpp"', '#include "candidate_collection.hpp"').replace('stx_cng_collection', 'cng_candidate_collection').replace('stx_cng::', 'cng_candidate::').replace('{137, 67, 78, 71, 13, 10, 26, 10}', '{137, 67, 78, 88, 13, 10, 26, 10}')
    assert expected_collection == Path('experiments/cngcompress_gate/candidate_collection.cpp').read_text()
    release = json.loads(Path('evidence/verification.json').read_text())
    first = None
    for name in ('whole-first', 'whole-repeat', 'whole-short'):
        report = json.loads(gzip.decompress((root / (name + '.json.gz')).read_bytes()))
        assert report['complete']
        shape = []
        all_rows = []
        for group, count in [('general', 131), ('rgba', 80), ('composites', 11)]:
            rows = report['groups'][group]
            assert len(rows) == count
            all_rows += rows
            references = release['groups'][group]['images']
            for row, reference in zip(rows, references):
                assert row['exact'] and row['name'] == reference['name']
                assert row['input_sha256'] == reference['cng_sha256']
                assert row['raw_sha256'] == reference['source_sha256']
                assert row['reference_bytes'] == reference['cng_bytes']
                shape.append([row[key] for key in ('name', 'raw_sha256', 'candidate_sha256', 'reference_bytes', 'candidate_bytes')])
            summarize(name, group, rows)
        summarize(name, 'all', all_rows)
        if first is None:
            first = shape
        else:
            assert shape == first
    collection_shape = None
    for name in ('collections-first', 'collections-repeat', 'collections-short-first', 'collections-short-repeat'):
        rows = list(csv.DictReader(io.StringIO((root / (name + '.csv')).read_text())))
        assert len(rows) == 12
        shape = []
        for kind in range(3):
            for form in range(2):
                group = [row for row in rows if int(row['case']) == kind and int(row['form']) == form]
                assert [row['method'] for row in group] == ['0', '1']
                reference, candidate = group
                assert reference['images'] == candidate['images'] == str([16, 64, 8][kind])
                print(name, kind, form, {key: float(candidate[key]) / float(reference[key]) for key in ('bytes', 'encode_seconds', 'decode_seconds')})
                for row in group:
                    shape.append([row[key] for key in ('case', 'form', 'method', 'images', 'bytes', 'encode_workspace', 'decode_workspace')])
        if collection_shape is None:
            collection_shape = shape
        else:
            assert shape == collection_shape
        assert 'PASS: 3 collections, both forms, both codecs, 7 paired trials; exact slices and plane ownership' in (root / (name + '.log')).read_text()
    assert 'PASS 1178 byte-identical packets and inverses; 128 full-alphabet depth checks' in (root / 'check.log').read_text()
    assert checks['vector_sha256'] == '04e9277ec8c5f6913918dce18c2792b1f4504fe068d9ff0e142f51ace022126b'
    assert '432' in (root / 'bounds.log').read_text()
    print('PASS: three 222-image runs, collection forms and release identity')


def summarize(run, group, rows):
    result = {'size_ratio': sum(row['candidate_bytes'] for row in rows) / sum(row['reference_bytes'] for row in rows)}
    for metric in ('encode_seconds', 'decode_seconds'):
        result[metric + '_ratio'] = sum(row['candidate']['medians'][metric] for row in rows) / sum(row['reference']['medians'][metric] for row in rows)
        result[metric + '_regressions'] = sum(row['candidate']['medians'][metric] > row['reference']['medians'][metric] for row in rows)
    result['size_regressions'] = sum(row['candidate_bytes'] > row['reference_bytes'] for row in rows)
    print(run, group, result)


if __name__ == '__main__':
    run()
