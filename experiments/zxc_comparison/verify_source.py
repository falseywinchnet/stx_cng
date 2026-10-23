"""Check the pinned comparator source before building, without installing it."""
import hashlib
import json
from pathlib import Path

pin = json.loads(Path('experiments/zxc_comparison/pin.json').read_text())
root = Path('build/zxc-study/source')
for name, expected in pin['source_sha256'].items():
    actual = hashlib.sha256((root / name).read_bytes()).hexdigest()
    assert actual == expected, name
print('PASS: zxc', pin['tag'], pin['commit'], 'source identities')
