"""Verify the six SDK candidate profiles offline against pinned manifests."""

import argparse
import hashlib
import json
import pathlib
import re
import sys
import zipfile

import linux_debug_symbols as linux_split
import linux_release_candidate as linux
import macos_metal_package as metal
import macos_release_candidate as macos
import windows_release_candidate as windows
from linux_comparison import require


GROUPS = ('windows', 'linux-headless', 'linux-xcb', 'macos')


def unique_object(pairs):
    result = dict(pairs)
    require(len(result) == len(pairs), 'Duplicate JSON field in candidate manifest')
    return result


def pinned_manifest(folder, expected_sha):
    path = folder / 'manifest.json'
    require(path.is_file() and not path.is_symlink(), 'Missing candidate manifest')
    data = path.read_bytes()
    require(linux.digest(expected_sha) and hashlib.sha256(data).hexdigest() == expected_sha,
            'Pinned candidate manifest SHA-256 differs')
    manifest = json.loads(data, object_pairs_hook=unique_object)
    require(type(manifest['schema_version']) is int and manifest['schema_version'] == 1,
            'Unsupported candidate manifest schema')
    return manifest


def inventory(rows):
    payload = {row['path']: row for row in rows}
    require(len(payload) == len(rows), 'Duplicate payload path in candidate manifest')
    return payload


def source_identity(source, fingerprint_key='SDK_source_fingerprint'):
    commit = source['source_commit']
    fingerprint = source[fingerprint_key]
    version = source['version']
    require(linux.digest(commit, 40) and linux.digest(fingerprint)
            and re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version), 'Invalid candidate source or version')
    return dict(version=version, source_commit=commit, sdk_source_fingerprint=fingerprint)


def inspect_sdk(path, payload, library, version, third_party=False):
    notices = {metal.NOTICE}
    if third_party:
        notices.add(linux.NOTICE)
    expected = (metal.PAYLOAD_FILES - {metal.LIBRARY}) | {library} | notices
    require(payload.keys() == expected, 'SDK payload or required notice differs from the profile')
    require(all(type(row['bytes']) is int and row['bytes'] > 0 and linux.digest(row['sha256'])
                for row in payload.values()), 'Invalid payload file size or digest')

    with zipfile.ZipFile(path) as archive:
        config = archive.read(macos.VERSION_FILE).decode('utf-8').replace('\r\n', '\n')
        header = archive.read(macos.VERSION_HEADER).decode('utf-8').replace('\r\n', '\n')
    public_versions = re.findall(r'^inline constexpr std::string_view version = "([^"]+)";$', header, re.M)
    require(macos.package_version(config) == version and public_versions == [version],
            'Candidate version differs from the installed CMake config or public header')
    return dict(payload_files=len(payload), notices={name: payload[name]['sha256'] for name in sorted(notices)})


def windows_profiles(folder, manifest):
    identity = source_identity(manifest)
    windows.check_manifest(folder, manifest)
    profiles = {}
    for profile in windows.PROFILES:
        row = manifest['packages'][profile]
        package = row['package']
        payload = inspect_sdk(folder / package['name'], row['payload'], windows.LIBRARY,
                              identity['version'], third_party=profile == 'vulkan')
        smoke = row['installed_consumer']
        require(all(smoke[key] is True for key in ('CPU_executed', 'exact_version', 'wrong_version_rejected',
                                                  'missing_prefix_rejected')) and smoke['GPU_executed'] is False,
                'Windows recorded CPU consumer scope differs')
        profiles[f'windows-x64-{profile}-release'] = dict(
            **identity, **payload, runtime=package, recorded_validation='windows-installed-cpu')
    return profiles


def linux_profile(folder, manifest, xcb):
    source = manifest['source']
    identity = source_identity(source, 'sdk_source_fingerprint')
    profile = linux.XCB_PROFILE if xcb else linux.PROFILE
    flags = dict(linux.FLAGS, GFX_VULKAN_XCB='ON' if xcb else 'OFF')
    require(source['profile'] == profile and source['build_flags'] == flags
            and linux.digest(source['audit_receipt_sha256']) and linux.digest(source['audit_package_sha256']),
            'Linux split profile or audit identity differs')
    stem = f'GraphicsSDK-{identity["version"]}-{profile}-{identity["source_commit"][:12]}-{source["audit_package_sha256"][:12]}'
    linux_split.check_manifest(stem, inventory(source['payload']), source, folder, manifest)
    runtime = manifest['runtime']
    payload = inspect_sdk(folder / runtime['name'], inventory(manifest['runtime_payload']), linux.LIBRARY,
                          identity['version'], third_party=True)
    scope = 'linux-lavapipe-xvfb' if xcb else 'linux-lavapipe'
    return {profile: dict(**identity, **payload, runtime=runtime, debug=manifest['debug'],
                          audit_receipt_sha256=source['audit_receipt_sha256'], recorded_validation=scope)}


def macos_profiles(folder, manifest):
    identity = source_identity(manifest)
    require(manifest['profile'] == 'macos-metal-release' and set(manifest['packages']) == set(macos.CPU_TYPES)
            and manifest['physical_GPU_accepted'] is False and manifest['performance_accepted'] is False
            and re.fullmatch(r'[1-9][0-9]*', manifest['github_run_id']), 'macOS candidate scope differs')
    profiles = {}
    files = {'manifest.json'}
    for arch in macos.CPU_TYPES:
        row = manifest['packages'][arch]
        package = row['package']
        require(linux.digest(package['sha256']) and linux.digest(row['audit_receipt_sha256']),
                'Invalid macOS package or audit hash')
        name = macos.candidate_name(identity['version'], arch, dict(
            source_commit=identity['source_commit'], zip_sha256=package['sha256']))
        require(package['name'] == name, 'macOS ZIP name differs')
        files.add(name)
        path = folder / name
        linux.check_file(path, package)
        metal.verify_zip(path, row['payload'], package['sha256'])
        payload = inspect_sdk(path, inventory(row['payload']), metal.LIBRARY, identity['version'])
        with zipfile.ZipFile(path) as archive:
            require(macos.archive_objects(archive.read(metal.LIBRARY), arch) == row['archive_objects'],
                    'macOS archive object inventory differs')
        native = row['native_consumer']
        require(native['architecture'] == arch and native['physical_GPU_accepted'] is False,
                'macOS recorded consumer scope differs')
        profiles[f'macos-{arch}-metal-release'] = dict(
            **identity, **payload, runtime=package, recorded_validation='macos-hosted-metal',
            audit_receipt_sha256=row['audit_receipt_sha256'], github_run_id=manifest['github_run_id'])
    require({p.name for p in folder.iterdir()} == files, 'Unexpected macOS candidate files')
    return profiles


def audit(groups):
    profiles = {}
    for group in GROUPS:
        folder, expected_sha = groups[group]
        try:
            manifest = pinned_manifest(folder, expected_sha)
            if group == 'windows':
                checked = windows_profiles(folder, manifest)
            elif group == 'macos':
                checked = macos_profiles(folder, manifest)
            else:
                checked = linux_profile(folder, manifest, xcb=group == 'linux-xcb')
        except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
            raise ValueError(f'{group}: {error}') from error
        profiles.update({name: dict(**row, manifest_sha256=expected_sha) for name, row in checked.items()})
    versions = {row['version'] for row in profiles.values()}
    require(len(versions) == 1, 'Candidate SDK versions differ')
    return dict(schema_version=1, scope='offline-sdk-candidate-integrity', version=versions.pop(),
                source_commits=sorted({row['source_commit'] for row in profiles.values()}), profiles=profiles)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for group in GROUPS:
        parser.add_argument(f'--{group}', nargs=2, required=True, metavar=('DIRECTORY', 'MANIFEST_SHA256'))
    args = parser.parse_args()
    inputs = {}
    for group in GROUPS:
        folder, digest = getattr(args, group.replace('-', '_'))
        inputs[group] = (pathlib.Path(folder), digest)
    try:
        result = audit(inputs)
    except ValueError as error:
        parser.exit(1, f'{parser.prog}: {error}\n')
    sys.stdout.buffer.write((json.dumps(result, indent=2, sort_keys=True) + '\n').encode('utf-8'))


if __name__ == '__main__':
    main()
