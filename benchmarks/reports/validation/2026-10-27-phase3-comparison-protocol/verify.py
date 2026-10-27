"""Offline replay of the BEN-02 source capsule and archived raw evidence."""
import pathlib,json,hashlib,zipfile,tempfile,subprocess,sys
R=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'docs/ROADMAP_TASKS.json').is_file())
F=R/'benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol';S=R/'benchmarks/reports/2026-10-27-phase3-comparison-protocol'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
r=read(F/'accepted/receipt.json');s=read(S/'summary.json')
assert r['status']=='qualified' and r['source_commit']==s['source_commit']=='18c316de929a4d83cf668cfa0552b67252c38901'
with tempfile.TemporaryDirectory() as td:
 snapshot=pathlib.Path(td)
 for kind in ('windows','linux-git'):
  m=read(F/f'source-manifest-{kind}.json');a=m['archive'];assert sha((R/a['path']).read_bytes())==a['sha256'] and (R/a['path']).stat().st_size==a['bytes']
  assert len(m['files'])==74 and m['SDK_files']==28 and m['SDK_product_unchanged'] and not m['compiled_or_external_binaries']
  with zipfile.ZipFile(R/a['path']) as z:
   names={x['path'] for x in m['files']};assert set(z.namelist())==names|{'source-receipt.json'}
   assert all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts for n in names)
   for x in m['files']:assert len(z.read(x['path']))==x['bytes'] and sha(z.read(x['path']))==x['sha256']
   sdk={n for n in names if n=='CMakeLists.txt' or n.startswith(('src/','include/'))};assert len(sdk)==28
   assert sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sorted(sdk)).encode())==m['SDK_source_fingerprint']
   previous=read(R/f'benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs/source-manifest-{kind}.json')
   with zipfile.ZipFile(R/previous['archive']['path']) as old:assert all(z.read(n)==old.read(n) for n in sdk)
   if kind=='windows':
    z.extractall(snapshot)
    prefix=r['validation_library'].split('/out/')[0]+'/'
    matched=0
    for x in r['frozen_inputs']:
     n=x['path'][len(prefix):]
     if n in names:assert sha(z.read(n))==x['sha256'] and len(z.read(n))==x['bytes'];matched+=1
    assert matched>=58
 # Replay original Windows bytes even on another OS; this executes Python only.
 replay="""
import sys,pathlib,json,copy
from unittest import mock
sys.dont_write_bytecode=True
sys.path.insert(0,sys.argv[1])
import run_vulkan_comparison as v
folder=pathlib.Path(sys.argv[2]);original=v.read(folder/'receipt.json');result=v.check(folder)
assert result['measurement_samples']==288
checks=[]
def reject(name,change):
 r=copy.deepcopy(original);change(r);reader=v.read
 def replacement(p):return r if pathlib.Path(p)==folder/'receipt.json' else reader(p)
 with mock.patch.object(v,'read',side_effect=replacement):
  try:v.check(folder)
  except ValueError:checks.append(name)
  else:raise AssertionError('Altered evidence accepted: '+name)
reject('statistics altered',lambda r:r['groups'][0]['statistics']['total_ms'].update(p50_ms=999))
reject('process missing',lambda r:r['runs'].pop())
reject('run order swapped',lambda r:r['runs'].__setitem__(slice(42,44),r['runs'][42:44][::-1]))
reject('extra process',lambda r:r['runs'].append(copy.deepcopy(r['runs'][-1])))
reject('validation injected into timing',lambda r:r['runs'][42]['overrides'].update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'))
reject('parsed readback changed',lambda r:r['runs'][0]['result'].update(verified_bytes=0))
reject('parsed sample changed',lambda r:r['runs'][42]['result'].update(samples_ms=[999]))
reject('overlapping processes',lambda r:r['runs'][1].update(started=r['runs'][0]['started']))
reject('fallback control succeeded',lambda r:r['runs'][26].update(returncode=0))
reject('validation library changed',lambda r:r.update(validation_library_sha256='0'*64))
reject('historical timing promoted',lambda r:r.update(historical_timings_reused=True))
reject('overall ranking promoted',lambda r:r.update(overall_SDK_ranking=True))
print(json.dumps(dict(offline_replay=result,tamper_rejections=checks,source_capsule='Windows execution bytes',native_GPU_executed=False)))
"""
 p=subprocess.run([sys.executable,'-B','-c',replay,str(snapshot/'tools'),str(F/'accepted')],capture_output=True,text=True,timeout=60)
 assert p.returncode==0,(p.stdout,p.stderr);result=json.loads(p.stdout);assert len(result['tamper_rejections'])==12
 if '--json' in sys.argv:print(json.dumps(result,indent=2))
py=read(F/'python-local/receipt.json');assert py['successful'] and py['inputs_unchanged'] and py['source_commit']==r['source_commit'] and py['tests']==785 and len(set(py['test_ids']))==785 and not py['skipped'] and not py['failures'] and not py['errors'] and sha((F/'python-local/unittest.log').read_bytes())==py['log']['sha256']
assert s['serialized_processes']==66 and s['validation_processes']==26 and s['control_processes']==16 and s['measurement_processes']==24 and s['measurement_samples']==288 and s['samples_removed']==0
assert s['statistics']==r['groups'] and s['validation_verified_bytes']==sum(x['result']['verified_bytes'] for x in r['runs'] if x['job']['kind']=='validation')
assert s['measurement_verified_bytes']==sum(x['result']['verified_bytes'] for x in r['runs'] if x['job']['kind']=='measurement')
assert s['full_workload_tasks_completed']==[] and not any(s[k] for k in ('historical_timings_reused','overall_SDK_ranking','SDL_native_handle_verified','Intel_GPU_device_created','Linux_benchmark_executed','human_display_accepted','runtime_redistributed'))
initial=read(F/'python-initial-context.json');assert not initial['source_or_fixture_fix_applied'] and initial['focused_retry_passed'] and initial['recorded_full_discovery_passed']
for root in (F,S):
 for p in root.rglob('*'):assert p.name!='__pycache__' and not p.name.casefold().endswith(('.exe','.dll','.obj','.lib','.a','.so','.pdb','.pyc'))
if (F/'ci-runs.json').is_file():
 ci=read(F/'ci-runs.json');assert len(ci['workflow_runs'])==2 and all(x['head_sha']==r['source_commit'] and x['conclusion']=='success' and x['status']=='completed' for x in ci['workflow_runs'])
if (F/'inventory.json').is_file():
 inv=read(F/'inventory.json');assert sha((R/inv['summary']['path']).read_bytes())==inv['summary']['sha256']
 assert {p.relative_to(F).as_posix() for p in F.rglob('*') if p.is_file() and p.name not in ('inventory.json','offline-verification.log')}=={x['path'] for x in inv['files']}
 for x in inv['files']:assert sha((F/x['path']).read_bytes())==x['sha256'] and (F/x['path']).stat().st_size==x['bytes']
if (R/'out/ben02-user-files.json').is_file():
 for n,h in read(R/'out/ben02-user-files.json').items():assert sha((R/n).read_bytes()).upper()==h
print('PASS offline source-capsule replay of66 serialized GPU processes/288 raw qualification samples; all percentiles/MAD/process medians; 12 tamper rejections; Python785; SDK28 unchanged; no workload/ranking promotion. No native GPU execution by this verifier.')
