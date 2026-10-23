import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_draw as report
source=root/'benchmarks/results/2026-10-23-vulkan-draw';out=root/'out/vulkan-draw-benchmark-report-controls';shutil.copytree(source,out)
original={p.name:p.read_bytes() for p in source.iterdir() if p.is_file()}
x=json.loads(original['results.json'])
def mutate(fn):
    y=copy.deepcopy(x);fn(y);(out/'results.json').write_text(json.dumps(y))
def reject(name,fn):
    for n,b in original.items():(out/n).write_bytes(b)
    fn()
    try:report.review(out)
    except (ValueError,KeyError,AssertionError):print('PASS rejected',name)
    else:raise AssertionError('Accepted '+name)
report.review(out)
reject('GPU timestamp attribution',lambda:mutate(lambda y:y.update(GPU_timestamp_measurement=True)))
reject('native draw-count attribution',lambda:mutate(lambda y:y.update(native_draw_count_verified=True)))
reject('summary statistics',lambda:mutate(lambda y:y['groups'][0]['statistics']['sdl'].update(p50_ms=999)))
reject('missing comparison cell',lambda:mutate(lambda y:y['groups'].pop()))
reject('raw result mismatch',lambda:mutate(lambda y:y['runs'][0]['result'].update(submitted_draw_calls=999)))
def command():
    y=json.loads(original['commands.json']);y[2]['argv'][-1]='other GPU';(out/'commands.json').write_text(json.dumps(y))
reject('command attribution',command)
reject('validation stdout',lambda:(out/(x['runs'][0]['name']+'.stdout')).write_bytes(b'Validation Error: VUID-shader ERROR\n'+original[x['runs'][0]['name']+'.stdout']))
print('PASS: seven altered report controls rejected; no GPU work executed')
