"""Prepare or replay isolated Phase 6 Vulkan buffer pool sources from a pinned commit."""

import argparse
import io
import json
import os
import pathlib
import subprocess
import tarfile
import tempfile

from audit_licenses import record_file, sdk_fingerprint, sha256
from linux_comparison import GIT, ROOT, require


BASE_COMMIT = 'e75b658e5e61491f78519e714f6913123194fb48'
SOURCE_PATHS = ('CMakeLists.txt', 'LICENSE', 'cmake', 'examples', 'include', 'shaders', 'src', 'tests', 'tools')
POLICIES = ('dedicated', 'adaptive', 'four-slot')
HISTORICAL = ROOT/'experiments/vulkan_pool_churn/candidate.patch'
HISTORICAL_SHA256 = '2da6c3291e09e0794d66918387e265ec8c7e779981114ce2f769f887ec0117bd'
CAP_PATCH = ROOT/'experiments/vulkan_pool_four_slot/four-slot.patch'
RECEIPT = 'candidate-receipt.json'
VULKAN = 'src/backends/vulkan/'
POOL_FILES = {VULKAN+name for name in ('Vulkan.cpp', 'BufferPool.hpp', 'BufferPoolRange.hpp')}


def inventory(root):
    files = {}
    for path in root.rglob('*'):
        require(not path.is_symlink(), 'Unexpected symbolic link in isolated source')
        if path.is_file() and path != root/RECEIPT:
            record = record_file(root, path)
            files[record['path']] = record
    return dict(sorted(files.items()))


def apply(source, patch):
    data = patch.read_bytes().replace(b'\r\n', b'\n')
    digest = sha256(data)
    if patch == HISTORICAL:
        require(digest == HISTORICAL_SHA256, 'Historical pool patch SHA-256 differs')
    # Run outside the worktree's Git context and apply to the archive only.
    env = {**os.environ, 'GIT_CEILING_DIRECTORIES': str(source.parent)}
    subprocess.run([*GIT, '-c', 'core.autocrlf=false', '-c', 'apply.ignoreWhitespace=no', '-C', str(source),
                    'apply', '--no-index', '--whitespace=error', '-'], input=data, env=env, check=True)
    return dict(path=patch.relative_to(ROOT).as_posix(), normalized_sha256=digest)


def changed(before, after):
    return {path for path in before.keys() | after.keys() if before.get(path) != after.get(path)}


def generate(source, policy):
    data = subprocess.check_output([*GIT, '-c', 'core.autocrlf=false', '-c', 'core.eol=lf',
                                    '-C', str(ROOT), 'archive', BASE_COMMIT, *SOURCE_PATHS])
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        archive.extractall(source, filter='data')
    baseline = inventory(source)
    baseline_hash = sdk_fingerprint(source)
    patches = []
    if policy != 'dedicated':
        cpp = source/(VULKAN+'Vulkan.cpp')
        original = cpp.read_bytes()
        cpp.write_bytes(original.replace(b'\r\n', b'\n'))
        patches.append(apply(source, HISTORICAL))
        if b'\r\n' in original:
            cpp.write_bytes(cpp.read_bytes().replace(b'\n', b'\r\n'))
        adaptive = inventory(source)
        require(changed(baseline, adaptive) == POOL_FILES, 'Adaptive patch touched unexpected files')
        if policy == 'four-slot':
            patches.append(apply(source, CAP_PATCH))
            require(changed(adaptive, inventory(source)) == {VULKAN+'BufferPoolRange.hpp'},
                    'Slot cap patch touched unexpected files')
    files = inventory(source)
    delta = sorted(changed(baseline, files))
    receipt = dict(schema_version=1, policy=policy, base_commit=BASE_COMMIT,
                   product_SDK_source_fingerprint=baseline_hash,
                   candidate_SDK_source_fingerprint=sdk_fingerprint(source),
                   production_adopted=False, changed_files=delta, patches=patches, files=list(files.values()))
    (source/RECEIPT).write_bytes((json.dumps(receipt, indent=2)+'\n').encode('utf-8'))
    return receipt


def prepare(output, policy, check):
    out = (ROOT/'out').resolve()
    output = output.resolve()
    require(out.is_relative_to(ROOT) and output != out and output.is_relative_to(out),
            'Use an isolated output directory under repository out/')
    require(output.is_dir() if check else not output.exists(), 'Check requires an existing source; prepare requires a new path')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix='pool-stage-') as temporary:
        source = pathlib.Path(temporary)/'source'
        source.mkdir()
        receipt = generate(source, policy)
        if check:
            require((output/RECEIPT).read_bytes() == (source/RECEIPT).read_bytes() and
                    list(inventory(output).values()) == receipt['files'], 'Isolated source or receipt differs')
        else:
            require(not output.exists(), 'Isolated output path was created during generation')
            source.rename(output)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--policy', choices=POLICIES, default='four-slot')
    parser.add_argument('--check', action='store_true', help='Recreate the source and verify an existing tree')
    args = parser.parse_args()
    receipt = prepare(args.output, args.policy, args.check)
    print(f"PASS {args.policy} SDK source {receipt['candidate_SDK_source_fingerprint']}")


if __name__ == '__main__':
    main()
