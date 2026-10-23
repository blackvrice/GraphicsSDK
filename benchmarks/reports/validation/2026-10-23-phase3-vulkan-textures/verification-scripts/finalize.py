import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile

root=pathlib.Path(__file__).resolve().parents[5]
sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import audit_vulkan as av
v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-textures'
report=root/'benchmarks/reports/2026-10-23-phase3-vulkan-textures'
generated=root/'licenses/generated/windows-sdk-vulkan-textures-release'
package=root/'out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-textures.zip'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return dict(path=p.relative_to(root).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_bytes((json.dumps(x,indent=2)+'\n').encode())

av.configure_variant('textures');inventory,notices=av.collect(root)
required=av.payload(root,notices);inventory['package']=av.package_record(root,package,required)
audit.write_or_check(generated,audit.output_files(inventory,notices),True)
assert len(inventory['files'])==61
for r in inventory['compiler_read_evidence']+inventory['linker_read_evidence']:
    original=root/r['path'];target=v/'build-traces'/original.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,target)
for path in inventory['projects']:
    original=root/path;target=v/'build-traces'/original.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,target)
for folder in ('vulkan-textures','vulkan-textures-consumer','vulkan-textures-only','vulkan-textures-portable'):
    shutil.copyfile(root/'out'/folder/'CMakeCache.txt',v/(folder+'-cache.txt'))
for name,transform in (('missing-notices',lambda p:{k:x for k,x in p.items() if k!='share/GraphicsSDK/THIRD_PARTY_NOTICES.txt'}),
    ('changed-library',lambda p:{**p,'lib/GraphicsSDKCore.lib':b'changed'}),('extra-library',lambda p:{**p,'lib/unreviewed.lib':b'extra'})):
    negative=root/'out'/('vulkan-textures-negative-'+name+'.zip')
    with zipfile.ZipFile(negative,'w') as z:
        for k,data in transform(required).items():z.writestr(k,data)
    try:av.package_record(root,negative,required)
    except audit.AuditError:pass
    else:raise AssertionError(name)
(v/'package-controls.log').write_bytes(b'PASS: missing notices, changed library and extra library rejected\n')
previous=root/'benchmarks/reports/2026-10-23-phase3-vulkan-validation/evidence.json';prior=load(previous)
preserved={r['path']:r for r in prior['files']+prior['preserved_inputs']+prior['local_artifacts']+
    [prior[k] for k in ('summary','source_archive','previous_evidence')]+[rec(previous)]}
assert all(sha(root/r['path'])==r['sha256'] for r in preserved.values())
historical=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip')) if p!=package];assert len(historical)==14
assert all(sha(root/r['path'])==r['sha256'] for r in prior['historical_packages']+[prior['SDK_package']])
assert sha(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
assert load(v/'runtime-receipt.json')==load(root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-validation/runtime-receipt.json')
for name in ('.gitattributes','README.md','docs/ROADMAP.md','docs/BUILD.md','docs/TESTING.md','docs/VULKAN.md','docs/BACKENDS.md',
    'docs/DEPENDENCY_LICENSES.md','docs/PHASE3_VULKAN_TEXTURE_REPORT.md','tools/audit_vulkan.py'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
report.mkdir(exist_ok=False)
write(report/'summary.json',dict(SDK_source_fingerprint=inventory['SDK_source_fingerprint'],production_SDK_changed=True,
    Windows_CTests=dict(integrated=39,Vulkan_only=11,no_backend=6,skipped=0),Python_tests=208,
    texture_suite=dict(assertions=835,roundtrips=74,verified_texel_bytes=499371832,padded_roundtrips=40,adapters=2),
    validation_texture_roundtrips=222,validation_texture_bytes=1498115496,total_texture_roundtrips=370,total_texture_bytes=2496859160,
    buffer_suite=dict(assertions=717,transfers=48,verified_bytes=57042546,adapters=2),
    native_probe=dict(assertions=274,adapters=2,native_core_errors=64,native_sync_hazards=2,synthetic_errors=2050,invalid_GPU_submissions=0),
    normal_SDK_validation_errors=0,required_tests_do_not_skip=True,installed_consumers_verified=True,local_runtime_reused=True,
    external_runtime_bundled=False,public_API_changed=False,rendering_implemented=False,GPU_assisted_validation=False,Linux_native_verified=False,
    Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False,deferred_memory_investigation_unchanged=True,
    historical_packages_preserved=14,compiler_inputs=61,components=2,compiler_logs=18,linker_logs=17,
    inventory=rec(generated/'inventory.json'),SDK_package=inventory['package']))
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((root/'benchmarks/competitors').glob('*.json'))
paths|=set((v/'verification-scripts').glob('*.py'))|set(generated.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'}
sources=[rec(p) for p in sorted(paths)];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f"benchmarks/reports/sources/vulkan-textures-{inventory['SDK_source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=inventory['SDK_source_fingerprint'],source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for p in sorted(v.rglob('*')) if p.is_file()]+[rec(p) for p in sorted(generated.iterdir())]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.pdb','.lib','.dmp','.etl') for r in files+sources)
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
    SDK_source_fingerprint=inventory['SDK_source_fingerprint'],summary=rec(report/'summary.json'),source_archive=rec(archive),source_files=sources,
    files=files,preserved_inputs=list(preserved.values()),historical_packages=historical,previous_evidence=rec(previous),SDK_package=rec(package),
    local_artifacts=inventory['artifacts']+load(v/'runtime-receipt.json')['artifacts']))
print(f'PASS: {len(files)} evidence/audit files, {len(sources)} source archive members; history/14 packages preserved')
