"""Build and verify separate Windows x64 Release SDK candidates."""

import argparse
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import zipfile

import audit_licenses as audit
from audit_linux_package import notice, package_zip, require, sha
from audit_vulkan import system_input
from linux_release_candidate import check_file, digest, package_entries, record


ROOT = audit.ROOT
PROFILES = ('d3d12', 'vulkan')
LIBRARY = 'lib/GraphicsSDKCore.lib'
LICENSE = 'share/GraphicsSDK/LICENSE'
NOTICE = 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt'
CONFIG = 'lib/cmake/GraphicsSDK/'
CONFIGS = ('GraphicsSDKConfig.cmake', 'GraphicsSDKConfigVersion.cmake', 'GraphicsSDKTargets.cmake',
           'GraphicsSDKTargets-release.cmake')
MANIFEST = 'manifest.json'
PRODUCT = ('CMakeLists.txt', 'cmake', 'include', 'src', 'LICENSE')
MSVC = ('-G', 'Visual Studio 17 2022', '-A', 'x64', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL')
BUILD_OVERRIDES = {'CL', '_CL_', 'LINK', '_LINK_', 'CXX', 'CXXFLAGS', 'LDFLAGS', 'CMAKE_TOOLCHAIN_FILE'}


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True, encoding='utf-8', timeout=30).strip()


def source_identity():
    require(not git('status', '--porcelain', '--untracked-files=all', '--', *PRODUCT)
            and not git('ls-files', '--others', '--ignored', '--exclude-standard', '--', *PRODUCT),
            'SDK source and install inputs must match HEAD')
    return dict(source_commit=git('rev-parse', 'HEAD'), SDK_source_fingerprint=audit.sdk_fingerprint(ROOT))


def run(log, command, failure=None):
    env = {key: value for key, value in os.environ.items() if key.upper() not in BUILD_OVERRIDES}
    child = subprocess.run(list(map(str, command)), cwd=log.parent, stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, timeout=300)
    log.write_bytes(child.stdout)
    stdout = child.stdout.decode(errors='replace')
    if failure is None:
        require(child.returncode == 0, f'{log.name} failed:\n{stdout[-4000:]}')
    else:
        require(child.returncode != 0 and ' (find_package):' in stdout and failure in stdout,
                f'Expected find_package rejection in {log}')
    return stdout


def trace(directory, name):
    logs = list(directory.rglob(name))
    require(len(logs) == 1, f'Expected one MSBuild {name} in {directory}')
    return {pathlib.Path(path).resolve() for path in audit.parse_tlog(logs[0].read_bytes())}


def reviewed_vulkan_headers(paths, policy, tracked):
    dependency = policy['dependency']
    directory = ROOT / dependency['directory']
    expected = {row['path'].casefold(): row for row in dependency['reviewed_headers']}
    actual = {p.relative_to(directory).as_posix().casefold() for p in paths if p.is_relative_to(directory)}
    require(actual == expected.keys(), 'Windows Vulkan compiler-read header set differs from policy')
    headers = []
    for key in sorted(actual):
        row = expected[key]
        path = directory / row['path']
        require(key in tracked and sha(path) == row['sha256'], f'Unreviewed Vulkan header: {row["path"]}')
        headers.append(path)
    apache = directory / 'LICENSES/Apache-2.0.txt'
    require('licenses/apache-2.0.txt' in tracked
            and sha(apache) == sha(ROOT / 'licenses/provenance/vulkan-headers-Apache-2.0.txt'),
            'Vulkan Apache notice differs from the pinned text')
    return notice(ROOT, headers, apache)


def compiler_inputs(paths, own, target_dir, vendor=()):
    for path in paths:
        if path in own or path in vendor or system_input(path):
            continue
        if path.is_relative_to(target_dir) and path.suffix.casefold() in ('.obj', '.pdb', '.tlog'):
            continue
        raise ValueError(f'Unreviewed compiler input: {path}')
    require(paths & own, 'No compiler-read evidence for project files')


def installed_payload(prefix, built_library, license_notice):
    expected = {LIBRARY: built_library, LICENSE: ROOT / 'LICENSE'}
    expected.update({p.relative_to(ROOT).as_posix(): p for p in (ROOT / 'include/graphics').glob('*.hpp')})
    files = list(prefix.rglob('*'))
    require(not any(p.is_symlink() or p.is_junction() for p in files), 'Package contains a link')
    require({p.relative_to(prefix).as_posix() for p in files if p.is_file()} == set(expected) | {CONFIG + p for p in CONFIGS},
            'Unexpected or missing installed files')
    for name, source in expected.items():
        require(sha(prefix / name) == sha(source), f'Installed bytes differ: {name}')
    if license_notice is not None:
        (prefix / NOTICE).write_bytes(license_notice.encode('utf-8'))
    return {p.relative_to(prefix).as_posix(): dict(bytes=p.stat().st_size, sha256=sha(p))
            for p in sorted(prefix.rglob('*')) if p.is_file()}


def unpack(zip_path, payload, prefix):
    with zipfile.ZipFile(zip_path) as archive:
        for name, data in package_entries(archive, payload):
            # Names come from the fixed install inventory checked before packing.
            dest = prefix / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)


def smoke(work, profile, version, fingerprint, prefix, original):
    source = work / 'external consumer'
    shutil.copytree(ROOT / 'tests/windows_release_consumer', source)
    build = work / 'consumer-build'
    cmake = ['cmake', '-S', source, *MSVC]
    run(work / 'consumer-configure.log', [*cmake, '-B', build, f'-DPACKAGE_ROOT={prefix.as_posix()}',
                                         f'-DPACKAGE_VERSION={version}'])
    run(work / 'consumer-wrong-version.log',
        [*cmake, '-B', work / 'wrong-version', f'-DPACKAGE_ROOT={prefix.as_posix()}', '-DPACKAGE_VERSION=999.0.0'],
        'requested version "999.0.0"')
    run(work / 'consumer-missing.log',
        [*cmake, '-B', work / 'missing', f'-DPACKAGE_ROOT={(work / "absent SDK").as_posix()}',
         f'-DCMAKE_PREFIX_PATH={original.as_posix()}', f'-DPACKAGE_VERSION={version}'],
        'Could not find a package configuration file')
    run(work / 'consumer-build.log', ['cmake', '--build', build, '--config', 'Release', '--parallel', '1'])
    target = build / 'consumer.dir/Release'
    paths = trace(target, 'CL.read.1.tlog')
    headers = set((prefix / 'include/graphics').glob('*.hpp'))
    compiler_inputs(paths, headers | {source / 'main.cpp'}, target)
    require(prefix / 'include/graphics/Graphics.hpp' in paths, 'Consumer did not read the relocated SDK header')
    libraries = {p for p in trace(target, 'link.read.1.tlog') if p.suffix.casefold() == '.lib'}
    require(prefix / LIBRARY in libraries and all(system_input(p) for p in libraries - {prefix / LIBRARY}),
            'Consumer did not link the relocated SDK and reviewed system libraries')
    result = run(work / 'consumer-run.log', [build / 'Release/consumer.exe', profile, version, fingerprint])
    require(result.strip() == f'PASS {profile} {version} {fingerprint}', 'Consumer identity differs')
    return dict(installed_headers=len(paths & headers), linked_system_libraries=sorted(p.name.lower()
                for p in libraries - {prefix / LIBRARY}), exact_version=True, wrong_version_rejected=True,
                missing_prefix_rejected=True, CPU_executed=True, GPU_executed=False)


def build_profile(work, staging, profile, identity, own):
    work.mkdir()
    build, install = work / 'sdk-build', work / 'sdk-install'
    vulkan = profile == 'vulkan'
    policy = json.loads(audit.text_file(ROOT / 'licenses/vulkan-presentation-policy.json'))
    dependency = policy['dependency']
    headers = ROOT / dependency['directory']
    tracked = audit.verify_checkout(ROOT, 'vulkan', dependency['commit'],
                                    {'vulkan': dependency['directory']}) if vulkan else None
    flags = ['-DGFX_BUILD_TESTS=OFF', '-DGFX_BUILD_EXAMPLES=OFF', '-DGFX_ENABLE_METAL=OFF',
             '-DGFX_VULKAN_XCB=OFF', '-DGFX_DXC_INCLUDE_DIR=',
             f'-DGFX_ENABLE_D3D12={"OFF" if vulkan else "ON"}', f'-DGFX_ENABLE_VULKAN={"ON" if vulkan else "OFF"}']
    if vulkan:
        flags.append(f'-DGFX_VULKAN_HEADERS={headers}')
    configured = run(work / 'sdk-configure.log', ['cmake', '-S', ROOT, '-B', build, *MSVC, *flags])
    compiler = re.search(r'CXX compiler identification is MSVC ([0-9.]+)', configured)
    windows_sdk = re.search(r'Selecting Windows SDK version ([0-9.]+)', configured)
    require(compiler and windows_sdk, 'Missing MSVC or Windows SDK identity')
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    version, = re.findall(r'^CMAKE_PROJECT_VERSION:STATIC=(\d+\.\d+\.\d+)$', cache, re.MULTILINE)
    run(work / 'sdk-build.log', ['cmake', '--build', build, '--config', 'Release', '--parallel', '1'])
    run(work / 'sdk-install.log', ['cmake', '--install', build, '--config', 'Release', '--prefix', install])

    target = build / 'GraphicsSDKCore.dir/Release'
    paths = trace(target, 'CL.read.1.tlog')
    license_notice = reviewed_vulkan_headers(paths, policy, tracked) if vulkan else None
    vendor = {headers / row['path'] for row in dependency['reviewed_headers']} if vulkan else set()
    compiler_inputs(paths, own, target, vendor)
    expected_backend = ROOT / ('src/backends/vulkan/Vulkan.cpp' if vulkan else 'src/backends/d3d12/D3D12.cpp')
    require(expected_backend in paths, 'Missing compiler-read evidence for the selected backend')
    library = build / 'Release/GraphicsSDKCore.lib'
    payload = installed_payload(install, library, license_notice)
    temporary_zip = work / 'sdk.zip'
    package_zip(install, temporary_zip, [dict(path=name, **row) for name, row in sorted(payload.items())])
    zip_path = staging / candidate_name(version, profile, identity['source_commit'], sha(temporary_zip))
    temporary_zip.rename(zip_path)
    prefix = work / 'relocated SDK'
    unpack(zip_path, payload, prefix)
    result = smoke(work, profile, version, identity['SDK_source_fingerprint'], prefix, install)
    return dict(version=version, MSVC=compiler[1], windows_sdk=windows_sdk[1], CRT='MD'), dict(
        package=record(zip_path), payload=payload, installed_consumer=result, compiled_vendor_headers=len(paths & vendor),
        vulkan_headers_commit=dependency['commit'] if vulkan else None)


def candidate_name(version, profile, source, zip_sha):
    return f'GraphicsSDK-{version}-windows-x64-{profile}-release-{source[:12]}-{zip_sha[:12]}.zip'


def check(output, manifest_sha):
    path = output / MANIFEST
    require(digest(manifest_sha) and path.is_file() and not path.is_symlink() and sha(path) == manifest_sha,
            'Pinned candidate manifest SHA-256 differs')
    manifest = json.loads(path.read_bytes())
    require(manifest['schema_version'] == 1 and manifest['platform'] == 'windows-x64'
            and manifest['configuration'] == 'Release' and set(manifest['packages']) == set(PROFILES)
            and digest(manifest['source_commit'], 40) and digest(manifest['SDK_source_fingerprint'])
            and re.fullmatch(r'\d+\.\d+\.\d+', manifest['version']),
            'Unexpected candidate identity')
    expected = {MANIFEST}
    for profile, row in manifest['packages'].items():
        payload = row['payload']
        zip_sha = row['package']['sha256']
        require(digest(zip_sha), 'Invalid ZIP digest')
        name = candidate_name(manifest['version'], profile, manifest['source_commit'], zip_sha)
        require(row['package']['name'] == name, 'Unexpected ZIP name')
        expected.add(name)
        check_file(output / name, row['package'])
        with zipfile.ZipFile(output / name) as archive:
            for _ in package_entries(archive, payload):
                pass
    require({p.name for p in output.iterdir()} == expected, 'Unexpected candidate files')
    return manifest


def build(output):
    require(platform.system() == 'Windows', 'A native Windows build environment is required')
    require(output.is_relative_to(ROOT / 'out') and output != ROOT / 'out', 'Use a fresh path under repository out/')
    work = output.with_name(output.name + '-work')
    require(not output.exists() and not work.exists(), 'Expected fresh candidate and work directories')
    staging = work / 'candidate'
    staging.mkdir(parents=True)
    identity = source_identity()
    own = {ROOT / path for path in git('ls-files', '-z', '--', 'include', 'src').split('\0') if path}
    metadata, packages = {}, {}
    for profile in PROFILES:
        metadata[profile], packages[profile] = build_profile(work / profile, staging, profile, identity, own)
    require(metadata['d3d12'] == metadata['vulkan'] and identity == source_identity(),
            'SDK source or Windows toolchain identity differs between profiles')
    manifest = dict(schema_version=1, platform='windows-x64', configuration='Release',
                    **identity, **metadata['d3d12'], packages=packages)
    path = staging / MANIFEST
    path.write_bytes((json.dumps(manifest, indent=2) + '\n').encode())
    manifest_sha = sha(path)
    check(staging, manifest_sha)
    staging.rename(output)
    return manifest_sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'check'))
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--manifest-sha256', required=False)
    args = parser.parse_args()
    if (args.action == 'check') != (args.manifest_sha256 is not None):
        parser.error('--manifest-sha256 is required only for check')
    try:
        if args.action == 'build':
            print('PASS Windows candidates manifest_sha256=' + build(args.output.resolve()))
        else:
            check(args.output.resolve(), args.manifest_sha256)
            print('PASS Windows candidate manifest and both ZIPs')
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, audit.AuditError, zipfile.BadZipFile) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')


if __name__ == '__main__':
    main()
