"""Build and audit an independent consumer of a relocated macOS Metal SDK ZIP."""

import argparse
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import tempfile
import zipfile

from audit_linux_package import package_zip
from linux_comparison import ROOT, cache, read, require, save, sha
from macos_core import CORE, compile_graph, exact
from macos_metal_graphics import payloads, result


LIBRARY = 'lib/libGraphicsSDKCore.a'
NOTICE = 'share/GraphicsSDK/LICENSE'
CONFIG = 'lib/cmake/GraphicsSDK/'
CONFIG_FILES = ('GraphicsSDKConfig.cmake', 'GraphicsSDKConfigVersion.cmake',
                'GraphicsSDKTargets.cmake', 'GraphicsSDKTargets-release.cmake')
SDK_UNITS = (*CORE, 'src/backends/metal/Metal.cpp')
OBJECTS = {pathlib.PurePosixPath(p).name+'.o' for p in SDK_UNITS}
FRAMEWORKS = {'Metal', 'Foundation', 'QuartzCore'}
SYSTEM_LIBS = {'/usr/lib/libSystem.B.dylib', '/usr/lib/libc++.1.dylib', '/usr/lib/libobjc.A.dylib'}


def payload(prefix, headers):
    expected = {LIBRARY, NOTICE, *(CONFIG+p for p in CONFIG_FILES), *('include/'+h['path'] for h in headers)}
    nodes = list(prefix.rglob('*'))
    require(not any(p.is_symlink() for p in nodes), 'Package contains a symlink')
    files = {p.relative_to(prefix).as_posix() for p in nodes if p.is_file()}
    require(files == expected, f'Unexpected package members: {sorted(files ^ expected)}')
    for h in headers:
        require(sha(prefix/'include'/h['path']) == h['sha256'], 'Public header bytes changed')
    return [dict(path=p, bytes=(prefix/p).stat().st_size, sha256=sha(prefix/p)) for p in sorted(files)]


def exports(folder):
    for name in CONFIG_FILES:
        text = (folder/name).read_text(encoding='utf-8')
        require(not re.search(r'(?m)(?:/Applications/|/Users/|/private/var/|/opt/homebrew/|\.deps/|libSDL|libLLGL|Vulkan\.framework)', text),
                'CMake package embeds a build-machine path or a foreign backend')
    targets = (folder/'GraphicsSDKTargets.cmake').read_text()
    require(set(re.findall(r'-framework (\w+)', targets)) == FRAMEWORKS, 'Metal framework names missing or changed in the export')
    release = (folder/'GraphicsSDKTargets-release.cmake').read_text()
    require('${_IMPORT_PREFIX}/'+LIBRARY in release, 'SDK archive is not relative to the install prefix')


def under(path, root):
    return path.startswith(root.rstrip('/')+'/') and '..' not in pathlib.PurePosixPath(path).parts


def dependencies(text, objects, own, system):
    sections = {}
    target = None
    for line in text.splitlines():
        match = re.fullmatch(r'(.+): #deps (\d+), deps mtime .+ \(VALID\)', line)
        if match:
            target = match[1]
            require(target not in sections, 'Duplicate Ninja dependency target')
            sections[target] = (int(match[2]), [])
        elif line.startswith('    ') and target:
            sections[target][1].append(line.strip())
        elif line:
            raise ValueError(f'Unexpected Ninja dependency format: {line}')
    require(set(sections) == set(objects), 'Missing or extra audited compile units')
    totals = dict(own=0, system=0)
    for count, paths in sections.values():
        require(count == len(paths) and paths, 'Truncated Ninja dependency list')
        for path in paths:
            if path in own:
                totals['own'] += 1
            elif any(under(path, p) for p in system):
                totals['system'] += 1
            else:
                raise ValueError(f'Unreviewed compiler input: {path}')
    return totals


def linker(text, library, consumer_object, sysroot, build):
    require('# Object files:\n' in text and '# Sections:' in text, 'Missing linker map object table')
    section = text.split('# Object files:\n', 1)[1].split('# Sections:', 1)[0]
    pulled = set()
    seen_consumer = False
    for row in section.splitlines():
        if not row.strip():
            continue
        match = re.fullmatch(r'\[\s*\d+\]\s+(.+)', row)
        require(match, 'Invalid linker map entry')
        path = match[1]
        if path in (consumer_object, './'+consumer_object, build+'/'+consumer_object):
            seen_consumer = True
        elif path.startswith(library+'(') and path.endswith(')'):
            member = path[len(library)+1:-1]
            require(member in OBJECTS, 'Unexpected SDK archive member in link map')
            pulled.add(member)
        elif path != 'linker synthesized' and not (under(path, sysroot) and path.endswith('.tbd')):
            raise ValueError(f'Unreviewed linker input: {path}')
    require(seen_consumer and {'Backend.cpp.o', 'Metal.cpp.o'} <= pulled, 'Missing relocated Metal link objects')
    return sorted(pulled)


def dylibs(text):
    paths = {line.strip().split(' (compatibility version ', 1)[0] for line in text.splitlines()[1:] if line.strip()}
    frameworks = {f'/System/Library/Frameworks/{name}.framework/Versions/{version}/{name}'
                  for name, version in (('Metal', 'A'), ('Foundation', 'C'), ('QuartzCore', 'A'))}
    require(frameworks <= paths <= frameworks | SYSTEM_LIBS, f'Unexpected dynamic library list: {sorted(paths)}')
    return sorted(paths)


def check(out):
    r = read(out/'receipt.json')
    exact(r, dict(schema_version=1, scope='macos-relocated-metal-package-v1', physical_GPU_accepted=False, performance_accepted=False))
    require(r['architecture'] in ('arm64', 'x86_64'), 'Unexpected Mach-O architecture')
    raw = out/'commands'
    names = ['package-arch', 'archive-members', 'sdk-deps', 'empty-prefix', 'configure', 'build',
             'consumer-deps', 'consumer-arch', 'dylibs', 'run']
    rows = read(out/'commands.json')
    require([row['name'] for row in rows] == names, 'Missing package audit commands')
    for row in rows:
        require(row['returncode'] == (1 if row['name'] == 'empty-prefix' else 0), 'Unexpected package command outcome')
    require('Could not find a package configuration file provided by "GraphicsSDK"' in (raw/'empty-prefix.stderr').read_text(),
            'Missing package negative control did not exercise find_package')
    for name in ('package-arch','consumer-arch'):
        require((raw/f'{name}.stdout').read_text().strip() == r['architecture'], 'Package architecture differs')
    members = (raw/'archive-members.stdout').read_text().splitlines()
    require(len(members) == len(OBJECTS) and set(members) == OBJECTS, 'Unexpected static archive members')
    exports(out/'package-config')
    manifest = read(out/'payload.json')
    require(manifest == r['payload'] and len(manifest) == 19, 'Package manifest differs')
    for item in manifest:
        name = item['path']
        if name.startswith(CONFIG):
            file = out/'package-config'/name.removeprefix(CONFIG)
            require(sha(file) == item['sha256'] and file.stat().st_size == item['bytes'], 'Export bytes differ from ZIP manifest')
        elif name == NOTICE:
            require(sha(out/'LICENSE') == item['sha256'], 'Packaged license evidence differs')
    build = r['consumer_build']
    package = r['relocated_prefix']
    require(not under(package, r['source_directory']) and ' ' in package, 'Consumer did not use a separate prefix with spaces')
    exact(cache(out/'consumer-cache.txt'), dict(GraphicsSDK_DIR=package+'/'+CONFIG.rstrip('/')))
    compile_graph(read(out/'consumer-compile.json'), [r['consumer_source']+'/Consumer.cpp'], r['architecture'], r['sysroot'])
    obj = 'CMakeFiles/package_consumer.dir/Consumer.cpp.o'
    compiler_roots = (r['sysroot'], r['clang_resources'], r['stdlib_headers'])
    sdk = dependencies((raw/'sdk-deps.stdout').read_text(), ['CMakeFiles/GraphicsSDKCore.dir/'+p+'.o' for p in SDK_UNITS],
                       set(r['sdk_input_paths']), compiler_roots)
    consumer = dependencies((raw/'consumer-deps.stdout').read_text(), [obj],
                            {r['consumer_source']+'/Consumer.cpp', *(package+'/'+x['path'] for x in manifest if x['path'].startswith('include/'))}, compiler_roots)
    link = linker((out/'consumer.map').read_text(), package+'/'+LIBRARY, obj, r['sysroot'], build)
    system = dylibs((raw/'dylibs.stdout').read_text())
    data = read(raw/'run.stdout')
    result(data, r['SDK_source_fingerprint'])
    require(payloads(out/'readbacks') == data['verified_bytes'], 'Installed SDK draw readback differs')
    return dict(architecture=r['architecture'], files=len(manifest), graphics_images=data['images'],
                verified_bytes=data['verified_bytes'], sdk_dependencies=sdk, consumer_dependencies=consumer,
                linked_archive_members=link, dylibs=system, physical_GPU_accepted=False)


def run(evidence, out):
    require(platform.system() == 'Darwin', 'Native macOS required')
    baseline = read(evidence/'receipt.json')
    core = read(evidence/'availability/core/receipt.json')
    arch, sysroot = baseline['architecture'], core['sdk_path']
    require(arch == platform.machine(), 'Native package architecture differs')
    install = pathlib.Path(baseline['install_directory'])
    sdk_build = evidence.with_name(evidence.name+'-build')
    require(out.is_relative_to(ROOT/'out') and not out.exists(), 'Fresh package evidence path under out/ required')
    artifacts = out.with_name(out.name+'-payload')
    (out/'commands').mkdir(parents=True)
    artifacts.mkdir()
    manifest = payload(install, baseline['installed_headers'])
    require(sha(install/NOTICE) == sha(ROOT/'LICENSE'), 'Installed license differs from source')
    package = artifacts/f'GraphicsSDK-macos-metal-{arch}.zip'
    package_zip(install, package, manifest)
    save(out/'payload.json', manifest)
    shutil.copytree(install/CONFIG, out/'package-config')
    shutil.copyfile(install/NOTICE, out/'LICENSE')
    compiler = (evidence/'availability/core/commands/compiler-path.stdout').read_text().strip()
    env = {k:v for k,v in os.environ.items() if not k.startswith(('DYLD_', 'LD_', 'GFX_', 'MTL_', 'METAL_', 'CMAKE_'))
           and k not in ('CC','CXX','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','SDKROOT','CPATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH')}
    resources = subprocess.check_output([compiler, '-print-resource-dir'], env=env, text=True).strip()
    stdlib = pathlib.Path(compiler).parent.parent/'include/c++/v1'
    commands = []

    def call(name, args, cwd, expected=0):
        args = list(map(str, args))
        child = subprocess.run(args, cwd=cwd, env=env, stdin=subprocess.DEVNULL, capture_output=True, timeout=180)
        (out/'commands'/f'{name}.stdout').write_bytes(child.stdout)
        (out/'commands'/f'{name}.stderr').write_bytes(child.stderr)
        commands.append(dict(name=name, argv=args, cwd=str(cwd), returncode=child.returncode))
        save(out/'commands.json', commands)
        require(child.returncode == expected, f'{name} failed:\n'+(child.stdout+child.stderr).decode(errors='replace')[-6000:])

    with tempfile.TemporaryDirectory(prefix='graphics sdk package ') as directory:
        work = pathlib.Path(directory).resolve()
        extracted, src, build = (work/p for p in ('relocated SDK', 'consumer source', 'consumer build'))
        extracted.mkdir()
        with zipfile.ZipFile(package) as archive:
            archive.extractall(extracted)  # This archive was just created from the exact allowlist above.
        require(payload(extracted, baseline['installed_headers']) == manifest, 'Extracted ZIP bytes differ')
        src.mkdir()
        shutil.copyfile(ROOT/'tests/macos_metal_package/CMakeLists.txt', src/'CMakeLists.txt')
        shutil.copyfile(ROOT/'tests/MetalGraphicsTests.cpp', src/'Consumer.cpp')
        shutil.copyfile(ROOT/'tests/fixtures/metal/graphics.metal', src/'graphics.metal')
        shutil.copytree(src, out/'consumer-source')
        call('package-arch', ['lipo','-archs',extracted/LIBRARY], work)
        call('archive-members', ['ar','-t',extracted/LIBRARY], work)
        call('sdk-deps', ['ninja','-t','deps',*('CMakeFiles/GraphicsSDKCore.dir/'+p+'.o' for p in SDK_UNITS)], sdk_build)
        flags = ['-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+compiler,'-DCMAKE_OSX_ARCHITECTURES='+arch,
                 '-DCMAKE_OSX_SYSROOT='+sysroot,'-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        call('empty-prefix', ['cmake','-S',src,'-B',work/'negative',*flags,'-DPACKAGE_ROOT='+str(work/'missing'),
                              '-DCMAKE_PREFIX_PATH='+str(install)], work, expected=1)
        call('configure', ['cmake','-S',src,'-B',build,*flags,'-DPACKAGE_ROOT='+str(extracted)], work)
        call('build', ['cmake','--build',build,'--verbose'], work)
        call('consumer-deps', ['ninja','-t','deps','CMakeFiles/package_consumer.dir/Consumer.cpp.o'], build)
        call('consumer-arch', ['lipo','-archs',build/'package_consumer'], work)
        call('dylibs', ['otool','-L',build/'package_consumer'], work)
        call('run', [build/'package_consumer',src/'graphics.metal',baseline['SDK_source_fingerprint'],out/'readbacks'], work)
        for source, target in [('CMakeCache.txt','consumer-cache.txt'),('compile_commands.json','consumer-compile.json'),('consumer.map','consumer.map')]:
            shutil.copyfile(build/source, out/target)
        sdk_inputs = sorted(str(p) for base in (ROOT/'include', ROOT/'src') for p in base.rglob('*') if p.suffix in ('.hpp','.cpp'))
        save(out/'receipt.json', dict(schema_version=1,scope='macos-relocated-metal-package-v1',architecture=arch,
                                      source_commit=baseline['source_commit'],SDK_source_fingerprint=baseline['SDK_source_fingerprint'],
                                      github_run_id=baseline['github_run_id'],source_directory=str(ROOT),sysroot=sysroot,clang_resources=resources,
                                      stdlib_headers=str(stdlib),
                                      relocated_prefix=str(extracted),consumer_source=str(src),consumer_build=str(build),sdk_input_paths=sdk_inputs,
                                      payload=manifest,zip_sha256=sha(package),physical_GPU_accepted=False,performance_accepted=False))
    return check(out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('run','check'))
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--backend', type=pathlib.Path)
    a = parser.parse_args()
    print(json.dumps(run(a.backend.resolve(), a.output.resolve()) if a.stage == 'run' else check(a.output.resolve()), indent=2))
