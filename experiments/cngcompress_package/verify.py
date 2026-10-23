"""Check the standalone package's retained source and ordinary test evidence."""
import gzip
import hashlib
import json
from pathlib import Path


def run():
    root = Path('experiments/cngcompress_package/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, expected in checks['source_sha256'].items():
        assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == expected, name
    for name, expected in checks['evidence_sha256'].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, name
    assert '100% tests passed, 0 tests failed out of 2' in (root / 'tests-final-v2.log').read_text()
    assert '432 public frame cases' in (root / 'tests-final-v2.log').read_text()
    assert '100% tests passed, 0 tests failed out of 1' in (root / 'installed-final-v2.log').read_text()
    assert '414 known-raw vectors and 1413 retained frames' in (root / 'identity.log').read_text()
    libraries = (root / 'installed-libraries.log').read_text()
    assert 'libc++.1.dylib' in libraries and 'libSystem.B.dylib' in libraries
    assert 'zstd' not in libraries and 'libz.' not in libraries
    for unit in ('huffman', 'match'):
        for target in ('arm64', 'x86_64'):
            assembly = gzip.decompress((root / (unit + '-' + target + '.s.gz')).read_bytes())
            assert len(assembly) > 1000
    print('PASS: standalone package source, 432 cases, relocated C consumer and 1827 frame identities')


if __name__ == '__main__':
    run()
