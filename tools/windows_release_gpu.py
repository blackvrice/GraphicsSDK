"""Run offscreen GPU consumers against pinned Windows SDK candidate ZIPs."""

import argparse
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import zipfile

import windows_release_candidate as release
from audit_licenses import AuditError, record_file
from release_candidate_audit import inspect_sdk
from run_linux_validation import EXPECTED, SUITES


CONSUMER = 'tests/windows_release_consumer'
TARGETS = {
    'd3d12_consumer': {'tests/consumer/main.cpp': 'd3d12.cpp'},
    'vulkan_transfer': {'tests/VulkanTests.cpp': 'VulkanTests.cpp'},
    'vulkan_draw': {'tests/VulkanDrawTests.cpp': 'VulkanDrawTests.cpp',
                    'tests/fixtures/SpirvFixtures.hpp': 'fixtures/SpirvFixtures.hpp'},
}
require = release.require


def matching_lines(pattern, stdout):
    regex = re.compile(pattern)
    return [match.groups() for line in stdout.splitlines() if (match := regex.fullmatch(line))]


def verify_source(stdout, fingerprint):
    require(stdout.splitlines().count(f'source={fingerprint} validation=0') == 1,
            'Installed SDK fingerprint or validation mode differs')


def d3d12_result(stdout, fingerprint, version, warp):
    verify_source(stdout, fingerprint)
    adapters = matching_lines(r'adapter=(.+) id=(\d+) software=([01])', stdout)
    require(len(adapters) == 1, 'Expected one D3D12 adapter receipt')
    name, adapter_id, software = adapters[0]
    require((software == '1') == warp, 'D3D12 software/hardware adapter kind differs')
    lines = stdout.splitlines()
    require(f'GraphicsSDK {version}' in lines
            and 'Installed resource statistics: 64MiB retain/release verified' in lines
            and 'Installed Depth/MRT consumer: 1024 pixels verified' in lines,
            'D3D12 installed consumer did not complete the workload')
    return dict(adapter=dict(name=name, id=adapter_id, software=warp), pixels=1024, verified_bytes=4096,
                retain_release_bytes=64 * 1024 * 1024)


def vulkan_result(stdout, suite, fingerprint, adapters):
    verify_source(stdout, fingerprint)
    contract = SUITES[suite]
    matches = matching_lines(contract['pattern'], stdout)
    require(len(matches) == 1, f'Expected one Vulkan {suite} result')
    counts = dict(zip(contract['fields'], map(int, matches[0])))
    require(counts['adapters'] == adapters and counts['assertions'] >= adapters * contract['minimum_assertions']
            and all(counts[key] == value * adapters for key, value in EXPECTED[suite].items()),
            f'Vulkan {suite} workload or adapter count differs')
    return counts


def vulkan_results(transfer, draw, fingerprint):
    transfer_adapters = matching_lines(r'adapter=(.+) id=(\d+) vendor=(\d+) software=([01])', transfer)
    draw_adapters = matching_lines(r'adapter=(.+) id=(\d+)', draw)
    expected = {(name, adapter_id) for name, adapter_id, _, _ in transfer_adapters}
    require(len({row[1] for row in transfer_adapters}) == len(expected) == len(transfer_adapters)
            and len(draw_adapters) == len(expected) and set(draw_adapters) == expected
            and any(row[3] == '0' for row in transfer_adapters), 'Vulkan adapter sets differ or lack hardware')
    adapters = [dict(name=name, id=adapter_id, vendor_id=int(vendor), software=software == '1')
                for name, adapter_id, vendor, software in transfer_adapters]
    return dict(adapters=adapters, transfer=vulkan_result(transfer, 'buffer', fingerprint, len(adapters)),
                draw=vulkan_result(draw, 'draw', fingerprint, len(adapters)))


def build_consumers(output, profile, row, packages, manifest):
    work = output / profile
    source = work / 'external consumer'
    shutil.copytree(release.ROOT / CONSUMER, source)
    prefix = work / 'relocated SDK'
    package = packages / row['package']['name']
    inspect_sdk(package, row['payload'], release.LIBRARY, manifest['version'], third_party=profile == 'vulkan')
    release.unpack(package, row['payload'], prefix)
    build = work / 'consumer-build'
    targets = ('d3d12_consumer',) if profile == 'd3d12' else ('vulkan_transfer', 'vulkan_draw')
    for target in targets:
        for original, name in TARGETS[target].items():
            path = source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(release.ROOT / original, path)
    configured = release.run(work / 'configure.log', ['cmake', '-S', source, '-B', build, *release.MSVC,
                             f'-DPACKAGE_ROOT={prefix.as_posix()}', f'-DPACKAGE_VERSION={manifest["version"]}',
                             f'-DPACKAGE_GPU_PROFILE={profile}'])
    tools = release.toolchain(configured)
    require(all(manifest[key] == value for key, value in tools.items()), 'Consumer toolchain differs from the SDK candidate')
    release.run(work / 'build.log', ['cmake', '--build', build, '--config', 'Release', '--parallel', '1', '--target', *targets])
    inputs = {}
    for target in targets:
        target_dir = build / f'{target}.dir/Release'
        copied = [source / p for p in TARGETS[target].values()]
        inputs[target] = release.consumer_inputs(target_dir, set(copied), prefix)
        inputs[target]['sources'] = [record_file(output, path) for path in [source / 'CMakeLists.txt', *copied]]
        inputs[target]['executable'] = record_file(output, build / 'Release' / f'{target}.exe')
        for name in ('CL.read.1.tlog', 'link.read.1.tlog'):
            inputs[target][name] = record_file(output, next(target_dir.rglob(name)))
    return build / 'Release', inputs


def campaign(packages, manifest_sha, output):
    require(platform.system() == 'Windows', 'Native Windows is required for this smoke test')
    require(output.is_relative_to(release.ROOT / 'out') and output != release.ROOT / 'out'
            and not output.exists(), 'Use a fresh output directory under repository out/')
    manifest = release.check(packages, manifest_sha)
    fingerprint, version = manifest['SDK_source_fingerprint'], manifest['version']
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(('VK_', 'VVL_', 'GFX_EXPECT_VULKAN_'))}
    profiles = {}
    for profile, row in manifest['packages'].items():
        binary, inputs = build_consumers(output, profile, row, packages, manifest)
        work = output / profile
        if profile == 'd3d12':
            results = {}
            for mode in ('warp', 'hardware'):
                stdout = release.run(work / f'{mode}.log', [binary / 'd3d12_consumer.exe', '--' + mode], env=env)
                results[mode] = d3d12_result(stdout, fingerprint, version, mode == 'warp')
        else:
            transfer = release.run(work / 'transfer.log', [binary / 'vulkan_transfer.exe'], env=env)
            draw = release.run(work / 'draw.log', [binary / 'vulkan_draw.exe'], env=env)
            results = vulkan_results(transfer, draw, fingerprint)
        logs = [record_file(output, path) for path in sorted(work.glob('*.log'))]
        profiles[profile] = dict(package=row['package'], library_sha256=row['payload'][release.LIBRARY]['sha256'],
                                 consumer_inputs=inputs, logs=logs, results=results)
    release.check(packages, manifest_sha)
    receipt = dict(schema_version=1, scope='windows-candidate-offscreen-gpu-smoke', version=version,
                   source_commit=manifest['source_commit'], SDK_source_fingerprint=fingerprint,
                   manifest_sha256=manifest_sha, windows_version=platform.version(),
                   validation_requested=False, presentation_executed=False, profiles=profiles)
    (output / 'receipt.json').write_bytes((json.dumps(receipt, indent=2) + '\n').encode('utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', required=True, type=pathlib.Path)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    try:
        campaign(args.candidate.resolve(), args.manifest_sha256, args.output.resolve())
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, AuditError, zipfile.BadZipFile) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')
    print('PASS Windows installed GPU consumers: D3D12 WARP/hardware and Vulkan transfer/draw')


if __name__ == '__main__':
    main()
