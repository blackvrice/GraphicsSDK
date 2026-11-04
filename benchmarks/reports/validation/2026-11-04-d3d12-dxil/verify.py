"""Replay the installed D3D12 DXIL WARP evidence without the runtime."""

import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT/'tools'))

from d3d12_dxil import SCOPE, check
from linux_comparison import read, require, sha
from linux_readiness_report import unpack


def verify():
    summary = read(HERE/'summary.json')
    require(summary['schema_version'] == 1 and summary['scope'] == SCOPE, 'Unexpected D3D12 DXIL summary scope')
    archive = HERE/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'D3D12 DXIL evidence archive differs')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out', prefix='dxil-evidence-') as directory:
        folder = pathlib.Path(directory)
        unpack(archive, summary['files'], folder)
        require({path.name for path in folder.iterdir()} == {'matrix'}, 'Unexpected DXIL evidence root')
        matrix = folder/'matrix'
        require({path.name for path in matrix.iterdir()} == {'runner.py', 'warp'}, 'Unexpected DXIL evidence matrix')
        native = matrix/'warp'
        receipt = check(native)
        require(sha(matrix/'runner.py') == receipt['runner_sha256'] and
                sha(native/'receipt.json') == summary['receipt_sha256'] and
                all(receipt[key] == summary[key] for key in ('source_commit', 'github_run_id', 'SDK_source_fingerprint')),
                'DXIL evidence revision, runner or receipt differs')
    print('PASS installed D3D12 WARP DXIL and DXBC evidence, two complete 13x7 RGBA images')


if __name__ == '__main__':
    verify()
