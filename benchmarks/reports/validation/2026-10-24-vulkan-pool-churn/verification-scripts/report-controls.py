import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_pool_churn as report
import audit_vulkan_pool_churn as auditor
import audit_licenses as audit
v=pathlib.Path(__file__).resolve().parent.parent;source=root/'benchmarks/results/2026-10-24-vulkan-pool-churn'
def read(f,n):return json.loads((f/n).read_text(encoding='utf-8'))
def write(f,n,x):(f/n).write_text(json.dumps(x,indent=2)+'\n')
def modify(f,fn):
    x=read(f,'results.json');fn(x);write(f,'results.json',x)
def raw(f,fn):
    x=read(f,'results.json');row=x['runs'][0];r=read(f,row['name']+'.stdout');fn(r);write(f,row['name']+'.stdout',r);row['result']=r;write(f,'results.json',x)
controls={
    'missing-run':lambda f:modify(f,lambda x:x['runs'].pop()),
    'duplicate-run':lambda f:modify(f,lambda x:x['runs'].__setitem__(1,copy.deepcopy(x['runs'][0]))),
    'pilot-not-full':lambda f:modify(f,lambda x:x.update(pilot=True)),
    'wrong-candidate':lambda f:modify(f,lambda x:x.update(candidate_SDK_source_fingerprint='0'*64)),
    'wrong-artifact':lambda f:modify(f,lambda x:x['artifacts'][0].update(sha256='0'*64)),
    'promotion-claim':lambda f:modify(f,lambda x:x.update(production_adopted=True)),
    'timing-claim':lambda f:modify(f,lambda x:x.update(performance_remeasured=True)),
    'boolean-counter':lambda f:raw(f,lambda r:r['native_trace'].update(errors=False)),
    'readback-count':lambda f:raw(f,lambda r:r.update(verified_buffers=r['verified_buffers']-1)),
    'native-leak':lambda f:raw(f,lambda r:r['native_trace']['live'].update(bytes=64)),
    'unsafe-binding':lambda f:raw(f,lambda r:r['native_trace']['fault'].update(guard_errors=1)),
    'missing-frame':lambda f:raw(f,lambda r:r['native_trace']['frames'].pop()),
}
base=root/'out/vulkan-pool-churn-report-controls';assert not base.exists();base.mkdir();baseline=report.review(source)
for name,fn in controls.items():
    f=base/name;shutil.copytree(source,f);commands=read(f,'commands.json')
    for c in commands:
        if c['name']=='missing-layer':c['overrides']['VK_LAYER_PATH']=str(f/'missing')
        if c['name']=='wrong-layer':c['overrides']['GFX_EXPECT_FAULT_LAYER']=str(f/'missing/absent.dll')
    write(f,'commands.json',commands);assert report.review(f)==baseline,'Unmodified relocated baseline must pass';fn(f)
    try:report.review(f)
    except (ValueError,KeyError,TypeError,OSError):pass
    else:raise AssertionError('Accepted '+name)
i,n=auditor.collect()
for name in ('changed-inventory','missing-notices'):
    f=base/name;shutil.copytree(root/auditor.GENERATED,f)
    if name=='changed-inventory':(f/'inventory.json').write_text('{}\n')
    else:(f/'THIRD_PARTY_NOTICES.txt').unlink()
    try:audit.write_or_check(f,audit.output_files(i,n),True)
    except (audit.AuditError,OSError):pass
    else:raise AssertionError('Audit accepted '+name)
message='PASS 12 altered churn matrices/raw reports rejected; all unmodified relocated baselines passed; 2 audit corruption/missing-notice controls rejected'
(v/'report-controls.log').write_text(message+'\n');print(message)
