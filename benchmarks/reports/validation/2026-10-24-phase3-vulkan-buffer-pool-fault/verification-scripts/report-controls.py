import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_buffer_pool_fault as report
source=root/'benchmarks/results/2026-10-24-vulkan-buffer-pool-fault-v2';out=root/'out/vulkan-buffer-pool-fault-report-controls';shutil.copytree(source,out)
original={p.name:p.read_bytes() for p in source.iterdir() if p.is_file()};x=json.loads(original['results.json'])
def mutate(fn,raw=False):
    y=copy.deepcopy(x);fn(y);(out/'results.json').write_text(json.dumps(y),encoding='utf-8')
    if raw:
        for row in y['runs']:(out/(row['name']+'.stdout')).write_text(json.dumps(row['result']),encoding='utf-8')
def reject(name,fn):
    for n,b in original.items():(out/n).write_bytes(b)
    fn()
    try:report.review(out)
    except (ValueError,KeyError,AssertionError):print('PASS rejected',name)
    else:raise AssertionError('Accepted '+name)
report.review(out)
reject('unreviewed product adoption',lambda:mutate(lambda y:y.update(production_adopted=True)))
reject('unmeasured performance claim',lambda:mutate(lambda y:y.update(performance_remeasured=True)))
reject('physical memory claim',lambda:mutate(lambda y:y.update(physical_memory_reclamation_verified=True)))
reject('missing error/recovery cell',lambda:mutate(lambda y:y['runs'].pop()))
reject('wrong candidate SDK',lambda:mutate(lambda y:y.update(SDK_source_fingerprint='f'*64)))
reject('hidden pending plan',lambda:mutate(lambda y:y['runs'][0]['result']['native_trace']['fault'].update(pending=1),True))
reject('unverified payload bytes',lambda:mutate(lambda y:y['runs'][0]['result'].update(verified_bytes=1),True))
reject('masked prior invalid frees',lambda:mutate(lambda y:next(r for r in y['runs'] if r['variant']=='prior')['result']['native_trace']['fault'].update(invalid_frees=0),True))
reject('shared slot leaked on failed bind',lambda:mutate(lambda y:next(r for r in y['runs'] if r['result']['case']=='bind-reuse')['result']['native_trace']['frames'][2]['counts']['calls'].update(AllocateMemory=9),True))
def command():
    y=json.loads(original['commands.json']);y[1]['argv'][2]='other GPU';(out/'commands.json').write_text(json.dumps(y))
reject('command attribution',command)
print('PASS ten altered fault reports rejected; no GPU work executed')
