"""Split an audited Release SDK into a stripped ZIP and a relinkable DWARF archive."""

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shlex
import shutil
import subprocess
import tempfile
import zipfile

from audit_linux_package import package_zip
from linux_release_candidate import (
    LIBRARY, XCB_PROFILE, check_file, check_smoke, digest, inspect_and_smoke, inspect_audit,
    package_entries, record, run,
)
from linux_comparison import read, require, sha


def source_map(identity):
    flags = shlex.split(identity['release_cxx_flags'])
    maps = [flag.removeprefix('-ffile-prefix-map=') for flag in flags if flag.startswith('-ffile-prefix-map=')]
    require('-g' in flags and len(maps) == 1 and maps[0].startswith('/') and maps[0].endswith('=.'),
            'Expected an audited Release build with DWARF and a mapped source root')
    return maps[0][:-2].encode()


def read_package(path, payload):
    with zipfile.ZipFile(path) as archive:
        return dict(package_entries(archive, payload))


def runtime_inventory(original, library):
    require(library['path'] == LIBRARY and digest(library['sha256']) and library['sha256'] != original[LIBRARY]['sha256']
            and type(library['bytes']) is int and 0 < library['bytes'] < original[LIBRARY]['bytes'],
            'Runtime archive must differ and be smaller')
    return {**original, LIBRARY: library}


def split_library(full, runtime, audited_members, mapped_root):
    env = dict(os.environ, LC_ALL='C')
    full_bytes = full.read_bytes()
    require(mapped_root + b'/' not in full_bytes and mapped_root + b'\0' not in full_bytes,
            'Unmapped SDK source path in the debug archive')
    run(['objcopy', '--strip-debug', '--enable-deterministic-archives', str(full), str(runtime)], full.parent, env)

    def inspect(library, debug):
        members = run(['ar', 't', library.name], library.parent, env).splitlines()
        require(len(members) == len(audited_members) and set(members) == set(audited_members), 'Archive members differ')
        object_hashes = {}
        for member in members:
            obj = subprocess.check_output(['ar', 'p', library.name, member], cwd=library.parent, env=env, timeout=30)
            object_hashes[member] = hashlib.sha256(obj).hexdigest()
        sections = run(['readelf', '-SW', library.name], library.parent, env)
        objects = sections.split('\nFile: ')[1:]
        require(len(objects) == len(members), 'Missing ELF member section tables')
        for block in objects:
            names = set(re.findall(r'^\s*\[\s*\d+\]\s+(\.\S+)', block, re.M))
            if debug:
                require({'.debug_info', '.debug_line'} <= names, 'Audited member has no source-level DWARF')
            else:
                require(not any(name.startswith(('.debug', '.zdebug', '.rela.debug', '.rel.debug')) for name in names),
                        'Stripped member retains debug sections')
        symbols = run(['nm', '-A', '-P', '-g', library.name], library.parent, env)
        require(symbols, 'No global symbols in archive')
        return members, object_hashes, symbols

    full_order, full_members, full_symbols = inspect(full, True)
    order, members, symbols = inspect(runtime, False)
    require(full_members == audited_members and order == full_order and symbols == full_symbols,
            'Stripping changed archive layout or global symbols')
    return members, hashlib.sha256(symbols.encode()).hexdigest()


def artifact_stem(stem, runtime_sha, debug_sha):
    return stem.rsplit('-', 1)[0] + f'-split-{runtime_sha[:12]}-{debug_sha[:12]}'


def pack(stem, source, payload, identity, members, output):
    require(platform.system() == 'Linux' and platform.machine() == 'x86_64', 'Linux x86_64 is required to split DWARF')
    require(not output.exists(), 'Use a fresh output directory')
    mapped_root = source_map(identity)
    with tempfile.TemporaryDirectory(prefix='graphics-debug-split-') as temporary:
        work = pathlib.Path(temporary)
        runtime_root, debug_root = work / 'runtime', work / 'debug'
        for name, data in read_package(source, payload).items():
            target = (debug_root if name == LIBRARY else runtime_root) / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        runtime_lib = runtime_root / LIBRARY
        stripped_members, symbols_sha = split_library(debug_root / LIBRARY, runtime_lib, members, mapped_root)
        runtime_payload = runtime_inventory(payload, dict(path=LIBRARY, bytes=runtime_lib.stat().st_size,
                                                          sha256=sha(runtime_lib)))
        runtime_zip, debug_zip = work / 'runtime.zip', work / 'debug.zip'
        package_zip(runtime_root, runtime_zip, [runtime_payload[name] for name in sorted(runtime_payload)])
        package_zip(debug_root, debug_zip, [payload[LIBRARY]])
        _, smoke = inspect_and_smoke(runtime_zip, runtime_payload, identity, stripped_members, expect_debug=False)

        # Rebuild the original install from the two candidate ZIPs before relinking.
        runtime_lib.write_bytes(read_package(debug_zip, {LIBRARY: payload[LIBRARY]})[LIBRARY])
        restored_zip = work / 'restored.zip'
        package_zip(runtime_root, restored_zip, [payload[name] for name in sorted(payload)])
        _, restored = inspect_and_smoke(restored_zip, payload, identity, members, expect_debug=True)
        require(restored == smoke, 'Debug relink differs from the stripped runtime smoke')

        name = artifact_stem(stem, sha(runtime_zip), sha(debug_zip))
        output.mkdir(parents=True)
        runtime_file, debug_file = output / f'{name}.zip', output / f'{name}.debug.zip'
        shutil.copyfile(runtime_zip, runtime_file)
        shutil.copyfile(debug_zip, debug_file)
        manifest = dict(schema_version=1, source=identity, runtime=record(runtime_file), debug=record(debug_file),
                        runtime_payload=list(runtime_payload.values()), runtime_archive_members=stripped_members,
                        global_symbols_sha256=symbols_sha, smoke=smoke,
                        linked_sdk_dwarf=dict(runtime=False, debug_relink=True))
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


def check(stem, payload, identity, output):
    manifest = read(output / 'manifest.json')
    check_manifest(stem, payload, identity, output, manifest)


def check_manifest(stem, payload, identity, output, manifest):
    source_map(identity)
    require(manifest['schema_version'] == 1 and manifest['source'] == identity, 'Split manifest source differs from audit')
    rows = {row['path']: row for row in manifest['runtime_payload']}
    require(len(rows) == len(manifest['runtime_payload']) and rows == runtime_inventory(payload, rows[LIBRARY]),
            'Runtime payload changed outside the audited library')
    require(set(manifest['runtime_archive_members']) == set(identity['archive_members']) and
            all(digest(sha256) for sha256 in manifest['runtime_archive_members'].values()) and
            digest(manifest['global_symbols_sha256']), 'Split archive member or symbol identity differs')
    require(all(digest(manifest[key]['sha256']) for key in ('runtime', 'debug')), 'Split ZIP hash is invalid')
    name = artifact_stem(stem, manifest['runtime']['sha256'], manifest['debug']['sha256'])
    files = {'runtime': f'{name}.zip', 'debug': f'{name}.debug.zip'}
    require({p.name for p in output.iterdir()} == {'manifest.json', *files.values()}, 'Unexpected split output entries')
    for key, filename in files.items():
        require(manifest[key]['name'] == filename, 'Split archive name differs')
        check_file(output / filename, manifest[key])
    read_package(output / files['runtime'], rows)
    read_package(output / files['debug'], {LIBRARY: payload[LIBRARY]})
    require(manifest['linked_sdk_dwarf'] == dict(runtime=False, debug_relink=True), 'Linked DWARF evidence differs')
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
    check(stem, payload, identity, args.output)
    print(f'PASS {identity["profile"]} stripped package and audited debug relink archive')


if __name__ == '__main__':
    main()
