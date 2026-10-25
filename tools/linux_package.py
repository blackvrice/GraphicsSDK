"""P3-LNX-12: actual GNU inputs and a relocated, installed Linux SDK consumer."""
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time

import audit_linux_package as audit
from audit_licenses import sdk_fingerprint
from run_linux_device import ROOT, HEADERS, require
from run_linux_validation import SUITES, EXPECTED


def main():
    require(platform.system() == 'Linux', 'Actual Linux host required')
    output = ROOT / 'out/linux-package-evidence'
    build = ROOT / 'out/linux-package-sdk'
    retired = ROOT / 'out/linux-package-sdk-retired'
    install = ROOT / 'out/linux-package-install'
    prefix = ROOT / 'out/linux-package-relocated'
    require(all(not p.exists() for p in (output, build, retired, install, prefix)), 'Fresh roots required')
    output.mkdir(parents=True)
    empty = output / 'empty-layers'
    empty.mkdir()
    xdg = output / 'xdg-runtime'
    xdg.mkdir(mode=0o700)
    drivers = sorted(pathlib.Path('/usr/share/vulkan/icd.d').glob('lvp_icd*.json'))
    require(len(drivers) == 1, 'One observed lavapipe ICD required')
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VK_', 'VVL_', 'GFX_EXPECT_VULKAN_', 'LD_'))}
    overrides = dict(VK_DRIVER_FILES=str(drivers[0]), VK_ICD_FILENAMES=str(drivers[0]), VK_LAYER_PATH=str(empty), VK_LOADER_LAYERS_DISABLE='~implicit~', XDG_RUNTIME_DIR=str(xdg), LP_NUM_THREADS='2')
    env.update(overrides)
    commands = []

    def run(name, args, cwd=ROOT, extra=None):
        start = time.monotonic()
        child = subprocess.run(list(map(str, args)), cwd=cwd, env={**env, **(extra or {})}, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
        (output / (name + '.stdout')).write_bytes(child.stdout)
        (output / (name + '.stderr')).write_bytes(child.stderr)
        commands.append(dict(name=name, argv=list(map(str, args)), cwd=str(cwd), returncode=child.returncode, seconds=time.monotonic() - start, environment=extra or {}))
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if child.returncode:
            print((child.stdout + child.stderr).decode(errors='replace')[-20000:], flush=True)
        require(child.returncode == 0, name + ' failed')
        print(name + ' PASS', flush=True)
        return child.stdout.decode()

    commit = run('source-commit', ['git', 'rev-parse', 'HEAD']).strip()
    require(commit == os.environ.get('GITHUB_SHA', commit), 'CI source differs')
    require(not run('source-status', ['git', 'status', '--porcelain', '--untracked-files=no']).strip(), 'Tracked files changed')
    headers_root = ROOT / '.deps/Vulkan-Headers'
    require(run('headers-commit', ['git', '-C', headers_root, 'rev-parse', 'HEAD']).strip() == HEADERS, 'Header pin differs')
    require(not run('headers-status', ['git', '-C', headers_root, 'status', '--porcelain']).strip(), 'Headers changed')
    policy = json.loads((ROOT / 'licenses/linux-vulkan-policy.json').read_text())
    own = {(ROOT / p).resolve() for p in policy['own_compiler_inputs']}
    vendor = {(headers_root / row['path']).resolve(): row['sha256'] for row in policy['dependency']['reviewed_headers']}
    require(policy['dependency']['commit'] == HEADERS, 'Policy pin differs')
    for path, digest in vendor.items():
        require(audit.sha(path) == digest, 'Reviewed Vulkan header changed')
    fp = sdk_fingerprint(ROOT)
    run('compiler-version', ['g++-14', '--version'])
    run('linker-version', ['ld', '--version'])
    run('packages', ['dpkg-query', '-W', '-f=${Package} ${Version}\n', 'g++-14', 'binutils', 'libvulkan1', 'mesa-vulkan-drivers', 'vulkan-tools'])
    run('vulkaninfo', ['vulkaninfo', '--summary'])
    captured = {}
    input_inventory = {}

    def capture(kind, directory, installed, copied):
        rows = []
        for path in sorted(directory.glob('*.json')):
            row = json.loads(path.read_text())
            require(row['returncode'] == 0, 'Captured command failed')
            require(audit.sha(row['output']) == row['output_sha256'], 'Captured output changed')
            if row['mode'] == 'compile':
                dep = path.with_suffix('.dep')
                require(audit.sha(dep) == row['dependency_sha256'], 'Captured depfile changed')
                paths = audit.dependencies(dep.read_text(), row['cwd'])
                inventory = audit.audit_dependencies(paths, kind, own, installed, copied, vendor)
                for item in inventory:
                    input_inventory[item['path']] = item
                row['inputs'] = [item['path'] for item in inventory]
            rows.append(row)
        require(rows, 'No actual build commands captured')
        captured[kind] = rows
        return rows

    launcher = str(ROOT / 'tools/linux_build_capture.py')

    def launchers(directory):
        return ['-DCMAKE_CXX_COMPILER_LAUNCHER=' + ';'.join((sys.executable, launcher, 'compile', str(directory))), '-DCMAKE_CXX_LINKER_LAUNCHER=' + ';'.join((sys.executable, launcher, 'link', str(directory)))]

    config = ['cmake', '--preset', 'linux-vulkan', '-B', build, '-DCMAKE_INSTALL_PREFIX=' + str(install), '-DCMAKE_CXX_COMPILER=g++-14', '-DGFX_BUILD_TESTS=OFF', '-DGFX_BUILD_EXAMPLES=OFF', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    run('configure-sdk', config)
    run('configure-sdk-capture', [*config, *launchers(output / 'sdk-capture')])
    run('build-sdk', ['cmake', '--build', build, '--parallel', '1', '--verbose'])
    rows = capture('sdk', output / 'sdk-capture', set(), set())
    require(len(rows) == 6 and all(r['mode'] == 'compile' for r in rows), 'Exact six SDK compilation units required')
    actual_vendor = {pathlib.Path(p) for row in rows for p in row['inputs'] if pathlib.Path(p) in vendor}
    require(actual_vendor == set(vendor), 'Actual Vulkan header set differs from reviewed policy')
    library = build / 'libGraphicsSDKCore.a'
    library_sha = audit.sha(library)
    members = run('archive-members', ['ar', 't', library]).splitlines()
    require(len(members) == len(set(members)) == 6, 'Archive members differ')
    member_hashes = {}
    for member in members:
        data = subprocess.check_output(['ar', 'p', str(library), member])
        member_hashes[member] = hashlib.sha256(data).hexdigest()
    require(sorted(member_hashes.values()) == sorted(r['output_sha256'] for r in rows), 'Archive objects differ from actual compiled objects')
    run('install-sdk', ['cmake', '--install', build])
    apache = headers_root / 'LICENSES/Apache-2.0.txt'
    notices = audit.notice(ROOT, actual_vendor, apache)
    (install / 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt').write_bytes(notices.encode())
    (output / 'THIRD_PARTY_NOTICES.txt').write_bytes(notices.encode())
    headers = sorted((ROOT / 'include/graphics').glob('*.hpp'))
    require(len(headers) == 13, 'Public header allowlist differs')
    audit.payload(install, headers, notices, library_sha)
    for source, destination in ((install, prefix), (build, retired)):
        require(source.resolve().is_relative_to(ROOT / 'out') and destination.resolve().is_relative_to(ROOT / 'out') and not destination.exists(), 'Unsafe relocation')
        source.rename(destination)
    require(not install.exists() and not build.exists(), 'Original prefixes still exist')
    for config_file in (prefix / 'lib/cmake/GraphicsSDK').glob('*.cmake'):
        text = config_file.read_text()
        require(all(str(p) not in text for p in (ROOT, build, install, retired)), 'Non-relocatable exported CMake file')
    installed = {(prefix / 'include/graphics' / h.name).resolve() for h in headers}
    external = pathlib.Path(tempfile.mkdtemp(prefix='graphics-sdk-consumer-')).resolve()
    require(not external.is_relative_to(ROOT), 'Consumer must be outside the repository')
    copied = set()
    copies = {}
    client_files = ['tests/consumer/main.cpp']
    client_files += ['tests/' + n + '.cpp' for n in ('VulkanTests', 'VulkanTextureTests', 'VulkanRenderPassTests', 'VulkanDrawTests', 'VulkanVertexTests', 'VulkanBindingTests', 'VulkanDepthTests', 'VulkanTimestampTests')]
    client_files += ['tests/' + n for n in ('VulkanVertexSupport.hpp', 'VulkanBindingSupport.hpp', 'VulkanDepthSupport.hpp')]
    client_files += ['tests/fixtures/' + n + '.hpp' for n in ('SpirvFixtures', 'VertexFixtures', 'BindingFixtures', 'DepthFixtures')]
    for name in client_files:
        source = ROOT / name
        destination = external / pathlib.Path(name).relative_to('tests')
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        require(audit.sha(source) == audit.sha(destination), 'External client copy changed')
        copied.add(destination)
        copies[str(destination)] = dict(source=name, sha256=audit.sha(source))
    shutil.copyfile(ROOT / 'tests/linux_package_consumer/CMakeLists.txt', external / 'CMakeLists.txt')
    client_build = external / 'build'
    config = ['cmake', '-S', external, '-B', client_build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=g++-14', '-DCMAKE_PREFIX_PATH=' + str(prefix), '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    run('configure-consumer', config)
    run('configure-consumer-capture', [*config, *launchers(output / 'consumer-capture')])
    run('build-consumer', ['cmake', '--build', client_build, '--parallel', '1', '--verbose'])
    rows = capture('consumer', output / 'consumer-capture', installed, copied)
    compiles = [r for r in rows if r['mode'] == 'compile']
    links = [r for r in rows if r['mode'] == 'link']
    require(len(compiles) == len(links) == 9, 'Nine external compile/link commands required')
    objects = {pathlib.Path(r['output']).resolve() for r in compiles}
    link_inputs = set()
    archive = (prefix / 'lib/libGraphicsSDKCore.a').resolve()
    for path in sorted((output / 'consumer-capture').glob('*.map')):
        trace = path.with_suffix('.stdout').read_text().splitlines()
        actual = set()
        for line in trace:
            if not line.strip():
                continue
            match = re.fullmatch(r'\(([^)]+)\)(.+)', line)
            value = match[1] if match else line
            candidate = (client_build / value).resolve()
            require(candidate.is_file(), 'Unrecognized GNU linker trace: ' + line)
            require(candidate in objects or candidate == archive or audit.classify(candidate, set(), set(), set(), {}) == 'system', 'Unexpected linker input')
            actual.add(candidate)
        require(archive in actual and len(actual & objects) == 1, 'Linker did not use relocated SDK and one client object')
        require('libGraphicsSDKCore.a(' in path.read_text(), 'Archive member extraction missing from GNU map')
        link_inputs |= actual
    require(len(list((output / 'consumer-capture').glob('*.map'))) == 9, 'Nine actual GNU link maps required')
    for path in sorted(link_inputs):
        input_inventory[str(path)] = dict(path=str(path), category='installed_archive' if path == archive else 'client_object' if path in objects else 'system_link', bytes=path.stat().st_size, sha256=audit.sha(path))
    # System inputs stay external; preserve exact owning distribution package records.
    system = sorted(p for p, r in input_inventory.items() if r['category'].startswith('system'))
    owners = []
    for filename in system:
        aliases = [filename]
        if filename.startswith('/usr/lib/'):
            aliases.append(filename.removeprefix('/usr'))
        attempts = []
        for alias in aliases:
            child = subprocess.run(['dpkg-query', '-S', alias], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
            attempts.append(dict(query=alias, returncode=child.returncode, stdout=child.stdout.decode(), stderr=child.stderr.decode()))
            if child.returncode == 0:
                break
        require(child.returncode == 0 and child.stdout.strip(), 'Unowned system input: ' + filename)
        owners.append(dict(path=filename, attempts=attempts))
    (output / 'system-input-owners.json').write_text(json.dumps(owners, indent=2) + '\n')
    cpu = run('installed-cpu', [client_build / 'cpu_consumer'])
    require('GraphicsSDK 0.0.1' in cpu, 'Generic CPU consumer output missing')
    results = []
    for name, suite in SUITES.items():
        target = ('render' if name == 'color-pass' else name) + '_consumer'
        executable = client_build / target
        stdout = run('installed-' + name, [executable], extra={'LD_DEBUG': 'libs'})
        require('source=' + fp + ' validation=0' in stdout, 'Unexpected source/validation mode')
        matches = re.findall(suite['pattern'], stdout)
        require(len(matches) == 1, 'Suite result missing')
        counts = dict(zip(suite['fields'], map(int, matches[0])))
        require(counts['adapters'] == 1 and counts['assertions'] >= suite['minimum_assertions'] and all(counts[k] == v for k, v in EXPECTED[name].items()), 'Suite workload differs')
        require('llvmpipe (' in stdout, 'Unexpected runtime adapter')
        debug = (output / ('installed-' + name + '.stderr')).read_text()
        require('calling init:' in debug and 'libvulkan_lvp.so' in debug and 'libVkLayer_khronos_validation.so' not in debug, 'Actual driver-only runtime differs')
        dynamic = run('elf-' + name, ['readelf', '-d', executable])
        require(not re.search(r'NEEDED.*(?:vulkan|VkLayer|lvp)', dynamic, re.I), 'External runtime statically imported into client')
        results.append(dict(suite=name, **counts, executable_sha256=audit.sha(executable), validation=False))
    inventory = audit.payload(prefix, headers, notices, library_sha)
    require((prefix / 'share/GraphicsSDK/LICENSE').read_bytes() == (ROOT / 'LICENSE').read_bytes(), 'Own MIT license changed')
    for name, source in (('sdk', retired), ('consumer', client_build)):
        for filename in ('CMakeCache.txt', 'compile_commands.json'):
            shutil.copyfile(source / filename, output / (name + '-' + filename))
    package = output / 'GraphicsSDK-linux-verification.zip'
    audit.package_zip(prefix, package, inventory)
    require(sdk_fingerprint(ROOT) == fp and audit.sha(retired / 'libGraphicsSDKCore.a') == library_sha, 'SDK changed during acceptance')
    source_paths = sorted(set(policy['own_compiler_inputs'] + client_files + ['CMakeLists.txt', 'CMakePresets.json', 'cmake/GraphicsSDKConfig.cmake.in', 'LICENSE', 'licenses/linux-vulkan-policy.json', 'tools/linux_package.py', 'tools/linux_build_capture.py', 'tools/audit_linux_package.py', 'tools/audit_licenses.py', 'tests/test_linux_package_audit.py', 'tests/linux_package_consumer/CMakeLists.txt', '.github/workflows/linux-vulkan-package.yml'] + ['tools/run_linux_' + n + '.py' for n in ('validation', 'device', 'draw', 'input', 'depth', 'timestamp')]))
    def record(path):
        return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size, sha256=audit.sha(path))
    receipt = dict(schema_version=1, task='P3-LNX-12', source_commit=commit, github_run_id=os.environ.get('GITHUB_RUN_ID'), native_SDK_source_fingerprint=fp,
        platform=dict(os_release=platform.freedesktop_os_release(), kernel=platform.release(), machine=platform.machine()), environment=overrides,
        external_project=str(external), original_build_absent=not build.exists(), original_install_absent=not install.exists(), relocated_prefix=str(prefix),
        SDK_library=dict(bytes=(prefix / 'lib/libGraphicsSDKCore.a').stat().st_size, sha256=library_sha), archive_members=member_hashes,
        captured_commands=captured, actual_inputs=sorted(input_inventory.values(), key=lambda r:r['path']), external_client_copies=copies,
        reviewed_headers=policy['dependency'], apache_license=dict(sha256=audit.sha(apache), bytes=apache.stat().st_size),
        payload=inventory, package=record(package), notice=record(output / 'THIRD_PARTY_NOTICES.txt'), results=results,
        cpu_consumer='version/binding/render-target CPU contracts; D3D12-specific branch not executed',
        hardware_GPU_accepted=False, required_validation_this_run=False, notice_added_by='post-install audit packaging step, not plain cmake --install',
        inputs=[record(ROOT / p) for p in source_paths], diagnostics=[record(p) for p in sorted(output.rglob('*')) if p.is_file()])
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS relocated external consumers=9 Vulkan_assertions=' + str(sum(r['assertions'] for r in results)) + ' payload_files=' + str(len(inventory)), flush=True)


if __name__ == '__main__':
    main()
