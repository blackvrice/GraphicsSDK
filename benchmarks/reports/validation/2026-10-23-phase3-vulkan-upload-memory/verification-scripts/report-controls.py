"""Reject altered raw/result attribution and timing scopes using a disposable result copy."""
import copy
import json
import pathlib
import shutil
import sys
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import report_vulkan_upload_compare as r
source=root/'benchmarks/results/2026-10-23-vulkan-upload-memory'
target=root/'out/vulkan-upload-memory-report-controls';shutil.copytree(source,target)
x=json.loads((target/'results.json').read_bytes());commands=(target/'commands.json').read_bytes()
def changed_result(v):
    (target/'results.json').write_text(json.dumps(v))
def reject(label,mutate):
    changed_result(x);(target/'commands.json').write_bytes(commands);mutate()
    try:r.review(target)
    except (ValueError,KeyError,AssertionError):print('PASS rejected',label)
    else:raise AssertionError('Accepted '+label)
r.review(target)
def mutation(f):
    y=copy.deepcopy(x);f(y);changed_result(y)
reject('diagnostic timing scope',lambda:mutation(lambda y:y.update(diagnostic_timings_excluded=False)))
reject('changed summary',lambda:mutation(lambda y:y['groups'][0]['statistics']['after'].update(p50_ms=999)))
reject('missing comparison cell',lambda:mutation(lambda y:y['groups'].pop()))
reject('raw result divergence',lambda:mutation(lambda y:y['runs'][0]['result'].update(verified_images=999)))
def command_change():
    y=json.loads(commands);y[2]['argv'][-1]='different device';(target/'commands.json').write_text(json.dumps(y))
reject('command attribution',command_change)
def raw_change():
    name=x['profiles'][0]['name'];p=target/(name+'.stdout');y=json.loads(p.read_bytes());y['profile_source_fingerprint']='changed';p.write_text(json.dumps(y))
reject('diagnostic executable source',raw_change)
print('PASS: six altered report controls rejected; no GPU processes executed')
