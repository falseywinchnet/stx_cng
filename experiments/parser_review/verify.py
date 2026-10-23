"""Verify the native order witness and the exact-count corpus provenance."""
import gzip
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    root = Path(__file__).resolve().parents[2]
    folder = root / 'experiments/parser_review'
    checks = json.loads((folder / 'checks.json').read_text())
    assert checks['witness_sha256'] == checks['sibling_witness_sha256']
    assert checks['deterministic_huffman_counts'] == 432
    assert checks['corpus_huffman_counts'] == 2953 and checks['images'] == 222
    for name, expected in checks['source_sha256'].items():
        assert digest(root / name) == expected, name
    assert digest(folder / 'counterexample.json') == checks['counterexample_sha256']
    assert digest(folder / 'corpus_manifest.json') == checks['corpus_manifest_sha256']
    witness = json.loads((folder / 'counterexample.json').read_text())
    assert witness['control_bytes'] == [42, 44]
    assert witness['native_output_sha256'] == checks['witness_sha256']
    corpus = json.loads((folder / 'corpus_manifest.json').read_text())
    assert corpus['source_sha256'] == digest(folder / 'extract.py')
    assert len(corpus['images']) == 222
    assert sum(row['packets'] for row in corpus['images']) == corpus['packets'] == 2953
    baseline = json.loads(gzip.decompress((root / 'experiments/whole_codec/evidence/first_corpus.json.gz').read_bytes()))
    admitted = {}
    for group, rows in baseline['groups'].items():
        for row in rows:
            admitted[(group, row['name'])] = row['candidate_sha256']
    for row in corpus['images']:
        assert row['candidate_sha256'] == admitted[(row['group'], row['name'])]
    print('PASS: native 42/44-byte witness, exact-count source identities and 2953-packet corpus provenance')


if __name__ == '__main__':
    run()
