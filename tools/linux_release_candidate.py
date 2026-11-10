"""Version and smoke-test an audited Ubuntu x86_64 Vulkan SDK ZIP."""

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import stat
import subprocess
import tempfile
import zipfile

from linux_comparison import ROOT, cache, read, require, sha
from run_linux_validation import EXPECTED, SUITES


PROFILE = 'linux-x86_64-vulkan-headless-release'
XCB_PROFILE = 'linux-x86_64-vulkan-xcb-release'
PACKAGE = 'GraphicsSDK-linux-verification.zip'
LIBRARY = 'lib/libGraphicsSDKCore.a'
CONFIG = 'lib/cmake/GraphicsSDK/'
NOTICE = 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt'
VERSION_FILE = CONFIG + 'GraphicsSDKConfigVersion.cmake'
DIAGNOSTICS = ('sdk-CMakeCache.txt', 'compiler-version.stdout')
FLAGS = dict(CMAKE_BUILD_TYPE='Release', CMAKE_GENERATOR='Ninja', GFX_ENABLE_D3D12='OFF',
             GFX_ENABLE_METAL='OFF', GFX_ENABLE_VULKAN='ON', GFX_VULKAN_XCB='OFF',
             GFX_BUILD_TESTS='OFF', GFX_BUILD_EXAMPLES='OFF')


def digest(value, length=64):
    return isinstance(value, str) and re.fullmatch(f'[0-9a-f]{{{length}}}', value) is not None


def record(path):
    return dict(name=path.name, bytes=path.stat().st_size, sha256=sha(path))


def check_file(path, expected):
    require(path.is_file() and not path.is_symlink() and path.stat().st_size == expected['bytes']
            and sha(path) == expected['sha256'], f'Missing or changed file: {path.name}')


def package_entries(archive, payload):
    infos = archive.infolist()
    require([i.filename for i in infos] == sorted(payload), 'Missing, extra or duplicate ZIP entry')
    for info in infos:
        row = payload[info.filename]
        require(stat.S_ISREG(info.external_attr >> 16) and info.file_size == row['bytes'],
                f'Wrong ZIP member type or size: {info.filename}')
        data = archive.read(info)
        require(hashlib.sha256(data).hexdigest() == row['sha256'], f'Changed ZIP member: {info.filename}')
        yield info.filename, data


def inspect_audit(folder, expected_sha, xcb=False):
    receipt_path = folder / 'receipt.json'
    require(digest(expected_sha) and sha(receipt_path) == expected_sha, 'Pinned audit receipt hash differs')
    receipt = read(receipt_path)
    require(receipt['schema_version'] == 1 and receipt['task'] == 'P3-LNX-12' and
            receipt['hardware_GPU_accepted'] is False and receipt['required_validation_this_run'] is False,
            'Expected a Linux software installation audit')
    system = receipt['platform']
    require(system['machine'] == 'x86_64' and system['os_release']['ID'] == 'ubuntu'
            and system['os_release']['VERSION_ID'] == '24.04', 'Expected Ubuntu 24.04 x86_64')
    commit, fingerprint = receipt['source_commit'], receipt['native_SDK_source_fingerprint']
    require(digest(commit, 40) and digest(fingerprint), 'Missing audited source identity')
    for name in DIAGNOSTICS:
        rows = [row for row in receipt['diagnostics'] if row['path'] == f'out/linux-package-evidence/{name}']
        require(len(rows) == 1, f'Expected one audited {name}')
        check_file(folder / name, rows[0])
    configured = cache(folder / DIAGNOSTICS[0])
    flags = dict(FLAGS, GFX_VULKAN_XCB='ON' if xcb else 'OFF')
    require(all(configured.get(key) == value for key, value in flags.items()) and
            pathlib.PurePosixPath(configured['CMAKE_CXX_COMPILER']).name == 'g++-14', 'Audited build profile differs')
    if xcb:
        inputs = receipt.get('actual_inputs', [])
        require(any(row['category'] == 'system' and row['path'].endswith('/xcb/xcb.h') for row in inputs) and
                any(row['category'] == 'vendor' and row['path'].endswith('/vulkan/vulkan_xcb.h') for row in inputs),
                'Audited XCB compiler inputs missing')
    compiler = (folder / DIAGNOSTICS[1]).read_text().splitlines()[0]
    require(re.match(r'g\+\+-14 \(Ubuntu .*\) 14\.', compiler), 'Audited GNU compiler differs')
    require(receipt['package']['path'] == f'out/linux-package-evidence/{PACKAGE}', 'Audit package path differs')
    package = folder / PACKAGE
    check_file(package, receipt['package'])
    names = {f'include/graphics/{p.name}' for p in (ROOT / 'include/graphics').glob('*.hpp')}
    names |= {LIBRARY, NOTICE, 'share/GraphicsSDK/LICENSE', VERSION_FILE, CONFIG + 'GraphicsSDKConfig.cmake',
              CONFIG + 'GraphicsSDKTargets.cmake', CONFIG + 'GraphicsSDKTargets-release.cmake'}
    payload = {row['path']: row for row in receipt['payload']}
    require(set(payload) == names and len(receipt['payload']) == len(names), 'Audited payload allowlist differs')
    require({key: payload[LIBRARY][key] for key in ('bytes', 'sha256')} == receipt['SDK_library']
            and payload[NOTICE]['sha256'] == receipt['notice']['sha256'], 'Audited archive or notice identity differs')
    with zipfile.ZipFile(package) as archive:
        version_text = dict(package_entries(archive, payload))[VERSION_FILE].decode()
    versions = re.findall(r'^set\(PACKAGE_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)$', version_text, re.M)
    require(len(versions) == 1, 'Audited CMake package version missing')
    version = versions[0]
    profile = XCB_PROFILE if xcb else PROFILE
    stem = f'GraphicsSDK-{version}-{profile}-{commit[:12]}-{receipt["package"]["sha256"][:12]}'
    identity = dict(profile=profile, version=version, source_commit=commit, sdk_source_fingerprint=fingerprint,
                    audit_receipt_sha256=expected_sha, audit_package_sha256=receipt['package']['sha256'],
                    compiler=compiler, build_flags=flags, release_cxx_flags=configured['CMAKE_CXX_FLAGS_RELEASE'],
                    archive_members=receipt['archive_members'], payload=receipt['payload'])
    return stem, package, payload, identity, receipt['archive_members']


def run(args, cwd, env):
    child = subprocess.run(args, cwd=cwd, env=env, stdin=subprocess.DEVNULL, capture_output=True,
                           text=True, encoding='utf-8', errors='replace', timeout=180)
    require(child.returncode == 0, f'{args[0]} failed: {(child.stdout + child.stderr)[-4000:]}')
    return child.stdout


def check_smoke(smoke, xcb=False):
    suite = SUITES['buffer']
    require(smoke['cpu'] is True and smoke['software'] == 'lavapipe' and smoke['validation'] is False
            and all(type(smoke[key]) is int for key in suite['fields']) and smoke['adapters'] == 1
            and smoke['assertions'] >= suite['minimum_assertions']
            and all(smoke[k] == v for k, v in EXPECTED['buffer'].items()), 'Installed consumer result differs')
    linked = smoke['direct_needed']
    require(isinstance(linked, list) and linked and linked == sorted(set(linked)) and
            not any(re.search(r'vulkan|VkLayer|lvp|libxcb', name, re.I) for name in linked),
            'Unexpected Vulkan/XCB dynamic link dependency')
    require(('display' in smoke) == xcb, 'Display evidence differs from the profile')
    if xcb:
        display = smoke['display']
        require(display['server'] == 'Xvfb' and type(display['presents']) is int and display['presents'] == 3
                and type(display['resizes']) is int and display['resizes'] == 1
                and display['verified_bytes'] == 3836 and display['human_display_accepted'] is False,
                'Installed XCB present or readback coverage differs')
        linked = display['direct_needed']
        require('libxcb.so.1' in linked and not any(re.search(r'vulkan|VkLayer|lvp', name, re.I) for name in linked),
                'Unexpected XCB application dynamic link dependency')


def direct_dependencies(executable, work, env):
    dynamic = run(['readelf', '-d', str(executable)], work, env)
    return sorted(re.findall(r'\(NEEDED\).*?\[([^]]+)\]', dynamic))


def inspect_and_smoke(package, payload, identity, members):
    require(platform.system() == 'Linux' and platform.machine() == 'x86_64', 'Linux x86_64 is required to build and run')
    xcb = identity['profile'] == XCB_PROFILE
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VK_', 'VVL_', 'LD_', 'GFX_EXPECT_'))}
    env['LC_ALL'] = 'C'
    with tempfile.TemporaryDirectory(prefix='graphics-release-') as temp:
        work = pathlib.Path(temp)
        prefix = work / 'relocated SDK'
        with zipfile.ZipFile(package) as archive:
            for name, data in package_entries(archive, payload):
                target = prefix / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        library = prefix / LIBRARY
        archive_members = run(['ar', 't', library.name], library.parent, env).splitlines()
        require(len(archive_members) == len(members) and set(archive_members) == set(members), 'Archive members differ')
        for member in archive_members:
            obj = subprocess.check_output(['ar', 'p', library.name, member], cwd=library.parent, env=env, timeout=30)
            # ELF64 little-endian relocatable for x86_64.
            require(obj[:6] == b'\x7fELF\x02\x01' and obj[16:20] == b'\x01\x00\x3e\x00' and
                    hashlib.sha256(obj).hexdigest() == members[member], f'Wrong archive object: {member}')
        symbols = run(['nm', '-A', '-P', '-g', library.name], library.parent, env)
        require(symbols and all(line.startswith(library.name + '[') for line in symbols.splitlines()),
                'Global symbol inventory lacks archive-relative members')
        client = work / 'independent consumer'
        client.mkdir()
        for source, name in (('tests/linux_release_consumer/CMakeLists.txt', 'CMakeLists.txt'),
                             ('tests/consumer/main.cpp', 'cpu.cpp'), ('tests/VulkanTests.cpp', 'transfer.cpp')):
            shutil.copyfile(ROOT / source, client / name)
        if xcb:
            shutil.copyfile(ROOT / 'tests/linux_release_consumer/Xcb.cpp', client / 'xcb.cpp')
        build = work / 'consumer build'
        run(['cmake', '-S', str(client), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_CXX_COMPILER=g++-14', f'-DPACKAGE_ROOT={prefix}', f'-DPACKAGE_VERSION={identity["version"]}',
             f'-DTEST_XCB={"ON" if xcb else "OFF"}'], work, env)
        run(['cmake', '--build', str(build), '--parallel', '1'], work, env)
        cpu = run([str(build / 'cpu_consumer')], work, env)
        require(f'GraphicsSDK {identity["version"]}' in cpu, 'Installed CPU version differs')
        drivers = list(pathlib.Path('/usr/share/vulkan/icd.d').glob('lvp_icd*.json'))
        require(len(drivers) == 1, 'Expected one lavapipe ICD')
        layers, xdg = work / 'empty layers', work / 'xdg'
        layers.mkdir()
        xdg.mkdir(mode=0o700)
        runtime = dict(env, VK_DRIVER_FILES=str(drivers[0]), VK_ICD_FILENAMES=str(drivers[0]), VK_LAYER_PATH=str(layers),
                       VK_LOADER_LAYERS_DISABLE='~implicit~', XDG_RUNTIME_DIR=str(xdg), LP_NUM_THREADS='2')
        executable = build / 'transfer_consumer'
        result = run([str(executable)], work, runtime)
        suite = SUITES['buffer']
        matches = re.findall(suite['pattern'], result)
        require(len(matches) == 1 and 'llvmpipe (' in result and 'software=1' in result and
                f'source={identity["sdk_source_fingerprint"]} validation=0' in result, 'Wrong installed Vulkan runtime')
        counts = dict(zip(suite['fields'], map(int, matches[0]), strict=True))
        linked = direct_dependencies(executable, work, env)
        smoke = dict(cpu=True, software='lavapipe', validation=False, direct_needed=linked, **counts)
        if xcb:
            display_client = build / 'xcb_consumer'
            result = run(['xvfb-run', '-a', '-s', '-screen 0 800x600x24', str(display_client)], work, runtime)
            report = re.fullmatch(r'PASS source=([0-9a-f]{64}) presents=(\d+) resizes=(\d+) verified_bytes=(\d+)\n',
                                  result)
            require(report and report[1] == identity['sdk_source_fingerprint'], 'Wrong installed XCB runtime')
            smoke['display'] = dict(server='Xvfb', presents=int(report[2]), resizes=int(report[3]),
                                    verified_bytes=int(report[4]), human_display_accepted=False,
                                    direct_needed=direct_dependencies(display_client, work, env))
        check_smoke(smoke, xcb)
        return symbols, smoke


def pack(stem, source, payload, identity, members, output):
    require(not output.exists(), 'Use a fresh output directory')
    symbols, smoke = inspect_and_smoke(source, payload, identity, members)
    output.mkdir(parents=True)
    package, listing = output / f'{stem}.zip', output / f'{stem}.symbols.txt'
    shutil.copyfile(source, package)
    require(sha(package) == identity['audit_package_sha256'], 'Versioned package copy changed')
    listing.write_bytes(symbols.encode())
    manifest = dict(schema_version=1, **identity, package=record(package), symbols=record(listing), smoke=smoke)
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


def check(stem, identity, output):
    manifest = read(output / 'manifest.json')
    require(manifest['schema_version'] == 1 and all(manifest[key] == value for key, value in identity.items()),
            'Release manifest differs from the pinned audit')
    require(manifest['package']['name'] == f'{stem}.zip' and manifest['symbols']['name'] == f'{stem}.symbols.txt',
            'Release file name differs from the profile')
    require({p.name for p in output.iterdir()} == {'manifest.json', manifest['package']['name'], manifest['symbols']['name']},
            'Unexpected release output entries')
    package = output / manifest['package']['name']
    check_file(package, manifest['package'])
    require(manifest['package']['sha256'] == identity['audit_package_sha256'], 'Release ZIP differs from the pinned audit')
    check_file(output / manifest['symbols']['name'], manifest['symbols'])
    check_smoke(manifest['smoke'], identity['profile'] == XCB_PROFILE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('pack', 'check'))
    parser.add_argument('--profile', choices=('headless', 'xcb'), default='headless')
    parser.add_argument('--audit', required=True, type=pathlib.Path)
    parser.add_argument('--audit-sha256', required=True)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    args = parser.parse_args()
    stem, source, payload, identity, members = inspect_audit(args.audit, args.audit_sha256, args.profile == 'xcb')
    if args.action == 'pack':
        pack(stem, source, payload, identity, members, args.output)
    check(stem, identity, args.output)
    print(f'PASS {stem} ({len(payload)} audited ZIP members)')


if __name__ == '__main__':
    main()
