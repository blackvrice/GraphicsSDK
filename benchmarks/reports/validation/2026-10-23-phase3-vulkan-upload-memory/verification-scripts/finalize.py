"""Freeze current build evidence without overwriting historical artifacts."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile

root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as a
import audit_vulkan as av
import audit_vulkan_upload_compare as ac
import report_vulkan_upload_compare as review
import vulkan_upload_experiment as experiment

v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory'
results=root/'benchmarks/results/2026-10-23-vulkan-upload-memory'
report=root/'benchmarks/reports/2026-10-23-phase3-vulkan-upload-memory'
sdk=root/'licenses/generated/windows-sdk-vulkan-upload-memory-release'
bench=root/'licenses/generated/windows-benchmarks-vulkan-upload-memory-release'
package=root/'out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-upload-memory.zip'
def load(p):return json.loads(p.read_bytes())
def rec(p):return a.record_file(root,p)
def write(p,x):p.write_bytes((json.dumps(x,indent=2)+'\n').encode())
def check(r):assert rec(root/r['path'])=={k:r[k] for k in ('path','sha256','bytes')},r['path']

x=review.review(results);candidate,archived=experiment.candidate();av.configure_variant('upload-memory')
si=load(sdk/'inventory.json');notices=(sdk/'THIRD_PARTY_NOTICES.txt').read_text(encoding='utf-8')
required=av.payload(root,notices)
for name in ('README.md','VULKAN.md'):required['share/GraphicsSDK/'+name]=archived['README.md' if name=='README.md' else 'docs/VULKAN.md']
assert av.package_record(root,package,required)==si['package']
bi,bn=ac.collect();a.write_or_check(bench,a.output_files(bi,bn),True)
assert len(si['files'])==85 and len(bi['files'])==676 and len(bi['components'])==21
records=si['compiler_read_evidence']+si['linker_read_evidence']+bi['compiler_read_evidence']+bi['linker_read_evidence']
records += [rec(root/p) for p in si['projects']]+bi['projects']
for r in records:
    p=root/r['path'];target=v/'build-traces'/p.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
for folder in ('vulkan-upload-memory','vulkan-upload-memory-consumer','vulkan-upload-memory-only','vulkan-upload-memory-portable','vulkan-upload-memory-bench','vulkan-upload-profile-before','vulkan-upload-profile-trial','vulkan-upload-profile-after','wsi-probe/build'):
    shutil.copyfile(root/'out'/folder/'CMakeCache.txt',v/(folder.replace('/','-')+'-cache.txt'))
for name,change in [('missing-notices',lambda p:{k:d for k,d in p.items() if k!='share/GraphicsSDK/THIRD_PARTY_NOTICES.txt'}),('changed-library',lambda p:{**p,'lib/GraphicsSDKCore.lib':b'changed'}),('extra-library',lambda p:{**p,'lib/unreviewed.lib':b'extra'})]:
    negative=root/'out'/('vulkan-upload-memory-negative-'+name+'.zip')
    if not negative.exists():
        with zipfile.ZipFile(negative,'x') as z:
            for k,d in change(required).items():z.writestr(k,d)
    else:
        with zipfile.ZipFile(negative) as z:assert {k:z.read(k) for k in z.namelist()}==change(required)
    try:av.package_record(root,negative,required)
    except a.AuditError:pass
    else:raise AssertionError(name)
for name in ('changed-inventory','missing-notices'):
    folder=root/'out'/('vulkan-upload-memory-audit-'+name);folder.mkdir(exist_ok=True)
    for p in bench.iterdir():shutil.copyfile(p,folder/p.name)
    if name=='changed-inventory':(folder/'inventory.json').write_text('{}\n')
    else:(folder/'THIRD_PARTY_NOTICES.txt').unlink()
    try:a.write_or_check(folder,a.output_files(bi,bn),True)
    except (a.AuditError,OSError):pass
    else:raise AssertionError(name)
(v/'audit-controls.log').write_bytes(b'PASS: missing package notices, changed library, extra library, altered comparison inventory and missing comparison notices rejected\n')
previous=root/'benchmarks/reports/2026-10-23-phase3-vulkan-upload/evidence.json';prior=load(previous)
preserved={r['path']:r for r in prior['files']+prior['preserved_inputs']+prior['local_artifacts']+[prior[k] for k in ('summary','metrics','source_archive','previous_evidence')]+[rec(previous)]}
for r in preserved.values():check(r)
for r in prior['source_files']:
    if r['path'].startswith(('include/','src/')) or r['path']=='CMakeLists.txt':check(r)
historical=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip')) if p!=package];assert len(historical)==22
for r in prior['historical_packages']:check(r)
assert a.sha256((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
# Reconstruct the rejected trial's production fingerprint from the frozen old source manifest.
trial_sources=[dict(r) for r in prior['source_files'] if r['path'].startswith(('include/','src/')) or r['path']=='CMakeLists.txt']
for r in trial_sources:
    if r['path']=='src/backends/vulkan/Vulkan.cpp':r.update(sha256=a.sha256((v/'profile-trial/Vulkan.cpp').read_bytes()),bytes=(v/'profile-trial/Vulkan.cpp').stat().st_size)
trial_fp=hashlib.sha256(''.join(r['path']+'|'+r['sha256']+'\n' for r in sorted(trial_sources,key=lambda r:r['path'])).encode()).hexdigest()
assert trial_fp=='7f3845c751ae9ac808b9a5089329a8781d53e971afa6da883a88b07a9c97f35f'
write(v/'profile-trial/source-manifest.json',dict(SDK_source_fingerprint=trial_fp,base_archive=prior['source_archive'],replacement=rec(v/'profile-trial/Vulkan.cpp'),files=trial_sources,adopted=False))
summary=load(report/'summary.json');summary.update(candidate_SDK_inventory=rec(sdk/'inventory.json'),comparison_inventory=rec(bench/'inventory.json'),candidate_SDK_package=rec(package),candidate_SDK_compiler_inputs=85,candidate_SDK_components=2,candidate_SDK_compiler_logs=37,candidate_SDK_linker_logs=36,comparison_inputs=676,comparison_components=21,comparison_compiler_logs=5,comparison_linker_logs=5,historical_packages_preserved=22)
write(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl','.spvasm'}
paths={p for f in ('include','src','cmake','tests','examples','tools','shaders','benchmarks/vulkan_upload','benchmarks/vulkan_upload_profile') for p in (root/f).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((root/'benchmarks/competitors').glob('*.json'))
paths|=set((v/'verification-scripts').glob('*.py'))|set(sdk.iterdir())|set(bench.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'}
sources=[rec(p) for p in sorted(paths)];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f"benchmarks/reports/sources/vulkan-upload-memory-{x['before_SDK_source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=x['before_SDK_source_fingerprint'],tested_candidate_SDK_source_fingerprint=x['after_SDK_source_fingerprint'],source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for folder in (v,results,sdk,bench) for p in sorted(folder.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.etl') for r in files+sources)
local={r['path']:r for r in si['artifacts']+bi['artifacts']+records+load(v/'runtime-receipt.json')['artifacts']}
superseded=[]
if (report/'evidence.json').exists():
    last=load(report/'evidence.json');superseded=last.get('superseded_source_archives',[])
    if last['source_archive']!=rec(archive):superseded.append(last['source_archive'])
for r in superseded:check(r)
write(report/'evidence.json',dict(schema_version=1,base_commit=subprocess.check_output(['git','-c','gc.auto=0','-c','maintenance.auto=false','rev-parse','HEAD'],cwd=root,text=True).strip(),SDK_source_fingerprint=x['before_SDK_source_fingerprint'],tested_candidate_SDK_source_fingerprint=x['after_SDK_source_fingerprint'],candidate_adopted=False,benchmark_source_fingerprint=x['benchmark_source_fingerprint'],
    summary=rec(report/'summary.json'),metrics=rec(report/'metrics.csv'),SDK_package=rec(package),source_archive=rec(archive),superseded_source_archives=superseded,source_files=sources,files=files,preserved_inputs=list(preserved.values()),historical_packages=historical,previous_evidence=rec(previous),local_artifacts=list(local.values())))
print(f'PASS: {len(files)} evidence files / {len(sources)} source members; 22 historical SDK ZIPs preserved, one new package')
