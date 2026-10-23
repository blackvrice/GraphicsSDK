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
import audit_vulkan_upload as av
import report_vulkan_upload as report_tool

v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload'
results=root/'benchmarks/results/2026-10-23-vulkan-upload'
report=root/'benchmarks/reports/2026-10-23-phase3-vulkan-upload'
generated=root/'licenses/generated/windows-benchmarks-vulkan-upload-release'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8',newline='')
def rec(p):return audit.record_file(root,p)
def check(r):assert rec(root/r['path'])=={k:r[k] for k in ('path','sha256','bytes')},r['path']

x=report_tool.review(results)
inventory,notices=av.collect(root)
audit.write_or_check(generated,audit.output_files(inventory,notices),True)
assert len(inventory['files'])==646 and len(inventory['components'])==21
for r in inventory['new_compiler_read_evidence']+inventory['linker_read_evidence']+[inventory['project']]:
    source=root/r['path'];target=v/'build-traces'/source.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
shutil.copyfile(root/'out/vulkan-upload-final/CMakeCache.txt',v/'benchmark-cache.txt')
shutil.copyfile(root/'out/vulkan-upload-final/Testing/Temporary/LastTest.log',v/'native-tests.log')
for name in ('changed-inventory','missing-notices'):
    folder=root/'out'/('vulkan-upload-audit-'+name);folder.mkdir(exist_ok=False)
    for path in generated.iterdir():shutil.copyfile(path,folder/path.name)
    if name=='missing-notices':(folder/'THIRD_PARTY_NOTICES.txt').unlink()
    else:(folder/'inventory.json').write_text('{}\n')
    try:audit.write_or_check(folder,audit.output_files(inventory,notices),True)
    except (audit.AuditError,OSError):pass
    else:raise AssertionError('Audit accepted '+name)
(v/'audit-controls.log').write_text('PASS: changed inventory and missing notices rejected; actual compiler/linker closure and reused binaries checked\n')
previous=root/'benchmarks/reports/2026-10-23-phase3-vulkan-presentation/evidence.json';prior=load(previous)
preserved={r['path']:r for r in prior['files']+prior['preserved_inputs']+prior['local_artifacts']+[prior[k] for k in ('summary','source_archive','previous_evidence')]+[rec(previous)]}
for r in preserved.values():check(r)
production=[r for r in prior['source_files'] if r['path'].startswith(('include/','src/')) or r['path']=='CMakeLists.txt']
for r in production:check(r)
assert audit.sdk_fingerprint(root)==prior['SDK_source_fingerprint']==x['SDK_source_fingerprint']
assert audit.sha256((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
packages=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip'))];assert len(packages)==22
for r in prior['historical_packages']+[prior['SDK_package']]:check(r)
summary=load(report/'summary.json');summary.update(inventory=rec(generated/'inventory.json'),compiler_inputs=646,components=21,new_compiler_logs=1,new_linker_logs=1,historical_packages_preserved=22,
    raw_commands=64,correctness_images=64,measured_run_images_including_warmup=1260,prior_SDK_source_fingerprint=prior['SDK_source_fingerprint'])
write(report/'summary.json',summary)
# Source-only reproduction archive; vendor trees and executables/libraries are prerequisites.
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl','.spvasm'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders','benchmarks/vulkan_upload') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/competitors/sources.lock.json')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((root/'benchmarks/competitors').glob('*.json'))
paths|=set((v/'verification-scripts').glob('*.py'))|set(generated.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'}
sources=[rec(p) for p in sorted(paths)];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f"benchmarks/reports/sources/vulkan-upload-{x['source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=x['SDK_source_fingerprint'],benchmark_source_fingerprint=x['source_fingerprint'],source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for folder in (v,results,generated) for p in sorted(folder.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.etl') for r in files+sources)
local=inventory['artifacts']+inventory['reused_compiler_read_evidence']+load(results/'runtime-receipt.json')['artifacts']
write(report/'evidence.json',dict(schema_version=1,base_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),SDK_source_fingerprint=x['SDK_source_fingerprint'],benchmark_source_fingerprint=x['source_fingerprint'],
    summary=rec(report/'summary.json'),metrics=rec(report/'metrics.csv'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=list(preserved.values()),unchanged_production_sources=production,
    historical_packages=packages,previous_evidence=rec(previous),local_artifacts=local))
print(f'PASS: {len(files)} new evidence files, {len(sources)} source files, 22 SDK packages and complete prior evidence preserved; SDK production sources unchanged')
