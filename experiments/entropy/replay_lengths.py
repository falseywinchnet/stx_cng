"""Inspect verified reference parses for selected failures and a photo control."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def digest(path):
    result = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError('preserve prior diagnostic')
    binary = Path('.build/entropy/cng_match_replay_lengths')
    source = Path('experiments/entropy/match_replay_lengths.cpp')
    frozen = json.loads((args.corpus / 'results/row_copy_comparison.json').read_text())
    admitted = json.loads(Path('evidence/verification.json').read_text())
    selected = ('smooth', 'large_smooth_rgba', 'rectangle_on_gradient',
                'composite_control_states', 'kodim01', 'mixed_order_noise')
    result = dict(scope='Informational generated Zstandard level-3 parses, not extracted frozen bitstream traces. '
                  'Generated matches are clipped to 128 KiB boundaries, matched pieces below eight or three become literals in separate replays with identical framing and Huffman coding. Every replay is decoded exactly; ordinary Zstandard recompression matches frozen frames.',
                  source_sha256={}, binary_sha256=digest(binary), images=[])
    for name in ('match_replay_lengths.cpp', 'huffman_scalars.cpp', 'match_huffman.hpp', 'huffman_kernel.hpp', 'rans.hpp', 'replay_lengths.py', 'CMakeLists.txt'):
        result['source_sha256'][name] = digest(Path('experiments/entropy') / name)
    for group in ('general', 'rgba', 'composites'):
        for original, reference in zip(frozen[group], admitted['groups'][group]['images']):
            if original['name'] not in selected:
                continue
            packet = args.corpus / original['original_artifact']
            assert original['name'] == reference['name'] and digest(packet) == reference['cng_sha256']
            data = subprocess.check_output([str(binary), str(packet)], text=True)
            row = json.loads(data)
            row['name'] = reference['name']
            row['group'] = group
            row['file_sha256'] = reference['cng_sha256']
            result['images'].append(row)
    assert len(result['images']) == len(selected)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS:', len(selected), 'verified reference-parse diagnostics')


if __name__ == '__main__':
    run()
