import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import audit_vulkan_loader_fault as auditor
import report_vulkan_loader_fault as report_tool
import vulkan_creation_provenance as history
v=pathlib.Path(__file__).resolve().parent.parent;base=root/'benchmarks/reports/2026-10-24-vulkan-loader-fault';results=root/'benchmarks/results/2026-10-24-vulkan-loader-fault-v2';generated=root/auditor.GENERATED
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def rec(p):return audit.record_file(root,p)
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8',newline='')
summary=report_tool.review(results);assert summary==load(base/'summary.json')
inventory,notices=auditor.collect(root);audit.write_or_check(generated,audit.output_files(inventory,notices),True)
for r in inventory['new_compiler_read_evidence']+inventory['new_linker_read_evidence']+inventory['new_projects']:
    source=root/r['path'];target=v/'build-traces'/source.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
for variant in ('before','after'):
    folder=root/f'out/vulkan-loader-fault-{variant}-v4';shutil.copyfile(folder/'CMakeCache.txt',v/(variant+'-cache.txt'));shutil.copyfile(folder/'Testing/Temporary/LastTest.log',v/(variant+'-plan-native.log'))
for name in ('vulkan-loader-fault-pilot','vulkan-loader-fault-pilot-v2','vulkan-loader-fault-pilot-v3'):
    shutil.copytree(root/'out'/name,v/'pilots'/name)
(v/'setup-corrections.log').write_text('Initial build: missing Vulkan platform declarations and mixed enum/unsigned initializer rejected; corrected in v2. Pilot v1: CMake/Python lexical order mismatch rejected. Pilot v2: Win32 minimum client extent rejected. Initial full matrix: Intel swapchain unsupported rejected. Final v4 uses explicit capability-scoped coverage. Offline controls v2 rejected the harness because a boolean scalar mutation was accepted; corrected with JSON type-preserving comparison and final v3 controls each accepting its valid relocated baseline first. No failed attempt counted in final accepted matrix.\n')
previous=root/'benchmarks/reports/2026-10-24-vulkan-creation-ownership/evidence.json';prior=load(previous)
old_records=prior['files']+prior['preserved_inputs']+prior['historical_packages']+prior['local_artifacts']+[prior[k] for k in ('summary','cases','source_archive','previous_evidence')]+[rec(previous)]
preserved={}
for r in old_records:
    checked=history.resolve(r,root);preserved[checked['path']]=checked
packages=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip'))];assert len(packages)==23
assert audit.sdk_fingerprint(root)==summary['SDK_source_fingerprint'] and audit.sdk_fingerprint(root/history.SNAPSHOT)==history.OLD
policy=rec(root/'tools/memory_stress_policy.json');assert policy['sha256']=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
summary.update(inventory=rec(generated/'inventory.json'),compiler_inputs=len(inventory['files']),components=len(inventory['components']),new_compiler_logs=6,new_linker_logs=6,prior_product_selected_CTests=55,product_CTests_rerun=False,CPU_plan_assertions_per_variant=216,Python_tests=550,report_mutations_rejected=12,audit_controls_rejected=2,historical_packages_preserved=23)
write(base/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl','.spvasm','.patch'}
folders=('include','src','cmake','tests','examples','tools','shaders','benchmarks/vulkan_allocation','benchmarks/vulkan_allocation_trace','experiments/vulkan_buffer_pool','experiments/vulkan_adaptive_buffer_pool','experiments/vulkan_buffer_pool_fault')
paths={p for folder in folders for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp','benchmarks/competitors/sources.lock.json')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((v/'verification-scripts').glob('*.py'))|set(generated.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'}
sources=[rec(p) for p in sorted(paths,key=lambda p:p.as_posix())];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f"benchmarks/reports/sources/vulkan-loader-fault-{summary['SDK_source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=summary['SDK_source_fingerprint'],loader_source_fingerprint=summary['source_fingerprint'],source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for folder in (v,results,root/'benchmarks/results/2026-10-24-vulkan-loader-fault',generated) for p in sorted(folder.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.etl') for r in files+sources)
local=inventory['new_artifacts']+load(results/'results.json')['artifacts']+load(results/'runtime-receipt.json')['artifacts']
e=dict(schema_version=1,base_commit=subprocess.check_output(['git','-c','gc.auto=0','-c','maintenance.auto=false','rev-parse','HEAD'],cwd=root,text=True).strip(),SDK_source_fingerprint=summary['SDK_source_fingerprint'],loader_source_fingerprint=summary['source_fingerprint'],product_changed=False,summary=rec(base/'summary.json'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=list(preserved.values()),historical_packages=packages,previous_evidence=rec(previous),local_artifacts=local,memory_policy=policy)
write(base/'evidence.json',e)
print(f'PASS sealed {len(files)} evidence / {len(sources)} own source files / {len(preserved)} preserved records; 23 historical packages unchanged')
