import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_pso as report
source=root/'benchmarks/results/2026-10-24-vulkan-pso';out=root/'out/vulkan-pso-benchmark-report-controls';shutil.copytree(source,out)
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
reject('GPU time attribution',lambda:mutate(lambda y:y.update(GPU_timestamp_measurement=True)))
reject('actual memory reclamation',lambda:mutate(lambda y:y.update(actual_memory_reclamation_verified=True)))
reject('known cache state',lambda:mutate(lambda y:y.update(driver_cache_state_known=True)))
reject('summary statistics',lambda:mutate(lambda y:y['groups'][0]['statistics']['sdl']['create_ms'].update(p50_ms=999)))
reject('missing comparison cell',lambda:mutate(lambda y:y['groups'].pop()))
reject('raw release count',lambda:mutate(lambda y:y['runs'][0]['result'].update(released_pipeline_handles=999)))
def command():
    y=json.loads(original['commands.json']);y[2]['argv'][-1]='other GPU';(out/'commands.json').write_text(json.dumps(y))
reject('command attribution',command)
reject('validation stdout',lambda:(out/(x['runs'][0]['name']+'.stdout')).write_bytes(b'Validation Error: VUID-shader ERROR\n'+original[x['runs'][0]['name']+'.stdout']))
print('PASS: eight altered report controls rejected; no GPU work executed')
