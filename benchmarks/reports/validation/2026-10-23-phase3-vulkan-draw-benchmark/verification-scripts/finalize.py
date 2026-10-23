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
import audit_vulkan_draw as av
import report_vulkan_draw as report_tool

v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw-benchmark'
results=root/'benchmarks/results/2026-10-23-vulkan-draw'
report=root/'benchmarks/reports/2026-10-23-phase3-vulkan-draw-benchmark'
generated=root/'licenses/generated/windows-benchmarks-vulkan-draw-release'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8',newline='')
def rec(p):return audit.record_file(root,p)
def check(r):assert rec(root/r['path'])=={k:r[k] for k in ('path','sha256','bytes')},r['path']

x=report_tool.review(results)
inventory,notices=av.collect(root)
audit.write_or_check(generated,audit.output_files(inventory,notices),True)
assert len(inventory['files'])==648 and len(inventory['components'])==21
for r in inventory['new_compiler_read_evidence']+inventory['linker_read_evidence']+inventory['projects']:
    source=root/r['path'];target=v/'build-traces'/source.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
shutil.copyfile(root/'out/vulkan-draw-benchmark-vulkan11/CMakeCache.txt',v/'benchmark-cache.txt')
shutil.copyfile(root/'out/vulkan-draw-benchmark-vulkan11/Testing/Temporary/LastTest.log',v/'native-tests.log')
# The separate Vulkan-1.0 assembler experiment is local-only and was not used in final timings.
tool_build=root/'out/vulkan-draw-benchmark-shader-tools'
for p in list(tool_build.rglob('CL.read.1.tlog'))+list(tool_build.rglob('link.read.1.tlog'))+[tool_build/'spirv_fixture_tool.vcxproj',tool_build/'CMakeCache.txt']:
    if p.suffix=='.tlog' and 'Release' not in p.parts:continue
    target=v/'shader-tools/build-traces'/p.relative_to(tool_build);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
header=root/'out/vulkan-validation-runtime/prerequisites/include/spirv-tools/libspirv.h'
upstream=root/'.deps/vvl-validation/SPIRV-Tools/include/spirv-tools/libspirv.h'
assert header.read_bytes()==upstream.read_bytes()
write(v/'shader-tools/source-reconstruction.json',dict(compiled_source=load(v/'shader-tools/receipt.json')['source'],snapshot=rec(v/'initial-spirv10/tool-source.cpp'),installed_header=rec(header),upstream_header=rec(upstream),current_tool_source_restored=True,used_in_final_comparison=False))
for name in ('changed-inventory','missing-notices'):
    folder=root/'out'/('vulkan-draw-benchmark-audit-'+name);folder.mkdir(exist_ok=False)
    for path in generated.iterdir():shutil.copyfile(path,folder/path.name)
    if name=='missing-notices':(folder/'THIRD_PARTY_NOTICES.txt').unlink()
    else:(folder/'inventory.json').write_text('{}\n')
    try:audit.write_or_check(folder,audit.output_files(inventory,notices),True)
    except (audit.AuditError,OSError):pass
    else:raise AssertionError('Audit accepted '+name)
(v/'audit-controls.log').write_text('PASS: changed inventory and missing notices rejected; actual compiler/linker closure and reused binaries checked\n')
previous=root/'benchmarks/reports/2026-10-23-phase3-vulkan-upload-memory/evidence.json';prior=load(previous)
preserved={r['path']:r for r in prior['files']+prior['preserved_inputs']+prior['local_artifacts']+prior.get('superseded_source_archives',[])+[prior[k] for k in ('summary','metrics','source_archive','previous_evidence')]+[rec(previous)]}
for r in preserved.values():check(r)
production=[r for r in prior['source_files'] if r['path'].startswith(('include/','src/')) or r['path']=='CMakeLists.txt']
for r in production:check(r)
assert audit.sdk_fingerprint(root)==prior['SDK_source_fingerprint']==x['SDK_source_fingerprint']
assert audit.sha256((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
packages=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip'))];assert len(packages)==23
for r in prior['historical_packages']+[prior['SDK_package']]:check(r)
summary=load(report/'summary.json');summary.update(inventory=rec(generated/'inventory.json'),compiler_inputs=648,components=21,new_compiler_logs=2,new_linker_logs=2,historical_packages_preserved=23,
    raw_commands=60,correctness_images=48,measured_run_images_including_warmup=1260,prior_SDK_source_fingerprint=prior['SDK_source_fingerprint'])
write(report/'summary.json',summary)
# Source-only reproduction archive; vendor trees and executables/libraries are prerequisites.
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl','.spvasm'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders','benchmarks/vulkan_draw') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp','benchmarks/competitors/sources.lock.json')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((root/'benchmarks/competitors').glob('*.json'))
paths|=set((v/'verification-scripts').glob('*.py'))|set(generated.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'}
sources=[rec(p) for p in sorted(paths)];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f"benchmarks/reports/sources/vulkan-draw-benchmark-{x['source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=x['SDK_source_fingerprint'],benchmark_source_fingerprint=x['source_fingerprint'],source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for folder in (v,results,generated) for p in sorted(folder.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.etl') for r in files+sources)
local=inventory['artifacts']+inventory['reused_compiler_read_evidence']+load(results/'runtime-receipt.json')['artifacts']
for folder in ('initial-spirv13','initial-spirv10'):
    local+=load(v/folder/'inventory/inventory.json')['artifacts']
local+=load(v/'initial-audit/artifacts.json')['artifacts']
tool=load(v/'shader-tools/receipt.json')
local+=[tool['tool'],tool['prerequisite_library']]+[r['binary'] for r in tool['fixtures']]+tool['license_files']
local+=[rec(header),rec(upstream)]
for r in local:check(r)
assert audit.sha256((v/'initial-spirv10/tool-source.cpp').read_bytes())==tool['source']['sha256']
assert audit.sha256((v/'initial-spirv10/DrawShaders.hpp').read_bytes())==tool['header']['sha256']
write(report/'evidence.json',dict(schema_version=1,base_commit=subprocess.check_output(['git','-c','gc.auto=0','-c','maintenance.auto=false','rev-parse','HEAD'],cwd=root,text=True).strip(),SDK_source_fingerprint=x['SDK_source_fingerprint'],benchmark_source_fingerprint=x['source_fingerprint'],
    summary=rec(report/'summary.json'),metrics=rec(report/'metrics.csv'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=list(preserved.values()),unchanged_production_sources=production,
    historical_packages=packages,previous_evidence=rec(previous),local_artifacts=local))
print(f'PASS: {len(files)} new evidence files, {len(sources)} source files, 23 SDK packages and complete prior evidence preserved; SDK production sources unchanged')
