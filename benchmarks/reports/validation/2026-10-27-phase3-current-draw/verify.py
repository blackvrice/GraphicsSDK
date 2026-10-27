"""Replay the exact draw CPU source capsule; no GPU or native binary execution."""
import pathlib,json,hashlib,zipfile,tempfile,subprocess,sys
R=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'docs/ROADMAP_TASKS.json').is_file())
F=R/'benchmarks/reports/validation/2026-10-27-phase3-current-draw';S=R/'benchmarks/reports/2026-10-27-phase3-current-draw'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
r=read(F/'accepted/receipt.json');s=read(S/'summary.json');assert r['status']=='completed' and r['source_commit']==s['source_commit']=='2323f3ab0e4c6de6ae5c75b17c2dfdc257ac88ac'
with tempfile.TemporaryDirectory() as td:
 root=pathlib.Path(td)
 for kind in ('windows','linux-git'):
  m=read(F/f'source-manifest-{kind}.json');a=m['archive'];assert sha((R/a['path']).read_bytes())==a['sha256'] and (R/a['path']).stat().st_size==a['bytes']
  assert len(m['files'])==83 and m['SDK_files']==28 and m['SDK_product_unchanged'] and not m['compiled_or_external_binaries']
  with zipfile.ZipFile(R/a['path']) as z:
   names={x['path'] for x in m['files']};assert set(z.namelist())==names|{'source-receipt.json'}
   assert all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts for n in names)
   for x in m['files']:assert len(z.read(x['path']))==x['bytes'] and sha(z.read(x['path']))==x['sha256']
   sdk={n for n in names if n=='CMakeLists.txt' or n.startswith(('src/','include/'))};assert len(sdk)==28
   assert sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sorted(sdk)).encode())==m['SDK_source_fingerprint']
   previous=read(R/f'benchmarks/reports/validation/2026-10-27-phase3-current-upload/source-manifest-{kind}.json')
   with zipfile.ZipFile(R/previous['archive']['path']) as old:assert all(z.read(n)==old.read(n) for n in sdk)
   if kind=='windows':
    z.extractall(root);prefix=r['validation_library'].split('/out/')[0]+'/';matched=0
    for x in r['frozen_inputs']:
     n=x['path'][len(prefix):]
     if n in names:assert sha(z.read(n))==x['sha256'] and len(z.read(n))==x['bytes'];matched+=1
    assert matched>=58
    scoped=json.loads(z.read('docs/ROADMAP_TASKS.json'));tasks={t['id']:t for t in scoped['tasks']}
    assert tasks['P3-BEN-09']['status']=='todo' and tasks['P3-BEN-09']['gate']=='required' and 'P3-BEN-09' in tasks['P3-GATE-02']['depends_on']
 replay="""
import sys,pathlib,json,copy
from unittest import mock
sys.dont_write_bytecode=True;sys.path.insert(0,sys.argv[1])
import run_current_vulkan_draw as v
folder=pathlib.Path(sys.argv[2]);original=v.read(folder/'receipt.json');result=v.check(folder);assert result['measurement_samples']==900 and result['GPU_timestamp_pending_task']=='P3-BEN-09'
checks=[]
def reject(name,change):
 r=copy.deepcopy(original);change(r);reader=v.read
 def replacement(p):return r if pathlib.Path(p)==folder/'receipt.json' else reader(p)
 with mock.patch.object(v,'read',side_effect=replacement):
  try:v.check(folder)
  except ValueError:checks.append(name)
  else:raise AssertionError('Altered evidence accepted: '+name)
reject('total statistics changed',lambda r:r['groups'][0]['statistics']['total_ms'].update(p50_ms=999))
reject('record statistics changed',lambda r:r['groups'][0]['statistics']['record_ms'].update(p50_ms=999))
reject('process missing',lambda r:r['runs'].pop())
reject('measurement order changed',lambda r:r['runs'].__setitem__(slice(10,12),r['runs'][10:12][::-1]))
reject('extra process',lambda r:r['runs'].append(copy.deepcopy(r['runs'][-1])))
reject('validation injected into timing',lambda r:r['runs'][10]['overrides'].update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'))
reject('readback total changed',lambda r:r.update(verified_bytes=0))
reject('parsed interval changed',lambda r:r['runs'][10]['result']['CPU_intervals_ms'][0].__setitem__(0,999))
reject('instance count changed',lambda r:r['runs'][10]['result'].update(instances_per_call=999))
reject('overlapping processes',lambda r:r['runs'][1].update(started=r['runs'][0]['started']))
reject('fallback control succeeded',lambda r:r['runs'][6].update(returncode=0))
reject('LUID changed',lambda r:r['identity'].update(device_luid='0100000000000000'))
reject('historical timing promoted',lambda r:r.update(historical_timings_reused=True))
reject('GPU timestamp promoted',lambda r:r.update(GPU_timestamp_pending_task='done'))
print(json.dumps(dict(offline_replay=result,tamper_rejections=checks,source_capsule='Windows execution bytes',native_GPU_executed=False)))
"""
 p=subprocess.run([sys.executable,'-B','-c',replay,str(root/'tools'),str(F/'accepted')],capture_output=True,text=True,timeout=60)
 assert p.returncode==0,(p.stdout,p.stderr);replayed=json.loads(p.stdout);assert len(replayed['tamper_rejections'])==14
 if '--json' in sys.argv:print(json.dumps(replayed,indent=2))
probe=read(F/'initial-binding-probe/probe.stdout');ctx=read(F/'initial-binding-probe/context.json')
assert probe['device_luid']==r['identity']['device_luid']==ctx['current_LUID']!=ctx['old_LUID'] and probe['sample_count']==0 and ctx['historical_measurement_samples_reused']==0
assert sha((R/ctx['previous_qualification_archive']/'receipt.json').read_bytes())==ctx['previous_qualification_receipt_sha256']
prep=read(F/'accepted/qualification/prepared/receipt.json');assert prep['source_commit']==r['source_commit'] and prep['identity']==r['identity'] and len(prep['probes'])==8
py=read(F/'python-local/receipt.json');assert py['source_commit']==r['source_commit'] and py['successful'] and py['inputs_unchanged'] and py['tests']==822 and len(set(py['test_ids']))==822 and not py['skipped'] and not py['failures'] and not py['errors'] and sha((F/'python-local/unittest.log').read_bytes())==py['log']['sha256']
assert s['statistics']==r['groups'] and s['verified_images']==r['verified_images']==1074 and s['verified_bytes']==r['verified_bytes']==10331880
assert s['completed_workload_tasks']==['P3-BEN-04'] and s['GPU_timestamp_pending_task']=='P3-BEN-09' and not any(s[k] for k in ('historical_timings_reused','overall_SDK_ranking','GPU_timestamp_measurement','SDL_native_handle_verified','Intel_GPU_device_created','Linux_benchmark_executed','human_display_accepted','runtime_redistributed','native_draw_count_verified'))
assert s['public_submitted_draw_calls']==sum(x['result']['submitted_draw_calls'] for x in r['runs'] if 'result' in x)==358716
initial=read(F/'python-initial-context.json');assert not initial['source_or_fixture_fix_applied'] and initial['existing_input_matches_Git_normalized'] and initial['focused_retry_passed'] and initial['recorded_full_discovery_passed']
for root in (F,S):
 for p in root.rglob('*'):assert p.name!='__pycache__' and not p.name.casefold().endswith(('.exe','.dll','.obj','.lib','.a','.so','.pdb','.pyc'))
if (F/'ci-runs.json').is_file():
 ci=read(F/'ci-runs.json');assert len(ci['workflow_runs'])==2 and all(x['head_sha']==r['source_commit'] and x['conclusion']=='success' and x['status']=='completed' for x in ci['workflow_runs'])
if (F/'inventory.json').is_file():
 inv=read(F/'inventory.json');assert sha((R/inv['summary']['path']).read_bytes())==inv['summary']['sha256']
 assert {p.relative_to(F).as_posix() for p in F.rglob('*') if p.is_file() and p.name not in ('inventory.json','offline-verification.log')}=={x['path'] for x in inv['files']}
 for x in inv['files']:assert sha((F/x['path']).read_bytes())==x['sha256'] and (F/x['path']).stat().st_size==x['bytes']
if (R/'out/ben04-user-files.json').is_file():
 for n,h in read(R/'out/ben04-user-files.json').items():assert sha((R/n).read_bytes()).upper()==h
print('PASS current-LUID preparation/protocol;40 draw CPU processes/900 raw samples/1074 full images;24 statistics columns and14 tamper rejections;Python822;SDK28 unchanged;GPU timestamp comparison remains separate BEN09. No GPU execution by this verifier.')
