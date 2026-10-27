"""Restore exact Windows source bytes and replay BEN-03 evidence without a GPU."""
import pathlib,json,hashlib,zipfile,tempfile,subprocess,sys
R=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'docs/ROADMAP_TASKS.json').is_file())
F=R/'benchmarks/reports/validation/2026-10-27-phase3-current-upload';S=R/'benchmarks/reports/2026-10-27-phase3-current-upload'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
r=read(F/'accepted/receipt.json');s=read(S/'summary.json');assert r['status']=='completed' and r['source_commit']==s['source_commit']=='646c2cfa5980b881b455970e0dd9990b95bfbc35'
with tempfile.TemporaryDirectory() as td:
 root=pathlib.Path(td)
 for kind in ('windows','linux-git'):
  m=read(F/f'source-manifest-{kind}.json');a=m['archive'];assert sha((R/a['path']).read_bytes())==a['sha256'] and (R/a['path']).stat().st_size==a['bytes']
  assert len(m['files'])==77 and m['SDK_files']==28 and m['SDK_product_unchanged'] and not m['compiled_or_external_binaries']
  with zipfile.ZipFile(R/a['path']) as z:
   names={x['path'] for x in m['files']};assert set(z.namelist())==names|{'source-receipt.json'}
   assert all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts for n in names)
   for x in m['files']:assert len(z.read(x['path']))==x['bytes'] and sha(z.read(x['path']))==x['sha256']
   sdk={n for n in names if n=='CMakeLists.txt' or n.startswith(('src/','include/'))};assert len(sdk)==28
   assert sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sorted(sdk)).encode())==m['SDK_source_fingerprint']
   previous=read(R/f'benchmarks/reports/validation/2026-10-27-phase3-comparison-protocol/source-manifest-{kind}.json')
   with zipfile.ZipFile(R/previous['archive']['path']) as old:assert all(z.read(n)==old.read(n) for n in sdk)
   if kind=='windows':
    z.extractall(root);prefix=r['validation_library'].split('/out/')[0]+'/';matched=0
    for x in r['frozen_inputs']:
     n=x['path'][len(prefix):]
     if n in names:assert sha(z.read(n))==x['sha256'] and len(z.read(n))==x['bytes'];matched+=1
    assert matched>=58
 replay="""
import sys,pathlib,json,copy
from unittest import mock
sys.dont_write_bytecode=True;sys.path.insert(0,sys.argv[1])
import run_current_vulkan_upload as v
folder=pathlib.Path(sys.argv[2]);original=v.read(folder/'receipt.json');result=v.check(folder);assert result['measurement_samples']==1200
checks=[]
def reject(name,change):
 r=copy.deepcopy(original);change(r);reader=v.read
 def replacement(p):return r if pathlib.Path(p)==folder/'receipt.json' else reader(p)
 with mock.patch.object(v,'read',side_effect=replacement):
  try:v.check(folder)
  except ValueError:checks.append(name)
  else:raise AssertionError('Altered evidence accepted: '+name)
reject('statistics changed',lambda r:r['groups'][0]['statistics'].update(p50_ms=999))
reject('process missing',lambda r:r['runs'].pop())
reject('measurement order changed',lambda r:r['runs'].__setitem__(slice(12,14),r['runs'][12:14][::-1]))
reject('extra process',lambda r:r['runs'].append(copy.deepcopy(r['runs'][-1])))
reject('validation injected into timing',lambda r:r['runs'][12]['overrides'].update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'))
reject('readback total changed',lambda r:r.update(verified_bytes=0))
reject('parsed sample changed',lambda r:r['runs'][12]['result'].update(samples_ms=[999]))
reject('overlapping processes',lambda r:r['runs'][1].update(started=r['runs'][0]['started']))
reject('fallback control succeeded',lambda r:r['runs'][8].update(returncode=0))
reject('LUID changed',lambda r:r['identity'].update(device_luid='0100000000000000'))
reject('historical timing promoted',lambda r:r.update(historical_timings_reused=True))
reject('other workload promoted',lambda r:r.update(completed_workload_tasks=['P3-BEN-03','P3-BEN-04']))
print(json.dumps(dict(offline_replay=result,tamper_rejections=checks,source_capsule='Windows execution bytes',native_GPU_executed=False)))
"""
 p=subprocess.run([sys.executable,'-B','-c',replay,str(root/'tools'),str(F/'accepted')],capture_output=True,text=True,timeout=60)
 assert p.returncode==0,(p.stdout,p.stderr);replayed=json.loads(p.stdout);assert len(replayed['tamper_rejections'])==12
 if '--json' in sys.argv:print(json.dumps(replayed,indent=2))
failed=read(F/'initial-binding-rejection/receipt.json');actual=read(F/'initial-binding-rejection/commands/000-validation-graphicssdk-odd-0.stdout');ctx=read(F/'initial-binding-rejection/context.json')
assert failed['status']=='failed' and failed['error']=='Device/driver/environment changed' and failed['measurement_samples']==0
assert actual['device_uuid']==failed['identity']['device_uuid']==r['identity']['device_uuid'] and actual['driver_uuid']==failed['identity']['driver_uuid']==r['identity']['driver_uuid']
assert actual['device_luid']==r['identity']['device_luid']!=failed['identity']['device_luid'] and ctx['historical_measurement_samples_reused']==0
assert sha((R/ctx['previous_qualification_archive']/'receipt.json').read_bytes())==ctx['previous_qualification_receipt_sha256']
prep=read(F/'accepted/qualification/prepared/receipt.json');assert prep['source_commit']==r['source_commit'] and prep['identity']==r['identity'] and len(prep['probes'])==8
py=read(F/'python-local/receipt.json');assert py['source_commit']==r['source_commit'] and py['successful'] and py['inputs_unchanged'] and py['tests']==803 and len(set(py['test_ids']))==803 and not py['skipped'] and not py['failures'] and not py['errors'] and sha((F/'python-local/unittest.log').read_bytes())==py['log']['sha256']
assert s['statistics']==r['groups'] and s['verified_images']==r['verified_images']==1432 and s['verified_bytes']==r['verified_bytes']==38605581560
assert s['completed_workload_tasks']==['P3-BEN-03'] and not any(s[k] for k in ('historical_timings_reused','overall_SDK_ranking','GPU_timestamp_measurement','SDL_native_handle_verified','Intel_GPU_device_created','Linux_benchmark_executed','human_display_accepted','runtime_redistributed'))
for root in (F,S):
 for p in root.rglob('*'):assert p.name!='__pycache__' and not p.name.casefold().endswith(('.exe','.dll','.obj','.lib','.a','.so','.pdb','.pyc'))
if (F/'ci-runs.json').is_file():
 ci=read(F/'ci-runs.json');assert len(ci['workflow_runs'])==2 and all(x['head_sha']==r['source_commit'] and x['conclusion']=='success' and x['status']=='completed' for x in ci['workflow_runs'])
if (F/'inventory.json').is_file():
 inv=read(F/'inventory.json');assert sha((R/inv['summary']['path']).read_bytes())==inv['summary']['sha256']
 assert {p.relative_to(F).as_posix() for p in F.rglob('*') if p.is_file() and p.name not in ('inventory.json','offline-verification.log')}=={x['path'] for x in inv['files']}
 for x in inv['files']:assert sha((F/x['path']).read_bytes())==x['sha256'] and (F/x['path']).stat().st_size==x['bytes']
if (R/'out/ben03-user-files.json').is_file():
 for n,h in read(R/'out/ben03-user-files.json').items():assert sha((R/n).read_bytes()).upper()==h
print('PASS fresh current-LUID preparation/protocol; 52 upload processes/1200 raw samples/1432 full image readbacks; statistics and 12 tamper rejections; original binding rejection retained; Python803; SDK28 unchanged. No GPU execution by this verifier.')
