"""Fail-closed GNU dependency, relocated package and notice checks (no MSBuild inference)."""
import hashlib
import pathlib
import shlex
import zipfile


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def dependencies(text, cwd):
    text = text.replace('\\\r\n', '').replace('\\\n', '')
    require(': ' in text or ':\n' in text, 'Missing GNU dependency target')
    result = sorted({(pathlib.Path(cwd) / p).resolve() for p in shlex.split(text.split(':', 1)[1])})
    require(result and all(p.is_file() and not p.is_symlink() for p in result), 'Missing dependency file')
    return result


def classify(path, own, installed, copied, vendor):
    # Exact allowlists precede system roots. A repository path is never a toolchain input.
    for name, paths in (('own', own), ('installed', installed), ('copied', copied), ('vendor', vendor)):
        if path in paths:
            return name
    if any(path.is_relative_to(pathlib.Path(p)) for p in ('/usr/include', '/usr/lib', '/lib')):
        return 'system'
    raise ValueError('Unreviewed compiler/linker input: ' + str(path))


def audit_dependencies(paths, kind, own, installed, copied, vendor):
    rows = []
    for path in paths:
        category = classify(path, own, installed, copied, vendor)
        require(category in ({'own', 'vendor', 'system'} if kind == 'sdk' else {'installed', 'copied', 'system'}),
                'Consumer source/vendor leakage or SDK package leakage: ' + str(path))
        rows.append(dict(path=str(path), category=category, sha256=sha(path), bytes=path.stat().st_size))
    return rows


def notice(root, vendor_paths, apache):
    sections = ['GraphicsSDK installed static SDK verification payload\n',
                'GraphicsSDK — MIT\n' + (root / 'LICENSE').read_text(),
                'Compiled using reviewed Vulkan-Headers. Selected license: Apache-2.0.\n'
                'The external Vulkan loader, ICD, validation layer and system/toolchain files are not bundled.\n']
    for path in sorted(vendor_paths):
        text = path.read_text()
        require('SPDX-License-Identifier: Apache-2.0' in text, 'Unreviewed header license: ' + str(path))
        start = text.find('/*')
        end = text.find('*/', start)
        require(start >= 0 and end > start and 'Copyright' in text[start:end], 'Header copyright preamble missing')
        sections.append(path.relative_to(root / '.deps/Vulkan-Headers').as_posix() + '\n' + text[start:end + 2])
    sections.append('Apache License 2.0 — full text\n' + apache.read_text())
    return '\n\n'.join(sections) + '\n'


def payload(prefix, headers, expected_notice, library_sha):
    expected = {'lib/libGraphicsSDKCore.a', 'share/GraphicsSDK/LICENSE', 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt'}
    expected |= {'include/graphics/' + p.name for p in headers}
    expected |= {'lib/cmake/GraphicsSDK/' + n for n in ('GraphicsSDKTargets.cmake', 'GraphicsSDKTargets-release.cmake', 'GraphicsSDKConfig.cmake', 'GraphicsSDKConfigVersion.cmake')}
    entries = list(prefix.rglob('*'))
    require(not any(p.is_symlink() for p in entries), 'Payload symlink rejected')
    actual = {p.relative_to(prefix).as_posix() for p in entries if p.is_file()}
    require(actual == expected, 'Payload file allowlist differs: ' + str(actual ^ expected))
    require(sha(prefix / 'lib/libGraphicsSDKCore.a') == library_sha, 'Installed archive changed')
    for header in headers:
        require(sha(prefix / 'include/graphics' / header.name) == sha(header), 'Installed header changed')
    require((prefix / 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt').read_bytes() == expected_notice.encode(), 'Missing or changed third-party notice')
    return [dict(path=p, bytes=(prefix / p).stat().st_size, sha256=sha(prefix / p)) for p in sorted(actual)]


def package_zip(prefix, destination, inventory):
    with zipfile.ZipFile(destination, 'x', zipfile.ZIP_DEFLATED) as archive:
        for row in inventory:
            info = zipfile.ZipInfo(row['path'], (2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, (prefix / row['path']).read_bytes())
    with zipfile.ZipFile(destination) as archive:
        require(archive.namelist() == [r['path'] for r in inventory], 'ZIP payload list differs')
        for row in inventory:
            require(hashlib.sha256(archive.read(row['path'])).hexdigest() == row['sha256'], 'ZIP payload bytes differ')
