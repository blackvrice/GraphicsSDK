"""Freeze this bounded Windows implementation's final records; never rewrite old evidence."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile

root = pathlib.Path(__file__).resolve().parents[5]
sys.path.insert(0, str(root / 'tools'))
import audit_licenses as audit
import audit_vulkan as vulkan

validation = root / 'benchmarks/reports/validation/2026-10-23-phase3-vulkan-transfer'
report = root / 'benchmarks/reports/2026-10-23-phase3-vulkan-transfer'
generated = root / 'licenses/generated/windows-sdk-vulkan-transfer-release'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p): return dict(path=p.relative_to(root).as_posix(), sha256=sha(p), bytes=p.stat().st_size)
def load(p): return json.loads(p.read_text(encoding='utf-8'))
def write(p, x): p.write_bytes((json.dumps(x, indent=2) + '\n').encode())

inventory, notices = vulkan.collect(root)
package = root / 'out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-transfer.zip'
required = vulkan.payload(root, notices)
inventory['package'] = vulkan.package_record(root, package, required)
audit.write_or_check(generated, audit.output_files(inventory, notices), True)
assert len(inventory['files']) == 60
assert sha(root/'out/core/Release/GraphicsSDKCore.lib') == 'cebe340ed4e5a9003f581950099725b95d53a0f19cef076981341de2a0be9234'
assert sha(root/'tools/memory_stress_policy.json') == '377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
# Native trace copies remain interpretable without the local MSBuild directory.
traces = inventory['compiler_read_evidence'] + inventory['linker_read_evidence']
for r in traces:
    original = root / r['path']; target = validation / 'build-traces' / original.relative_to(root / 'out')
    target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(original, target)
for path in inventory['projects']:
    source = root / path; target = validation / 'build-traces' / source.relative_to(root / 'out')
    target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source, target)
for directory in ('vulkan-transfer', 'vulkan-consumer', 'vulkan-only', 'vulkan-portable'):
    shutil.copyfile(root / 'out' / directory / 'CMakeCache.txt', validation / (directory + '-cache.txt'))
# Meaningful fail-closed controls: absent notices, altered library, and unreviewed extra payload.
for name, transform in (
    ('missing-notices', lambda p: {k: v for k, v in p.items() if k != 'share/GraphicsSDK/THIRD_PARTY_NOTICES.txt'}),
    ('changed-library', lambda p: {**p, 'lib/GraphicsSDKCore.lib': b'changed'}),
    ('extra-library', lambda p: {**p, 'lib/unreviewed.lib': b'unreviewed'})):
    negative = root / 'out' / ('vulkan-package-negative-' + name + '.zip')
    with zipfile.ZipFile(negative, 'w') as archive:
        for key, value in transform(required).items(): archive.writestr(key, value)
    try: vulkan.package_record(root, negative, required)
    except audit.AuditError: pass
    else: raise AssertionError(name + ' unexpectedly accepted')
(validation/'package-negative-controls.log').write_bytes(b'PASS: missing notices, changed library and unreviewed library rejected\n')
previous = root / 'benchmarks/reports/2026-10-23-phase2-fixed-work/evidence.json'
prior = load(previous)
preserved = {r['path']: r for r in prior['files'] + prior['preserved_inputs'] +
    [prior[k] for k in ('summary', 'source_archive', 'previous_evidence')] + [rec(previous)]}
assert all(sha(root/r['path']) == r['sha256'] for r in preserved.values())
historical = [rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip')) if p != package]
assert len(historical) == 12
assert all(sha(root/r['path']) == r['sha256'] for r in prior['SDK_packages'])
commands = load(validation/'commands.json')
assert len(commands) == 15 and all(c['returncode'] == c['expected'] for c in commands)
for name in ('.gitattributes', 'README.md', 'docs/BUILD.md', 'docs/TESTING.md', 'docs/DEPENDENCY_LICENSES.md',
             'docs/ROADMAP.md', 'docs/VULKAN.md', 'docs/PHASE3_VULKAN_TRANSFER_REPORT.md'):
    p = root / name; p.write_bytes(p.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
report.mkdir(exist_ok=False)
write(report/'summary.json', dict(SDK_source_fingerprint=inventory['SDK_source_fingerprint'],
    production_SDK_changed=True, Vulkan_scope='buffer transfers only', Vulkan_enabled_by_default=False,
    Windows_CTests=dict(integrated=35, Vulkan_only=7, no_backend=6, skipped=0), Python_tests=208,
    native_suite_assertions=717, native_suite_transfers=48, native_suite_verified_bytes=57042546,
    native_suite_configurations=3, native_transfers=144, verified_bytes=171127638,
    adapter_names=['NVIDIA GeForce GTX 1660 SUPER', 'Intel(R) UHD Graphics 770'],
    installed_consumer_verified=True, validation_layer_available=False, validation_layer_accepted=False,
    Linux_native_verified=False, software_Vulkan_verified=False, noncoherent_memory_branch_verified=False,
    rendering_implemented=False, Phase3_complete=False, memory_acceptance=False, Intel_AV_cause_resolved=False,
    memory_and_Intel_investigation_deferred_by_user=True, original_memory_policy_unchanged=True,
    historical_packages_preserved=12, compiler_inputs=60, Vulkan_header_inputs=11, components=2,
    compiler_logs=17, linker_logs=16, inventory=rec(generated/'inventory.json'), SDK_package=inventory['package'], commands=commands))
suffixes = {'.cpp', '.hpp', '.py', '.txt', '.in', '.json', '.md', '.manifest', '.ps1', '.hlsl'}
paths = {p for folder in ('include', 'src', 'cmake', 'tests', 'examples', 'tools', 'shaders')
    for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths |= {root/n for n in ('.gitattributes', 'CMakeLists.txt', 'CMakePresets.json', 'README.md', 'LICENSE')}
paths |= set((root/'docs').glob('*.md')) | set((root/'licenses').glob('*.json'))
paths |= set((root/'benchmarks/competitors').glob('*.json')) | set((validation/'verification-scripts').glob('*.py'))
paths |= {root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'} | set(generated.iterdir())
sources = [rec(p) for p in sorted(paths)]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe', '.dll', '.pdb', '.lib', '.dmp', '.etl') for r in sources)
digest = hashlib.sha256(json.dumps(sources, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
archive = root / f"benchmarks/reports/sources/vulkan-transfer-{inventory['SDK_source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources: z.writestr(r['path'], (root/r['path']).read_bytes())
    z.writestr('source-manifest.json', json.dumps(dict(SDK_source_fingerprint=inventory['SDK_source_fingerprint'],
        source_manifest_sha256=digest, files=sources), indent=2) + '\n')
files = [rec(p) for p in sorted(validation.rglob('*')) if p.is_file()] + [rec(p) for p in sorted(generated.iterdir())]
write(report/'evidence.json', dict(base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    SDK_source_fingerprint=inventory['SDK_source_fingerprint'], summary=rec(report/'summary.json'),
    source_archive=rec(archive), source_files=sources, files=files, preserved_inputs=list(preserved.values()),
    historical_packages=historical, previous_evidence=rec(previous), SDK_package=rec(package),
    native_artifacts=inventory['artifacts']))
print(f'PASS: {len(files)} evidence/audit files, {len(sources)} archive sources; historical evidence and 12 packages preserved')
