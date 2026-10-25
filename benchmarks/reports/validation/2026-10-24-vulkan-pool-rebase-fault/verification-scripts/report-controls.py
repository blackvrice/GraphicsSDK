import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_pool_rebase_fault as report
import audit_vulkan_pool_rebase_fault as auditor
import audit_licenses as audit
v=pathlib.Path(__file__).resolve().parent.parent;source=root/'benchmarks/results/2026-10-24-vulkan-pool-rebase-fault'
def read(f,n):return json.loads((f/n).read_text(encoding='utf-8'))
def write(f,n,x):(f/n).write_text(json.dumps(x,indent=2)+'\n')
def matrix(f,fn):
    x=read(f,'results.json');fn(x);write(f,'results.json',x)
def raw(f,fn,case='mixed-host'):
    x=read(f,'results.json');row=next(r for r in x['runs'] if r['kind']=='mixed' and r['result']['case']==case);r=read(f,row['name']+'.stdout');fn(r);write(f,row['name']+'.stdout',r);row['result']=r;write(f,'results.json',x)
controls={
 'missing-process':lambda f:matrix(f,lambda x:x['runs'].pop()),
 'duplicate-process':lambda f:matrix(f,lambda x:x['runs'].__setitem__(1,copy.deepcopy(x['runs'][0]))),
 'pilot-not-final':lambda f:matrix(f,lambda x:x.update(pilot=True)),
 'wrong-source':lambda f:matrix(f,lambda x:x.update(source_fingerprint='0'*64)),
 'wrong-artifact':lambda f:matrix(f,lambda x:x['artifacts'][0].update(sha256='0'*64)),
 'promotion-claim':lambda f:matrix(f,lambda x:x.update(production_adopted=True)),
 'boolean-counter':lambda f:raw(f,lambda r:r['native_trace'].update(errors=False)),
 'readback-count':lambda f:raw(f,lambda r:r.update(verified_buffers=1)),
 'consumed-on-reuse':lambda f:raw(f,lambda r:r['cycles'][0].update(pending_after_reuse=0),'reuse-then-host'),
 'wrong-block-request':lambda f:raw(f,lambda r:r['native_trace']['fault']['injections'][0].update(bytes_or_offset=4096)),
 'missing-second-oom':lambda f:raw(f,lambda r:r['native_trace']['fault']['injections'].pop(),'mixed-double'),
 'release-leak':lambda f:raw(f,lambda r:r['cycles'][0]['clean'].update(bytes=4096,memories=1)),
}
base=root/'out/vulkan-pool-rebase-fault-report-controls';assert not base.exists();base.mkdir();baseline=report.review(source)
for name,fn in controls.items():
    f=base/name;shutil.copytree(source,f);commands=read(f,'commands.json')
    for c in commands:
        if c['name'].endswith('-missing-layer'):c['overrides']['VK_LAYER_PATH']=str(f/'missing')
        if c['name'].endswith('-wrong-layer'):c['overrides']['GFX_EXPECT_FAULT_LAYER']=str(f/'missing/absent.dll')
    write(f,'commands.json',commands);assert report.review(f)==baseline,'Relocated valid baseline must pass';fn(f)
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
message='PASS 12 altered rebased-fault matrices/raw reports rejected; every valid relocated baseline accepted; 2 audit corruption/missing-notice controls rejected'
(v/'report-controls.log').write_text(message+'\n');print(message)
