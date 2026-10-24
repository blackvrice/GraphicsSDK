import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_allocation_trace as report
source=root/'benchmarks/results/2026-10-24-vulkan-allocation-trace';out=root/'out/vulkan-allocation-trace-benchmark-report-controls-final';shutil.copytree(source,out)
original={p.name:p.read_bytes() for p in source.iterdir() if p.is_file()};x=json.loads(original['results.json'])
def mutate(fn):
    y=copy.deepcopy(x);fn(y);(out/'results.json').write_text(json.dumps(y))
def reject(name,fn):
    for n,b in original.items():(out/n).write_bytes(b)
    fn()
    try:report.review(out)
    except (ValueError,KeyError,AssertionError):print('PASS rejected',name)
    else:raise AssertionError('Accepted '+name)
report.review(out)
reject('performance attribution',lambda:mutate(lambda y:y.update(timings_excluded=False)))
reject('physical memory reclamation',lambda:mutate(lambda y:y.update(physical_memory_reclamation_verified=True)))
reject('driver internal allocation claim',lambda:mutate(lambda y:y.update(native_driver_internal_allocations_verified=True)))
reject('missing matrix cell',lambda:mutate(lambda y:y['runs'].pop()))
reject('changed raw native counters',lambda:mutate(lambda y:y['runs'][0]['result']['native_trace']['frames'][0]['counts']['calls'].update(AllocateMemory=999)))
reject('dropped trace events',lambda:mutate(lambda y:y['runs'][0]['result']['native_trace'].update(dropped_events=1)))

def command():
    y=json.loads(original['commands.json']);y[2]['argv'][-1]='other GPU';(out/'commands.json').write_text(json.dumps(y))
reject('command attribution',command)
reject('validation stdout',lambda:(out/(x['runs'][0]['name']+'.stdout')).write_bytes(b'Validation Error: VUID-shader ERROR\n'+original[x['runs'][0]['name']+'.stdout']))
print('PASS: eight altered report controls rejected; no GPU work executed')
