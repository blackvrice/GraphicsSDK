"""Replay Apple offline MSL and installed Metal draw evidence on both architectures."""

import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT/'tools'))

from linux_comparison import read, require, sha
from linux_readiness_report import unpack
from metal_spirv import SCOPE, check


def inspect(matrix, summary):
    require({path.name for path in matrix.iterdir()} == {'runner.py', 'arm64', 'x86_64'},
            'Unexpected Metal conversion matrix')
    keys = ('source_commit', 'github_run_id', 'SDK_source_fingerprint', 'Vulkan_source_commit', 'D3D12_source_commit')
    profiles = {}
    for arch in ('arm64', 'x86_64'):
        native = matrix/arch
        receipt = check(native)
        require(arch == receipt['architecture'] and receipt['runner_sha256'] == sha(matrix/'runner.py') and
                all(receipt[key] == summary[key] for key in keys), 'Metal conversion source or architecture differs')
        names = ('vert.metal', 'frag.metal', 'metal.bin')
        profiles[arch] = dict(receipt_sha256=sha(native/'receipt.json'),
                              outputs={name: receipt['files'][name] for name in names})
    require(profiles['arm64']['outputs'] == profiles['x86_64']['outputs'],
            'Native architectures produced different Metal sources or readbacks')
    return profiles


def verify():
    summary = read(HERE/'summary.json')
    require(summary['schema_version'] == 1 and summary['scope'] == SCOPE, 'Unexpected Metal conversion summary')
    archive = HERE/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'Metal conversion evidence archive differs')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out', prefix='msl-evidence-') as directory:
        folder = pathlib.Path(directory)
        unpack(archive, summary['files'], folder)
        require({path.name for path in folder.iterdir()} == {'matrix'}, 'Unexpected Metal conversion evidence root')
        require(inspect(folder/'matrix', summary) == summary['profiles'], 'Metal conversion matrix summary differs')
    print('PASS ARM64 and Intel Metal MSL evidence and complete Vulkan/D3D12 image parity')


if __name__ == '__main__':
    verify()
