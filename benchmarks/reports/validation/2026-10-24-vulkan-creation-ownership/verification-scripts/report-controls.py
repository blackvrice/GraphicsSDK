import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_creation_ownership as report
source=root/'benchmarks/results/2026-10-24-vulkan-creation-ownership';out=root/'out/vulkan-creation-report-controls';shutil.copytree(source,out)
original={p.name:p.read_bytes() for p in source.iterdir() if p.is_file()};x=json.loads(original['results.json'])
def mutate(fn,raw=False):
    y=copy.deepcopy(x);fn(y);(out/'results.json').write_text(json.dumps(y))
    if raw:
        for row in y['runs']:(out/(row['name']+'.stdout')).write_text(json.dumps(row['result']))
def reject(name,fn):
    for n,b in original.items():(out/n).write_bytes(b)
    fn()
    try:report.review(out)
    except (ValueError,KeyError,AssertionError):print('PASS rejected',name)
    else:raise AssertionError('Accepted '+name)
report.review(out)
reject('unreviewed pool adoption',lambda:mutate(lambda y:y.update(buffer_pool_adopted=True)))
reject('unmeasured timing',lambda:mutate(lambda y:y.update(performance_remeasured=True)))
reject('physical memory claim',lambda:mutate(lambda y:y.update(physical_memory_reclamation_verified=True)))
reject('wrong product fingerprint',lambda:mutate(lambda y:y.update(SDK_source_fingerprint='f'*64)))
reject('missing hardware cell',lambda:mutate(lambda y:y['runs'].pop()))
reject('masked old CPU cleanup',lambda:mutate(lambda y:y['cpu']['before'].update(invalid_cleanup=0)))
reject('new CPU failure',lambda:mutate(lambda y:y['cpu']['after']['cases'][0].update(invalid_cleanup=1)))
reject('missing byte verification',lambda:mutate(lambda y:y['runs'][0]['result'].update(verified_bytes=1),True))
reject('unconsumed injection',lambda:mutate(lambda y:y['runs'][0]['result']['native_trace']['fault'].update(pending=1),True))
reject('masked hardware regression',lambda:mutate(lambda y:next(r for r in y['runs'] if r['variant']=='before')['result']['native_trace']['fault'].update(invalid_frees=0),True))
def command():
    y=json.loads(original['commands.json']);y[1]['argv'][2]='other GPU';(out/'commands.json').write_text(json.dumps(y))
reject('command attribution',command)
print('PASS eleven altered ownership reports rejected; no GPU work executed')
