import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_buffer_pool as report
source=root/'benchmarks/results/2026-10-24-vulkan-buffer-pool';out=root/'out/vulkan-buffer-pool-report-controls';shutil.copytree(source,out)
original={p.name:p.read_bytes() for p in source.iterdir() if p.is_file()};x=json.loads(original['results.json'])
def mutate(fn):
    y=copy.deepcopy(x);fn(y);(out/'results.json').write_text(json.dumps(y),encoding='utf-8')
def reject(name,fn):
    for n,b in original.items():(out/n).write_bytes(b)
    fn()
    try:report.review(out)
    except (ValueError,KeyError,AssertionError):print('PASS rejected',name)
    else:raise AssertionError('Accepted '+name)
report.review(out)
reject('unreviewed production adoption',lambda:mutate(lambda y:y.update(production_adopted=True)))
reject('physical memory reclamation',lambda:mutate(lambda y:y.update(physical_memory_reclamation_verified=True)))
reject('wrong candidate SDK',lambda:mutate(lambda y:y.update(candidate_SDK_source_fingerprint='f'*64)))
reject('changed P50 statistics',lambda:mutate(lambda y:y['groups'][0]['statistics']['candidate']['create_ms'].update(p50_ms=999)))
reject('missing process',lambda:mutate(lambda y:y['runs'].pop()))
reject('changed native blocks',lambda:mutate(lambda y:y['traces'][3]['result']['native_trace']['frames'][0]['counts']['successes'].update(AllocateMemory=1)))
def command():
    y=json.loads(original['commands.json']);y[2]['argv'][-1]='other GPU';(out/'commands.json').write_text(json.dumps(y),encoding='utf-8')
reject('command attribution',command)
reject('removed outliers',lambda:mutate(lambda y:y.update(samples_removed=1)))
print('PASS: eight altered candidate reports rejected; no GPU work executed')
