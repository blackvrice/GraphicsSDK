"""Replay the relocated SDK audit and Metal draw references from archived data."""

import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT/'tools'))

from linux_comparison import read, require, sha
from linux_llgl_probe import same
from linux_readiness_report import unpack
from macos_core import exact
from macos_metal_package import check


def inspect(matrix, source, run_id):
    require({p.name for p in matrix.iterdir()} == {'arm64', 'x86_64', 'runner.py'}, 'Unexpected package matrix')
    results = {}
    for arch in ('arm64', 'x86_64'):
        native = matrix/arch/'package'
        python = matrix/arch/'python'
        package = read(native/'receipt.json')
        exact(package, dict(architecture=arch, source_commit=source, github_run_id=run_id))
        py = read(python/'receipt.json')
        exact(py, dict(source_commit=source, github_run_id=run_id, successful=True, inputs_unchanged=True,
                       SDK_source_fingerprint=package['SDK_source_fingerprint'], failures=[], errors=[], skipped=[]))
        exact(py['platform'], dict(system='Darwin', machine=arch))
        require(type(py['tests']) is int and py['tests'] > 0, 'Missing complete Python test count')
        tested = {p['path']:p['sha256'] for p in py['inputs']}
        require(tested['tools/macos_metal_package.py'] == sha(matrix/'runner.py'), 'Package runner differs from tested source')
        require(sha(python/'unittest.log') == py['log']['sha256'], 'Python test log bytes differ')
        value = check(native)
        value.update(SDK_source_fingerprint=package['SDK_source_fingerprint'], zip_sha256=package['zip_sha256'],
                     python_contracts=py['tests'])
        results[arch] = value
    require(len({value['SDK_source_fingerprint'] for value in results.values()}) == 1, 'Mixed package SDK revisions')
    return results


def verify():
    summary = read(HERE/'summary.json')
    exact(summary, dict(schema_version=1, scope='macos-relocated-metal-package-v1', external_trust_signature=False))
    archive = HERE/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'Package evidence ZIP changed')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out', prefix='metal-pkg-') as directory:
        folder = pathlib.Path(directory)
        unpack(archive, summary['files'], folder)
        require({p.name for p in folder.iterdir()} == {'matrix'}, 'Unexpected package evidence root')
        actual = inspect(folder/'matrix', summary['source_commit'], summary['github_run_id'])
    require(same(actual, summary['architectures']), 'Saved package audit differs from replay')
    print('PASS relocated macOS SDK package and installed-only Metal draw evidence on arm64 and x86_64')


if __name__ == '__main__':
    verify()
