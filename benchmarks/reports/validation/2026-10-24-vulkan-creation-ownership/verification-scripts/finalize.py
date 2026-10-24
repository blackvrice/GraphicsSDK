import difflib
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import audit_vulkan_creation_ownership as auditor
import report_vulkan_creation_ownership as report_tool
import vulkan_creation_provenance as history
v=pathlib.Path(__file__).resolve().parent.parent;report=root/'benchmarks/reports/2026-10-24-vulkan-creation-ownership';results=root/'benchmarks/results/2026-10-24-vulkan-creation-ownership';generated=root/auditor.GENERATED
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def rec(p):return audit.record_file(root,p)
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8',newline='')
x=report_tool.review(results);inventory,notices=auditor.collect(root);audit.write_or_check(generated,audit.output_files(inventory,notices),True)
assert len(inventory['files'])==909 and len(inventory['components'])==21
for r in inventory['compiler_read_evidence']+inventory['linker_read_evidence']+inventory['projects']:
    source=root/r['path'];target=v/'build-traces'/source.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
folders=('vulkan-creation-product','vulkan-creation-cpu-before-v2','vulkan-creation-cpu-after-v2','vulkan-creation-native-before','vulkan-creation-native-after')
for folder in folders:shutil.copyfile(root/'out'/folder/'CMakeCache.txt',v/(folder+'-cache.txt'))
shutil.copyfile(root/'out/vulkan-creation-product/Testing/Temporary/LastTest.log',v/'product-native-tests.log')
receipt=root/history.SNAPSHOT/'receipt.json';shutil.copyfile(receipt,v/'pre-fix-source-receipt.json')
for r in load(receipt)['files']:assert rec(root/r['path'])==r
for name in ('changed-inventory','missing-notices'):
    folder=root/'out'/('vulkan-creation-audit-'+name);folder.mkdir()
    for p in generated.iterdir():shutil.copyfile(p,folder/p.name)
    if name=='missing-notices':(folder/'THIRD_PARTY_NOTICES.txt').unlink()
    else:(folder/'inventory.json').write_text('{}\n')
    try:audit.write_or_check(folder,audit.output_files(inventory,notices),True)
    except (audit.AuditError,OSError):pass
    else:raise AssertionError('Audit accepted '+name)
(v/'audit-controls.log').write_text('PASS changed inventory and missing notices rejected; before/after product/consumer provenance and historical snapshot verified\n')
previous=root/'benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool-fault/evidence.json';prior=load(previous)
old_records=prior['files']+prior['preserved_inputs']+prior['local_artifacts']+prior['unchanged_production_sources']+[prior[k] for k in ('summary','cases','source_archive','previous_evidence')]+[rec(previous)]
preserved={};relocated={}
for r in old_records:
    checked=history.resolve(r,root);preserved[checked['path']]=checked
    if checked['path']!=r['path']:relocated[r['path']]=dict(original={k:r[k] for k in ('path','sha256','bytes')},snapshot=checked)
assert set(relocated)==history.CHANGED
before=root/history.SNAPSHOT;old=load(receipt);changed=[]
for r in old['files']:
    name=(root/r['path']).relative_to(before).as_posix();current=root/name
    if current.read_bytes()!=(root/r['path']).read_bytes():
        assert name in history.CHANGED;changed.append(dict(before=r,after=rec(current)))
assert {r['after']['path'] for r in changed}==history.CHANGED
public=[p.relative_to(root/'include') for p in (root/'include').rglob('*.hpp')];assert set(public)=={p.relative_to(before/'include') for p in (before/'include').rglob('*.hpp')}
assert all((root/'include'/p).read_bytes()==(before/'include'/p).read_bytes() for p in public)
assert audit.sdk_fingerprint(before)==history.OLD and audit.sdk_fingerprint(root)==x['SDK_source_fingerprint']
assert audit.sha256((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
packages=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip'))];assert len(packages)==23
for r in prior['historical_packages']:assert rec(root/r['path'])==r
patch=''
for name in sorted(history.CHANGED|{'src/backends/vulkan/Creation.hpp','tests/VulkanCreationTests.cpp'}):
    p=before/name;original=p.read_text(encoding='utf-8').splitlines(True) if p.exists() else []
    patch+=''.join(difflib.unified_diff(original,(root/name).read_text(encoding='utf-8').splitlines(True),fromfile='a/'+name,tofile='b/'+name))
(v/'product.patch').write_bytes(patch.encode())
summary=load(report/'summary.json');summary.update(inventory=rec(generated/'inventory.json'),compiler_inputs=909,components=21,new_compiler_logs=36,new_linker_logs=35,historical_packages_preserved=23,historical_product_paths_resolved=3,public_headers_unchanged=True)
write(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl','.spvasm','.patch'}
folders=('include','src','cmake','tests','examples','tools','shaders','benchmarks/vulkan_allocation','benchmarks/vulkan_allocation_trace','experiments/vulkan_buffer_pool','experiments/vulkan_adaptive_buffer_pool','experiments/vulkan_buffer_pool_fault')
paths={p for folder in folders for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp','benchmarks/competitors/sources.lock.json')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((v/'verification-scripts').glob('*.py'))|set(generated.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt',v/'product.patch'}|{root/r['path'] for r in old['files']}|{receipt}
sources=[rec(p) for p in sorted(paths)];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f"benchmarks/reports/sources/vulkan-creation-ownership-{x['SDK_source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=x['SDK_source_fingerprint'],prior_SDK_source_fingerprint=history.OLD,source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for folder in (v,results,generated) for p in sorted(folder.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.etl') for r in files+sources)
local=inventory['artifacts']+x['artifacts']+load(results/'runtime-receipt.json')['artifacts']+old['files']+[rec(receipt)]
write(report/'evidence.json',dict(schema_version=1,base_commit=subprocess.check_output(['git','-c','gc.auto=0','-c','maintenance.auto=false','rev-parse','HEAD'],cwd=root,text=True).strip(),SDK_source_fingerprint=x['SDK_source_fingerprint'],prior_SDK_source_fingerprint=history.OLD,native_source_fingerprint=x['native_source_fingerprint'],summary=rec(report/'summary.json'),cases=rec(report/'cpu-cases.csv'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=list(preserved.values()),historical_relocations=list(relocated.values()),changed_product_sources=changed,new_product_source=rec(root/'src/backends/vulkan/Creation.hpp'),historical_packages=packages,previous_evidence=rec(previous),local_artifacts=local))
print(f'PASS sealed {len(files)} evidence / {len(sources)} source files; product fix, original source snapshot and prior evidence/packages preserved')
