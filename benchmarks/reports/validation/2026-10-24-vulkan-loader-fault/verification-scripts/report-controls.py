import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_loader_fault as report
import audit_vulkan_loader_fault as auditor
import audit_licenses as audit
v=pathlib.Path(__file__).resolve().parent.parent;source=root/'benchmarks/results/2026-10-24-vulkan-loader-fault-v2'
def read(folder,name):return json.loads((folder/name).read_text(encoding='utf-8'))
def write(folder,name,x):(folder/name).write_text(json.dumps(x,indent=2)+'\n')
def mutate_results(folder,fn):
    r=read(folder,'results.json');fn(r);write(folder,'results.json',r)
def mutate_commands(folder,fn):
    r=read(folder,'commands.json');fn(r);write(folder,'commands.json',r)
def raw_change(folder):
    name=read(folder,'results.json')['runs'][0]['name']+'.stdout';r=read(folder,name);r['verified_bytes']+=1;write(folder,name,r)
def stderr_change(folder):
    name=read(folder,'results.json')['runs'][0]['name']+'.stderr';(folder/name).write_bytes(b'unexpected validation diagnostic\n')
controls={
    'missing-process':lambda f:mutate_results(f,lambda r:r['runs'].pop()),
    'duplicate-process':lambda f:mutate_results(f,lambda r:r['runs'].__setitem__(1,copy.deepcopy(r['runs'][0]))),
    'pilot-not-final':lambda f:mutate_results(f,lambda r:r.update(pilot=True)),
    'wrong-source':lambda f:mutate_results(f,lambda r:r.update(source_fingerprint='0'*64)),
    'false-capability':lambda f:mutate_results(f,lambda r:r['probe']['presentation'].__setitem__(1,True)),
    'wrong-artifact':lambda f:mutate_results(f,lambda r:r['artifacts'][0].update(sha256='0'*64)),
    'unconsumed-error':lambda f:mutate_results(f,lambda r:r['runs'][0]['result']['cycles'][0]['failed'].update(pending=1)),
    'boolean-native-count':lambda f:mutate_results(f,lambda r:r['runs'][0]['result']['final']['live'].__setitem__(0,False)),
    'masked-command-exit':lambda f:mutate_commands(f,lambda r:r[2].update(returncode=1)),
    'raw-output-drift':raw_change,'stderr-diagnostic':stderr_change,
    'physical-memory-claim':lambda f:mutate_results(f,lambda r:r.update(physical_memory_reclamation_verified=True)),
}
base=root/'out/vulkan-loader-report-controls-v3';assert not base.exists();base.mkdir()
baseline=report.review(source)
for name,fn in controls.items():
    folder=base/name;shutil.copytree(source,folder)
    # A relocated copy must pass before its single mutation. Only the two
    # absolute negative-control fixture paths need relocation in the ledger.
    commands=read(folder,'commands.json')
    for c in commands:
        if c['name']=='missing-manifest':c['overrides']['VK_LAYER_PATH']=str(folder/'missing')
        if c['name']=='wrong-layer-dll':c['overrides']['GFX_EXPECT_LOADER_LAYER']=str(folder/'missing/absent.dll')
    write(folder,'commands.json',commands)
    assert report.review(folder)==baseline,'Relocated unmodified baseline must pass'
    fn(folder)
    try:report.review(folder)
    except (ValueError,KeyError,TypeError,OSError):pass
    else:raise AssertionError('Accepted '+name)
i,n=auditor.collect()
for name in ('changed-inventory','missing-notices'):
    folder=base/name;shutil.copytree(root/auditor.GENERATED,folder)
    if name=='changed-inventory':(folder/'inventory.json').write_text('{}\n')
    else:(folder/'THIRD_PARTY_NOTICES.txt').unlink()
    try:audit.write_or_check(folder,audit.output_files(i,n),True)
    except (audit.AuditError,OSError):pass
    else:raise AssertionError('Audit accepted '+name)
message=f'PASS {len(controls)} altered loader matrices/raw reports rejected; 2 audit corruption/missing-notice controls rejected'
(v/'report-controls-v3.log').write_text(message+'; every relocated baseline accepted before mutation\n');print(message)
