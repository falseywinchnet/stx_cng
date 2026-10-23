"""Pair the previous combined candidate and the limited-copy candidate directly."""
import argparse
import hashlib
import json
import statistics
import subprocess
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    binary = Path('.build/raw-fallback/cng_direct_comparison')
    original_root = Path('.build/whole-codec/first-corpus')
    original = json.loads((original_root / 'results.json').read_text())
    report = {'complete': False, 'scope': __doc__, 'binary_sha256': digest(binary),
              'cpu': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
              'reference': 'previous combined encoder, same CNE grammar',
              'source_sha256': {}, 'cases': []}
    paths = list(Path('experiments/raw_fallback').glob('*'))
    paths += list(Path('experiments/whole_codec').glob('*'))
    paths += list(Path('src').glob('*')) + list(Path('include/stx_cng').glob('*'))
    paths += [Path('experiments/entropy_reduction/huffman_reduced.cpp'),
              Path('experiments/entropy/huffman_kernel.hpp'), Path('experiments/entropy/rans.hpp')]
    for path in paths:
        if path.is_file():
            report['source_sha256'][str(path)] = digest(path)
    selected = ('random', 'smooth', 'mixed_order_noise', 'large_smooth_rgba',
                'kodim01', 'rectangle_on_gradient', 'composite_control_states')
    for group, rows in original['groups'].items():
        for row in rows:
            if row['name'] not in selected:
                continue
            source = original_root / row['candidate_path']
            assert digest(source) == row['candidate_sha256']
            case = {'name': row['name'], 'source_sha256': row['candidate_sha256'], 'runs': []}
            report['cases'].append(case)
            for repeat in range(5):
                destination = args.output / (row['name'] + '.cne')
                raw = args.output / (row['name'] + '.raw')
                process = subprocess.run([str(binary), str(source), str(destination), str(raw)],
                                         capture_output=True, text=True, check=True)
                result = json.loads(process.stdout)
                assert result['exact'] and result['candidate_bytes'] == result['reference_bytes'] == row['candidate_bytes']
                assert digest(raw) == row['raw_sha256'] and digest(destination) == row['candidate_sha256']
                case['runs'].append(result)
                raw.unlink()
            print(case['name'], 'encode ratios',
                  [round(statistics.median(result['candidate']['encode_seconds'])
                         / statistics.median(result['reference']['encode_seconds']), 4)
                   for result in case['runs']], flush=True)
            (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert len(report['cases']) == len(selected)
    report['complete'] = True
    (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    run()
