"""Replay the Vulkan DXC shader inputs and D3D12 pixel comparison."""

import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT/'tools'))

from linux_comparison import read, require, sha
from linux_readiness_report import unpack
from vulkan_spirv import SCOPE, check


def verify():
    summary = read(HERE/'summary.json')
    require(summary['schema_version'] == 1 and summary['scope'] == SCOPE, 'Unexpected Vulkan SPIR-V scope')
    archive = HERE/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'Vulkan SPIR-V evidence archive differs')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out', prefix='spirv-evidence-') as directory:
        folder = pathlib.Path(directory)
        unpack(archive, summary['files'], folder)
        require({path.name for path in folder.iterdir()} == {'matrix'}, 'Unexpected Vulkan SPIR-V evidence root')
        matrix = folder/'matrix'
        require({path.name for path in matrix.iterdir()} == {'runner.py', 'lavapipe'}, 'Unexpected Vulkan SPIR-V matrix')
        native = matrix/'lavapipe'
        receipt = check(native)
        keys = ('source_commit', 'github_run_id', 'SDK_source_fingerprint', 'D3D12_source_commit')
        require(sha(matrix/'runner.py') == receipt['runner_sha256'] and
                sha(native/'receipt.json') == summary['receipt_sha256'] and
                all(receipt[key] == summary[key] for key in keys), 'Vulkan source or receipt differs')
    print('PASS Vulkan DXC SPIR-V evidence and byte-identical D3D12 DXIL/DXBC reference')


if __name__ == '__main__':
    verify()
