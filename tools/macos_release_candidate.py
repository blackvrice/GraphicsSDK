"""Package or verify two audited, single-architecture macOS Metal SDK ZIPs."""

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import struct
import tempfile
import zipfile

from linux_comparison import cache, read, require, sha
from macos_metal_package import CONFIG, LIBRARY, OBJECTS, check


CPU_TYPES = {'arm64': 0x0100000C, 'x86_64': 0x01000007}
ARCHIVE_MAGIC = b'!<arch>\n'
ARCHIVE_HEADER_SIZE = 60
MACH_HEADER_SIZE = 32
MH_MAGIC_64 = 0xFEEDFACF
MH_OBJECT = 1
MANIFEST = 'manifest.json'
VERSION_FILE = CONFIG + 'GraphicsSDKConfigVersion.cmake'
VERSION_HEADER = 'include/graphics/Backend.hpp'


def archive_objects(data, arch):
    require(data.startswith(ARCHIVE_MAGIC), 'Expected a BSD static archive')
    position = len(ARCHIVE_MAGIC)
    index_seen = False
    objects = {}
    while position < len(data):
        header = data[position:position + ARCHIVE_HEADER_SIZE]
        require(len(header) == ARCHIVE_HEADER_SIZE and header.endswith(b'`\n'), 'Invalid archive member header')
        size = header[48:58].strip()
        require(size.isdigit(), 'Invalid archive member size')
        size = int(size)
        start = position + ARCHIVE_HEADER_SIZE
        end = start + size
        require(end <= len(data), 'Truncated archive member')
        position = end + (size % 2)
        if size % 2:
            require(data[end:position] == b'\n', 'Invalid archive padding')

        name = header[:16].rstrip()
        member = data[start:end]
        if name.startswith(b'#1/'):
            length = name[3:]
            require(length.isdigit(), 'Invalid BSD archive member name length')
            length = int(length)
            require(0 < length <= len(member), 'Invalid BSD archive member name length')
            name = member[:length].rstrip(b'\0')
            member = member[length:]
        name = name.decode('ascii')
        if name in ('__.SYMDEF', '__.SYMDEF SORTED'):
            require(not index_seen and not objects and member, 'Invalid archive symbol index')
            index_seen = True
            continue

        require(name in OBJECTS and name not in objects, f'Unexpected archive object: {name}')
        require(len(member) >= MACH_HEADER_SIZE, f'Truncated Mach-O header: {name}')
        magic, cpu, _, file_type, _, commands_size = struct.unpack_from('<6I', member)
        require(magic == MH_MAGIC_64 and cpu == CPU_TYPES[arch] and file_type == MH_OBJECT,
                f'Expected a thin {arch} Mach-O object: {name}')
        require(commands_size <= len(member) - MACH_HEADER_SIZE, f'Truncated Mach-O load commands: {name}')
        objects[name] = dict(bytes=len(member), sha256=hashlib.sha256(member).hexdigest())
    require(index_seen and objects.keys() == OBJECTS, 'Missing SDK archive objects or index')
    return dict(sorted(objects.items()))


def pin_receipt(audit, receipt_hash, arch):
    path = audit / 'receipt.json'
    require(re.fullmatch(r'[0-9a-f]{64}', receipt_hash) and sha(path) == receipt_hash,
            f'{arch} receipt SHA-256 differs from its pin')
    receipt = read(path)
    require(receipt['architecture'] == arch, f'{arch} audit architecture differs')
    for key, pattern in (('source_commit', r'[0-9a-f]{40}'), ('github_run_id', r'[1-9][0-9]*'),
                         ('SDK_source_fingerprint', r'[0-9a-f]{64}'), ('zip_sha256', r'[0-9a-f]{64}')):
        require(re.fullmatch(pattern, receipt[key]), f'Invalid {arch} {key}')
    return receipt


def package_version(config):
    versions = re.findall(r'^set\(PACKAGE_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)$', config, re.MULTILINE)
    require(len(versions) == 1, 'Expected one CMake package version')
    return versions[0]


def candidate_name(version, arch, receipt):
    source = receipt['source_commit'][:12]
    digest = receipt['zip_sha256'][:12]
    return f'GraphicsSDK-{version}-macos-{arch}-metal-release-{source}-{digest}.zip'


def inspect_package(arch, audit, receipt_hash, archive, output):
    receipt = pin_receipt(audit, receipt_hash, arch)
    config = audit / 'package-config' / pathlib.PurePosixPath(VERSION_FILE).name
    version = package_version(config.read_text(encoding='utf-8'))
    name = candidate_name(version, arch, receipt)
    if archive is None:
        archive = output / name
    require(archive.is_file() and not archive.is_symlink(), f'Missing {arch} SDK ZIP')
    require(cache(audit / 'consumer-cache.txt')['CMAKE_BUILD_TYPE'] == 'Release', 'Expected a Release consumer')
    native = check(audit, archive)
    with zipfile.ZipFile(archive) as package:
        header = package.read(VERSION_HEADER).decode('utf-8')
        versions = re.findall(r'^inline constexpr std::string_view version = "([^"]+)";$', header, re.MULTILINE)
        require(versions == [version], 'Public SDK version differs from the CMake package version')
        objects = archive_objects(package.read(LIBRARY), arch)
    identity = dict(version=version, source_commit=receipt['source_commit'], github_run_id=receipt['github_run_id'],
                    SDK_source_fingerprint=receipt['SDK_source_fingerprint'])
    record = dict(package=dict(name=name, bytes=archive.stat().st_size, sha256=receipt['zip_sha256']),
                  audit_receipt_sha256=receipt_hash, payload=receipt['payload'], archive_objects=objects,
                  native_consumer=native)
    return identity, record


def candidate(arm64, x86_64, output):
    inputs = {'arm64': arm64, 'x86_64': x86_64}
    packing = arm64[2] is not None
    require(packing == (x86_64[2] is not None), 'Supply both original SDK ZIPs when packing')
    if packing:
        require(not output.exists() and not output.is_symlink(), 'Expected a fresh output directory')

    identities = {}
    packages = {}
    for arch, (audit, receipt_hash, archive) in inputs.items():
        identities[arch], packages[arch] = inspect_package(arch, audit, receipt_hash, archive, output)
    require(identities['arm64'] == identities['x86_64'], 'The SDKs must share one version, source, and CI run')

    manifest = dict(schema_version=1, profile='macos-metal-release', **identities['arm64'],
                    packages=packages, physical_GPU_accepted=False, performance_accepted=False)
    if packing:
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.macos-candidate-', dir=output.parent) as temp:
            staged = pathlib.Path(temp) / 'candidate'
            staged.mkdir()
            for arch, record in packages.items():
                package = record['package']
                dest = staged / package['name']
                shutil.copyfile(inputs[arch][2], dest)
                require(sha(dest) == package['sha256'], f'{arch} SDK ZIP changed while copying')
            (staged / MANIFEST).write_bytes(manifest_bytes(manifest))
            staged.rename(output)
    else:
        expected = {MANIFEST, *(record['package']['name'] for record in packages.values())}
        require({entry.name for entry in output.iterdir()} == expected, 'Unexpected candidate files')
        require(not (output / MANIFEST).is_symlink(), 'Unexpected manifest symlink')
        require((output / MANIFEST).read_bytes() == manifest_bytes(manifest), 'Candidate manifest differs')
    return manifest


def manifest_bytes(manifest):
    return (json.dumps(manifest, indent=2) + '\n').encode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('pack', 'check'))
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--arm64-audit', nargs=2, metavar=('DIRECTORY', 'RECEIPT_SHA256'), required=True)
    parser.add_argument('--x86-64-audit', nargs=2, metavar=('DIRECTORY', 'RECEIPT_SHA256'), required=True)
    parser.add_argument('--arm64-zip', type=pathlib.Path)
    parser.add_argument('--x86-64-zip', type=pathlib.Path)
    args = parser.parse_args()
    if args.stage == 'pack' and not (args.arm64_zip and args.x86_64_zip):
        parser.error('pack requires --arm64-zip and --x86-64-zip')
    if args.stage == 'check' and (args.arm64_zip or args.x86_64_zip):
        parser.error('check uses the candidate ZIPs in --output')
    try:
        manifest = candidate((pathlib.Path(args.arm64_audit[0]), args.arm64_audit[1], args.arm64_zip),
                             (pathlib.Path(args.x86_64_audit[0]), args.x86_64_audit[1], args.x86_64_zip),
                             args.output)
    except (OSError, KeyError, TypeError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')
    print(manifest_bytes(manifest).decode('utf-8'), end='')


if __name__ == '__main__':
    main()
