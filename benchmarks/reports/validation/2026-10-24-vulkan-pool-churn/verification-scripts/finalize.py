import csv
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import audit_vulkan_pool_churn as auditor
import report_vulkan_pool_churn as report
import run_vulkan_pool_churn as native
import vulkan_creation_provenance as history
v=pathlib.Path(__file__).resolve().parent.parent;base=root/'benchmarks/reports/2026-10-24-vulkan-pool-churn';results=root/'benchmarks/results/2026-10-24-vulkan-pool-churn';generated=root/auditor.GENERATED
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def rec(p):return audit.record_file(root,p)
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8',newline='')
summary=report.review(results);assert summary==load(base/'summary.json');inventory,notices=auditor.collect();audit.write_or_check(generated,audit.output_files(inventory,notices),True)
for r in inventory['compiler_read_evidence']+inventory['linker_read_evidence']+inventory['projects']:
    source=root/r['path'];target=v/'build-traces'/source.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
for name in ('vulkan-pool-churn-sdk','vulkan-pool-churn-product-v2','vulkan-pool-churn-candidate-v2'):
    folder=root/'out'/name;shutil.copyfile(folder/'CMakeCache.txt',v/(name+'-cache.txt'));shutil.copyfile(folder/'Testing/Temporary/LastTest.log',v/(name+'-native-tests.log'))
shutil.copytree(root/'out/vulkan-pool-churn-pilot',v/'pilot')
receipt=native.SOURCE/'candidate-receipt.json';shutil.copyfile(receipt,v/'candidate-source-receipt.json');candidate=load(receipt)
for r in candidate['files']:assert rec(root/r['path'])==r
previous=root/'benchmarks/reports/2026-10-24-vulkan-loader-fault/evidence.json';prior=load(previous)
old_records=prior['files']+prior['preserved_inputs']+prior['historical_packages']+prior['local_artifacts']+[prior[k] for k in ('summary','source_archive','previous_evidence','memory_policy')]+[rec(previous)]
preserved={}
for r in old_records:
    checked=history.resolve(r,root);preserved[checked['path']]=checked
packages=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip'))];assert len(packages)==23
assert audit.sdk_fingerprint(root)==native.SDK and audit.sdk_fingerprint(native.SOURCE)==native.CANDIDATE
policy=rec(root/'tools/memory_stress_policy.json');assert policy['sha256']=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
headers=[p.relative_to(root/'include') for p in (root/'include').rglob('*.hpp')];assert all((root/'include'/p).read_bytes()==(native.SOURCE/'include'/p).read_bytes() for p in headers)
for name in ('BufferPool.hpp','BufferPoolRange.hpp'):assert (native.SOURCE/'src/backends/vulkan'/name).read_bytes()==(root/'out/vulkan-buffer-pool-fault-source/src/backends/vulkan'/name).read_bytes()
summary.update(inventory=rec(generated/'inventory.json'),compiler_inputs=len(inventory['files']),components=len(inventory['components']),compiler_read_logs=34,linker_read_logs=33,candidate_selected_CTests=55,product_CTests_rerun=False,CPU_model_assertions_per_variant=172,Python_tests=581,report_mutations_rejected=12,audit_controls_rejected=2,historical_packages_preserved=23,public_headers_unchanged=True)
write(base/'summary.json',summary)
with (base/'observations.csv').open('w',encoding='utf-8',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(summary['metrics'][0]),lineterminator='\n');writer.writeheader();writer.writerows(summary['metrics'])
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl','.spvasm','.patch'}
folders=('include','src','cmake','tests','examples','tools','shaders','benchmarks/vulkan_allocation','benchmarks/vulkan_allocation_trace','experiments/vulkan_buffer_pool','experiments/vulkan_adaptive_buffer_pool','experiments/vulkan_buffer_pool_fault','experiments/vulkan_pool_churn')
paths={p for folder in folders for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp','benchmarks/competitors/sources.lock.json')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((v/'verification-scripts').glob('*.py'))|set(generated.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'}
paths|={root/r['path'] for r in candidate['files']}|{receipt}
sources=[rec(p) for p in sorted(paths,key=lambda p:p.as_posix())];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/vulkan-pool-churn-{native.CANDIDATE}-{digest[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=native.SDK,candidate_SDK_source_fingerprint=native.CANDIDATE,native_source_fingerprint=native.fingerprint(),source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for folder in (v,results,generated) for p in sorted(folder.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.etl') for r in files+sources)
local=inventory['artifacts']+load(results/'results.json')['artifacts']+load(results/'runtime-receipt.json')['artifacts']+candidate['files']+[rec(receipt)]
write(base/'evidence.json',dict(schema_version=1,base_commit=subprocess.check_output(['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','rev-parse','HEAD'],cwd=root,text=True).strip(),SDK_source_fingerprint=native.SDK,candidate_SDK_source_fingerprint=native.CANDIDATE,native_source_fingerprint=native.fingerprint(),production_adopted=False,summary=rec(base/'summary.json'),observations=rec(base/'observations.csv'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=list(preserved.values()),historical_packages=packages,previous_evidence=rec(previous),local_artifacts=local,memory_policy=policy))
print(f'PASS sealed {len(files)} evidence / {len(sources)} own source files / {len(preserved)} preserved records; isolated candidate and history preserved')
