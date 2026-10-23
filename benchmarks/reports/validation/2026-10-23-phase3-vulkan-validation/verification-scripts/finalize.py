import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile

root=pathlib.Path(__file__).resolve().parents[5]
sys.path.insert(0,str(root/'tools'))
import audit_licenses as a
import audit_vulkan as av
v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-validation'
report=root/'benchmarks/reports/2026-10-23-phase3-vulkan-validation'
generated=root/'licenses/generated/windows-sdk-vulkan-validation-release'
runtime=root/'out/vulkan-validation-runtime'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return dict(path=p.relative_to(root).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_bytes((json.dumps(x,indent=2)+'\n').encode())
av.configure_variant('validation');inventory,notices=av.collect(root)
package=root/'out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-validation.zip'
required=av.payload(root,notices);inventory['package']=av.package_record(root,package,required)
a.write_or_check(generated,a.output_files(inventory,notices),True)
assert len(inventory['files'])==60
for r in inventory['compiler_read_evidence']+inventory['linker_read_evidence']:
    original=root/r['path'];target=v/'build-traces'/original.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,target)
for path in inventory['projects']:
    original=root/path;target=v/'build-traces'/original.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,target)
for folder in ('vulkan-validation','vulkan-validation-consumer','vulkan-validation-only','vulkan-validation-portable'):
    shutil.copyfile(root/'out'/folder/'CMakeCache.txt',v/(folder+'-cache.txt'))
for log in runtime.glob('*.log'):
    target=v/'runtime-build'/log.name;target.parent.mkdir(exist_ok=True);shutil.copyfile(log,target)
shutil.copyfile(runtime/'build/Vulkan-ValidationLayers/CMakeCache.txt',v/'runtime-cache.txt')
ledger=[]
for c in load(root/'tools/vulkan-validation.lock.json')['components']:
    folder=root/'.deps'/(c['name'] if c['name'] in ('Vulkan-Headers','Vulkan-ValidationLayers') else 'vvl-validation/'+c['name'])
    actual=subprocess.check_output(['git','-C',str(folder),'rev-parse','HEAD'],text=True).strip();assert actual==c['commit']
    assert not subprocess.check_output(['git','-C',str(folder),'status','--porcelain','--untracked-files=no'],text=True).strip()
    ledger.append(dict(**c,tracked_checkout_clean=True,license_files=[rec(p) for p in sorted(folder.glob('LICENSE*')) if p.is_file()]))
write(v/'runtime-provenance.json',dict(scope='Local-only tool provenance; not full runtime redistribution/reachability approval',components=ledger))
# Independent package integrity controls never modify the released candidate ZIP.
for name,transform in (('missing-notices',lambda p:{k:x for k,x in p.items() if k!='share/GraphicsSDK/THIRD_PARTY_NOTICES.txt'}),
    ('changed-library',lambda p:{**p,'lib/GraphicsSDKCore.lib':b'changed'}),('extra-library',lambda p:{**p,'lib/unreviewed.lib':b'extra'})):
    negative=root/'out'/('vulkan-validation-negative-'+name+'.zip')
    with zipfile.ZipFile(negative,'w') as z:
        for k,data in transform(required).items():z.writestr(k,data)
    try:av.package_record(root,negative,required)
    except a.AuditError:pass
    else:raise AssertionError(name)
(v/'package-controls.log').write_bytes(b'PASS: missing notices, changed library and extra library rejected\n')
previous=root/'benchmarks/reports/2026-10-23-phase3-vulkan-transfer/evidence.json';prior=load(previous)
preserved={r['path']:r for r in prior['files']+prior['preserved_inputs']+[prior[k] for k in ('summary','source_archive','previous_evidence')]+[rec(previous)]}
assert all(sha(root/r['path'])==r['sha256'] for r in preserved.values())
historical=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip')) if p!=package];assert len(historical)==13
assert all(sha(root/r['path'])==r['sha256'] for r in prior['historical_packages']+[prior['SDK_package']])
assert sha(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
for name in ('.gitattributes','README.md','docs/ROADMAP.md','docs/BUILD.md','docs/TESTING.md','docs/VULKAN.md','docs/DEPENDENCY_LICENSES.md','docs/PHASE3_VULKAN_VALIDATION_REPORT.md','tools/audit_vulkan.py','tools/prepare_vulkan_validation.py'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
report.mkdir(exist_ok=False)
write(report/'summary.json',dict(SDK_source_fingerprint=inventory['SDK_source_fingerprint'],production_SDK_changed=True,
    Windows_CTests=dict(integrated=37,Vulkan_only=9,no_backend=6,skipped=0),Python_tests=208,
    SDK_validation_suite_assertions=717,SDK_validation_transfers=144,SDK_validation_verified_bytes=171127638,
    SDK_driver_only_control_transfers=96,total_SDK_transfers=240,total_verified_bytes=285212730,
    native_probe=dict(assertions=274,adapters=2,native_core_errors=64,native_sync_hazards=2,sync_disabled_controls=2,synthetic_errors=2050,invalid_GPU_submissions=0),
    required_tests_do_not_skip=True,local_runtime_only=True,system_installation=False,registry_changed=False,
    installed_consumer_verified=True,synchronization_validation_accepted=True,GPU_assisted_validation=False,
    Linux_native_verified=False,Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False,
    deferred_memory_investigation_unchanged=True,historical_packages_preserved=13,
    compiler_inputs=60,components=2,compiler_logs=16,linker_logs=15,inventory=rec(generated/'inventory.json'),SDK_package=inventory['package']))
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((root/'benchmarks/competitors').glob('*.json'))
paths|=set((v/'verification-scripts').glob('*.py'))|set(generated.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'}
sources=[rec(p) for p in sorted(paths)];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f"benchmarks/reports/sources/vulkan-validation-{inventory['SDK_source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=inventory['SDK_source_fingerprint'],source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for p in sorted(v.rglob('*')) if p.is_file()]+[rec(p) for p in sorted(generated.iterdir())]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.pdb','.lib','.dmp','.etl') for r in files+sources)
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
    SDK_source_fingerprint=inventory['SDK_source_fingerprint'],summary=rec(report/'summary.json'),source_archive=rec(archive),source_files=sources,
    files=files,preserved_inputs=list(preserved.values()),historical_packages=historical,previous_evidence=rec(previous),SDK_package=rec(package),
    local_artifacts=inventory['artifacts']+load(v/'runtime-receipt.json')['artifacts']))
print(f'PASS: {len(files)} evidence/audit files, {len(sources)} source archive members; history/13 packages preserved')
