"""Replay Metal API Validation evidence without running archived code."""

import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT/'tools'))

from linux_comparison import read, require, sha
from linux_readiness_report import unpack
from macos_core import exact
from macos_metal_validation import check


def inspect(matrix, source, run_id):
    require({p.name for p in matrix.iterdir()} == {'arm64', 'x86_64', 'runner.py'}, 'Unexpected matrix contents')
    runner_sha = sha(matrix/'runner.py')
    results = {}
    for arch in ('arm64', 'x86_64'):
        native = matrix/arch/'native'
        py = matrix/arch/'python'
        receipt = read(native/'receipt.json')
        exact(receipt, dict(architecture=arch, source_commit=source, github_run_id=run_id))
        python = read(py/'receipt.json')
        exact(python, dict(source_commit=source, github_run_id=run_id, tests=1504, successful=True,
                           SDK_source_fingerprint=receipt['SDK_source_fingerprint'], inputs_unchanged=True,
                           failures=[], errors=[], skipped=[]))
        exact(python['platform'], dict(system='Darwin', machine=arch))
        tested = {r['path']:r['sha256'] for r in python['inputs']}
        require(tested['tools/macos_metal_validation.py'] == runner_sha, 'Tested Metal checker differs')
        require(sha(py/'unittest.log') == python['log']['sha256'], 'Python test log differs')
        results[arch] = check(native)
        results[arch]['python_contracts'] = python['tests']
    require(len({r['SDK_source_fingerprint'] for r in results.values()}) == 1, 'Mixed SDK source fingerprints')
    return results


def verify():
    summary = read(HERE/'summary.json')
    exact(summary, dict(schema_version=1, scope='macos-metal-api-validation-v1', external_trust_signature=False))
    archive = HERE/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'Validation ZIP bytes changed')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out', prefix='metal-api-replay-') as tmp:
        destination = pathlib.Path(tmp)
        unpack(archive, summary['files'], destination)
        require({p.name for p in destination.iterdir()} == {'matrix'}, 'Unexpected evidence root')
        actual = inspect(destination/'matrix', summary['source_commit'], summary['github_run_id'])
    exact(summary, dict(architectures=actual))
    print('PASS Metal API Validation controls, diagnostics and installed SDK readbacks on arm64 and x86_64')


if __name__ == '__main__':
    verify()
