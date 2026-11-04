"""Replay the Windows and Linux offline shader toolchain evidence."""

import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT/'tools'))

from linux_comparison import read, require, sha
from linux_readiness_report import unpack
from shader_toolchain import FIXTURE, LOCK, SCOPE, STAGES, check


def inspect(root, summary):
    require({path.name for path in root.iterdir()} == {'runner.py', 'windows-x64', 'linux-x64'},
            'Unexpected shader evidence matrix')
    results = {}
    for profile, system in (('windows-x64', 'Windows'), ('linux-x64', 'Linux')):
        folder = root/profile
        receipt = check(folder)
        require(receipt['system'] == system and receipt['source_commit'] == summary['source_commit'] and
                receipt['github_run_id'] == summary['github_run_id'], 'Mixed shader source, host or CI run')
        require(receipt['runner_sha256'] == sha(root/'runner.py'), 'Archived shader runner differs')
        require(sha(folder/LOCK.name) == summary['lock_sha256'] and
                sha(folder/FIXTURE.name) == summary['fixture_sha256'], 'Mixed shader locks or fixtures')
        outputs = {f'{stage}.{ext}': receipt['files'][f'{stage}.{ext}']
                   for stage, *_ in STAGES
                   for ext in (('dxil', 'spv', 'metal') if system == 'Linux' else ('dxil',))}
        results[profile] = dict(targets=receipt['targets'], outputs=outputs,
                                dxc_version=(folder/'dxc-version.stdout').read_text(encoding='utf-8').strip(),
                                receipt_sha256=sha(folder/'receipt.json'))
    return results


def verify():
    summary = read(HERE/'summary.json')
    require(summary['schema_version'] == 1 and summary['scope'] == SCOPE, 'Unexpected shader summary scope')
    archive = HERE/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'Shader evidence archive differs')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out', prefix='shader-evidence-') as tmp:
        folder = pathlib.Path(tmp)
        unpack(archive, summary['files'], folder)
        require({path.name for path in folder.iterdir()} == {'matrix'}, 'Unexpected shader evidence root')
        require(inspect(folder/'matrix', summary) == summary['profiles'], 'Shader matrix summary differs')
    print('PASS Windows and Linux offline shader toolchain receipts, diagnostics and payloads')


if __name__ == '__main__':
    verify()
