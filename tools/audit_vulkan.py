"""Fail-closed audit/package of Windows x64 Release Vulkan variants.

Other historical SDK/benchmark inventories remain snapshots of their own builds.
This profile consumes actual MSBuild read logs; it does not infer symbol reachability.
"""
import argparse
import json
import pathlib
import subprocess
import zipfile

import audit_licenses as audit

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = pathlib.Path('out/vulkan-transfer')
CONSUMER = pathlib.Path('out/vulkan-consumer')
INSTALL = pathlib.Path('out/vulkan-install')
POLICY = pathlib.Path('licenses/vulkan-policy.json')
PROFILE = 'windows-sdk-vulkan-transfer-release'
EXPECTED_LOGS = (17, 16)
EXCLUSIONS = ['Linux native builds/runtime', 'Vulkan validation-layer execution', 'Vulkan rendering and competitor measurements',
              'Historical D3D-only binaries/packages/benchmarks, preserved independently']

def system_input(path):
    name = path.as_posix().casefold()
    return name.startswith(('c:/program files/microsoft visual studio/', 'c:/program files (x86)/windows kits/',
                            'c:/program files/windows kits/')) or name in (
        'c:/windows/globalization/sorting/sortdefault.nls', 'c:/windows/system32/tzres.dll')

def collect(root):
    policy = json.loads(audit.text_file(root / POLICY))
    headers = root / policy['dependency']['directory']
    commit = policy['dependency']['commit']
    tracked = audit.verify_checkout(root, 'vulkan', commit, {'vulkan': policy['dependency']['directory']})
    cache = audit.text_file(root / BUILD / 'CMakeCache.txt')
    for option in ('GFX_ENABLE_D3D12:BOOL=ON', 'GFX_ENABLE_VULKAN:BOOL=ON', 'CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022'):
        if option not in cache.splitlines():
            raise audit.AuditError(f'Wrong build configuration: {option}')
    if PROFILE in ('windows-sdk-vulkan-validation-release', 'windows-sdk-vulkan-textures-release', 'windows-sdk-vulkan-color-pass-release', 'windows-sdk-vulkan-draw-release') and 'GFX_VULKAN_VALIDATION_TESTS:BOOL=ON' not in cache.splitlines():
        raise audit.AuditError('Required Vulkan validation tests were not configured')
    configured_headers = next((s.split('=', 1)[1] for s in cache.splitlines() if s.startswith('GFX_VULKAN_HEADERS:PATH=')), '')
    if pathlib.Path(configured_headers).resolve() != headers.resolve():
        raise audit.AuditError('Vulkan build does not use the reviewed header checkout')
    fingerprint = audit.sdk_fingerprint(root)
    library = root / BUILD / 'Release/GraphicsSDKCore.lib'
    if fingerprint.encode() not in library.read_bytes():
        raise audit.AuditError('Vulkan SDK static library is stale')
    if (root / INSTALL / 'lib/GraphicsSDKCore.lib').read_bytes() != library.read_bytes():
        raise audit.AuditError('Installed Vulkan library differs from build')
    own = {name.casefold(): name for name in policy['own_compiler_inputs']}
    reviewed_headers = {r['path'].casefold(): r for r in policy['dependency']['reviewed_headers']}
    paths, compiler_logs, link_logs, artifacts = set(), [], [], []
    projects = {}
    for folder in (BUILD, CONSUMER):
        for log in sorted((root / folder).rglob('CL.read.1.tlog')):
            if 'Release' not in log.parts:
                continue
            compiler_logs.append(audit.record_file(root, log)); paths.update(audit.parse_tlog(log.read_bytes()))
        for p in sorted((root / folder / 'Release').iterdir()):
            if p.suffix.casefold() in ('.exe', '.lib'):
                artifacts.append(audit.record_file(root, p))
        for project in sorted((root / folder).glob('*.vcxproj')):
            if project.stem not in ('ALL_BUILD', 'ZERO_CHECK', 'INSTALL'):
                projects[project.relative_to(root).as_posix()] = audit.link_evidence(root, project.relative_to(root).as_posix())
        for log in sorted((root / folder).rglob('link.read.1.tlog')):
            if 'Release' not in log.parts:
                continue
            libraries = []
            for name in audit.parse_tlog(log.read_bytes()):
                if not name.casefold().endswith('.lib'):
                    continue
                p = pathlib.Path(name).resolve()
                if p not in (library.resolve(), (root / INSTALL / 'lib/GraphicsSDKCore.lib').resolve()) and not system_input(p):
                    raise audit.AuditError(f'Unreviewed linked library: {name}')
                libraries.append(dict(path=p.as_posix(), sha256=audit.sha256(p.read_bytes()), system_prerequisite=system_input(p)))
            if not libraries:
                raise audit.AuditError(f'Missing linker library evidence: {log}')
            link_logs.append(dict(**audit.record_file(root, log), libraries=libraries))
    if (len(compiler_logs), len(link_logs)) != EXPECTED_LOGS:
        raise audit.AuditError('Missing/unreviewed build targets in compiler/linker evidence')
    files, system = {}, set()
    notices = set()
    for raw in sorted(paths, key=str.casefold):
        p = pathlib.Path(raw).resolve()
        if p.suffix.casefold() in ('.obj', '.pch', '.pdb', '.tlog'):
            continue
        if p.is_relative_to(headers):
            relative = p.relative_to(headers).as_posix()
            reviewed = reviewed_headers.get(relative.casefold())
            if relative.casefold() not in tracked or reviewed is None or audit.sha256(p.read_bytes()) != reviewed['sha256']:
                raise audit.AuditError(f'Unreviewed Vulkan header: {relative}')
            text = audit.text_file(p)
            if 'SPDX-License-Identifier: Apache-2.0\n' not in text:
                raise audit.AuditError(f'Header license changed: {relative}')
            notices.update(audit.source_notices(text))
            files[p.relative_to(root).as_posix()] = dict(**audit.record_file(root, p), component='vulkan-headers',
                source_url=f'https://github.com/KhronosGroup/Vulkan-Headers/blob/{commit}/{relative}')
        elif p.is_relative_to(root):
            relative = p.relative_to(root).as_posix()
            if p.is_relative_to(root / INSTALL / 'include/graphics'):
                source = root / 'include/graphics' / p.name
                if not source.is_file() or p.read_bytes() != source.read_bytes():
                    raise audit.AuditError(f'Unreviewed installed header: {relative}')
            elif relative.casefold() not in own:
                raise audit.AuditError(f'Unreviewed project compiler input: {relative}')
            files[relative] = dict(**audit.record_file(root, p), component='graphicssdk')
        elif system_input(p):
            system.add(p.as_posix())
        else:
            raise audit.AuditError(f'Unreviewed external compiler input: {p}')
    actual_headers = {r['path'].removeprefix(policy['dependency']['directory'] + '/') for r in files.values() if r['component'] == 'vulkan-headers'}
    if {r['path'] for r in reviewed_headers.values()} != actual_headers:
        raise audit.AuditError('Reviewed Vulkan header closure changed')
    apache = root / 'licenses/provenance/vulkan-headers-Apache-2.0.txt'
    if apache.read_bytes() != (headers / 'LICENSES/Apache-2.0.txt').read_bytes():
        raise audit.AuditError('Apache notice is not the pinned upstream text')
    notices_text = ('THIRD-PARTY AND SDK NOTICES\n\nProfile: ' + PROFILE + '\n'
        'Conservative compiler-read closure, not symbol reachability or legal approval.\n'
        'Loader, ICD, validation layer, Windows/SDK/CRT/driver binaries are external prerequisites, not bundled.\n\n'
        'GraphicsSDK - MIT\n\n' + audit.text_file(root / 'LICENSE').strip() + '\n\n'
        'Vulkan-Headers - Apache-2.0\nPinned commit: ' + commit + '\n'
        'Unmodified compiler-read headers; no MIT alternative is selected for these Apache-only files.\n\n'
        + '\n\n'.join(sorted(notices)) + '\n\n' + audit.text_file(apache).strip() + '\n')
    inventory = dict(schema_version=1, profile=PROFILE, platform='windows-x64',
        configuration='Release', SDK_source_fingerprint=fingerprint, policy=audit.record_file(root, root / POLICY),
        scope='Actual compiler-read closure of integrated Core/tests/examples/helper and installed Vulkan consumer; not symbol reachability or legal approval.',
        components=[dict(id='graphicssdk', selected='MIT'), dict(id='vulkan-headers', selected='Apache-2.0', commit=commit)],
        files=sorted(files.values(), key=lambda r: r['path'].casefold()), compiler_read_evidence=compiler_logs,
        linker_read_evidence=link_logs, projects=projects, artifacts=artifacts,
        system_header_count=len(system), system_input_paths_sha256=audit.sha256('\n'.join(sorted(system)).encode()),
        license_text=audit.record_file(root, apache), runtime_distribution='none',
        exclusions=EXCLUSIONS)
    return inventory, notices_text

def payload(root, notices):
    files = {'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt': notices.encode(),
             'share/GraphicsSDK/Apache-2.0.txt': (root / 'licenses/provenance/vulkan-headers-Apache-2.0.txt').read_bytes(),
             'share/GraphicsSDK/README.md': (root / 'README.md').read_bytes(),
             'share/GraphicsSDK/VULKAN.md': (root / 'docs/VULKAN.md').read_bytes()}
    expected = {'lib/GraphicsSDKCore.lib', 'share/GraphicsSDK/LICENSE'}
    expected.update('include/graphics/' + p.name for p in (root / 'include/graphics').glob('*.hpp'))
    expected.update('lib/cmake/GraphicsSDK/' + p for p in ('GraphicsSDKConfig.cmake', 'GraphicsSDKConfigVersion.cmake',
        'GraphicsSDKTargets.cmake', 'GraphicsSDKTargets-release.cmake'))
    installed = {p.relative_to(root / INSTALL).as_posix(): p for p in (root / INSTALL).rglob('*') if p.is_file()}
    if installed.keys() != expected:
        raise audit.AuditError('Missing or unexpected installed payload')
    for name, p in sorted(installed.items()):
        if name.startswith('include/graphics/') and p.read_bytes() != (root / name).read_bytes():
            raise audit.AuditError(f'Installed public header differs from source: {name}')
        files[name] = p.read_bytes()
    if not {'lib/GraphicsSDKCore.lib', 'share/GraphicsSDK/LICENSE', 'lib/cmake/GraphicsSDK/GraphicsSDKTargets-release.cmake'} <= files.keys():
        raise audit.AuditError('Incomplete installed package')
    if any(pathlib.PurePosixPath(p).suffix.casefold() in ('.exe', '.dll', '.pdb') for p in files):
        raise audit.AuditError('Runtime/helper binary leaked into SDK payload')
    return files

def package_record(root, package, required):
    with zipfile.ZipFile(package) as z:
        entries = [(i.filename, z.read(i)) for i in z.infolist()]
    audit.validate_package_entries(entries, required)
    if {p for p, _ in entries} != required.keys():
        raise audit.AuditError('Unexpected package members')
    return dict(**audit.record_file(root, package), status='inventory_verified',
        files=[dict(path=name, sha256=audit.sha256(data), bytes=len(data)) for name, data in sorted(entries)])

def configure_variant(variant):
    global BUILD, CONSUMER, INSTALL, POLICY, PROFILE, EXPECTED_LOGS, EXCLUSIONS
    if variant in ('validation', 'textures', 'color-pass', 'draw'):
        BUILD = pathlib.Path('out/vulkan-' + variant)
        CONSUMER = pathlib.Path('out/vulkan-' + variant + '-consumer')
        INSTALL = pathlib.Path('out/vulkan-' + variant + '-install')
        POLICY = pathlib.Path('licenses/vulkan-' + variant + '-policy.json')
        PROFILE = 'windows-sdk-vulkan-' + variant + '-release'
        EXPECTED_LOGS = {'validation': (16, 15), 'textures': (18, 17), 'color-pass': (20, 19), 'draw': (23, 22)}[variant]
        EXCLUSIONS = ['Linux native builds/runtime', 'Vulkan GPU-assisted validation', 'Vulkan rendering and competitor measurements',
            'External local-only VVL/SPIRV runtime build, separately recorded and not bundled',
            'Historical binaries/packages/benchmarks, preserved independently']
        if variant == 'color-pass':
            EXCLUSIONS[2] = 'Vulkan shader drawing/depth/bindings/presentation and competitor measurements'
        if variant == 'draw':
            EXCLUSIONS[2] = 'Vulkan depth/bindings/vertex input/indexed drawing/presentation and competitor measurements'
            EXCLUSIONS[3] = 'External local-only VVL/SPIRV fixture tools, separately recorded and not bundled'
    elif variant == 'transfer':
        BUILD = pathlib.Path('out/vulkan-transfer')
        CONSUMER = pathlib.Path('out/vulkan-consumer')
        INSTALL = pathlib.Path('out/vulkan-install')
        POLICY = pathlib.Path('licenses/vulkan-policy.json')
        PROFILE = 'windows-sdk-vulkan-transfer-release'
        EXPECTED_LOGS = (17, 16)
        EXCLUSIONS = ['Linux native builds/runtime', 'Vulkan validation-layer execution', 'Vulkan rendering and competitor measurements',
            'Historical D3D-only binaries/packages/benchmarks, preserved independently']
    else:
        raise audit.AuditError('Unknown Vulkan audit variant')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', choices=('transfer', 'validation', 'textures', 'color-pass', 'draw'), default='transfer')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--package', type=pathlib.Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--create', action='store_true')
    mode.add_argument('--check', action='store_true')
    args = parser.parse_args()
    configure_variant(args.variant)
    try:
        inventory, notices = collect(ROOT)
        required = payload(ROOT, notices)
        package = args.package.resolve()
        if args.create:
            # Exclusive creation preserves previous packages and snapshots.
            with zipfile.ZipFile(package, 'x', compression=zipfile.ZIP_DEFLATED) as z:
                for name, data in sorted(required.items()):
                    entry = zipfile.ZipInfo(name, (2026, 10, 23, 0, 0, 0)); entry.compress_type = zipfile.ZIP_DEFLATED
                    z.writestr(entry, data)
        inventory['package'] = package_record(ROOT, package, required)
        audit.write_or_check(args.output, audit.output_files(inventory, notices), args.check)
        print(f"PASS: {len(inventory['files'])} compiler inputs, 2 components, {len(inventory['linker_read_evidence'])} linker logs; exact package payload verified")
        return 0
    except (audit.AuditError, OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.SubprocessError) as e:
        print('FAIL:', e)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
