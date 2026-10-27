"""Replay exact PSO CPU source bytes; no GPU/native binary execution."""
import pathlib,json,hashlib,zipfile,tempfile,subprocess,sys
R=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'docs/ROADMAP_TASKS.json').is_file())
F=R/'benchmarks/reports/validation/2026-10-27-phase3-current-pso';S=R/'benchmarks/reports/2026-10-27-phase3-current-pso'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
r=read(F/'accepted/receipt.json');s=read(S/'summary.json');assert r['status']=='completed' and r['source_commit']==s['source_commit']=='37a8dda009fdbce81bb64aa25036a658c7f40fc0'
with tempfile.TemporaryDirectory() as td:
 root=pathlib.Path(td)
 for kind in ('windows','linux-git'):
  m=read(F/f'source-manifest-{kind}.json');a=m['archive'];assert sha((R/a['path']).read_bytes())==a['sha256'] and (R/a['path']).stat().st_size==a['bytes']
  assert len(m['files'])==87 and m['SDK_files']==28 and m['SDK_product_unchanged'] and not m['compiled_or_external_binaries']
  with zipfile.ZipFile(R/a['path']) as z:
   names={x['path'] for x in m['files']};assert set(z.namelist())==names|{'source-receipt.json'}
   assert all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts for n in names)
   for x in m['files']:assert len(z.read(x['path']))==x['bytes'] and sha(z.read(x['path']))==x['sha256']
   sdk={n for n in names if n=='CMakeLists.txt' or n.startswith(('src/','include/'))};assert len(sdk)==28
   assert sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sorted(sdk)).encode())==m['SDK_source_fingerprint']
   previous=read(R/f'benchmarks/reports/validation/2026-10-27-phase3-current-draw/source-manifest-{kind}.json')
   with zipfile.ZipFile(R/previous['archive']['path']) as old:assert all(z.read(n)==old.read(n) for n in sdk)
   if kind=='windows':
    z.extractall(root);prefix=r['validation_library'].split('/out/')[0]+'/';matched=0
    for x in r['frozen_inputs']:
     n=x['path'][len(prefix):]
     if n in names:assert sha(z.read(n))==x['sha256'] and len(z.read(n))==x['bytes'];matched+=1
    assert matched>=58
    scoped=json.loads(z.read('docs/ROADMAP_TASKS.json'));tasks={t['id']:t for t in scoped['tasks']}
    assert tasks['P3-BEN-05']['status']=='todo' and tasks['P3-BEN-09']['status']=='todo' and tasks['P3-BEN-09']['gate']=='required' and 'P3-BEN-09' in tasks['P3-GATE-02']['depends_on']
 replay="""
import sys,pathlib,json,copy
from unittest import mock
sys.dont_write_bytecode=True;sys.path.insert(0,sys.argv[1])
import run_current_vulkan_pso as v
folder=pathlib.Path(sys.argv[2]);original=v.read(folder/'receipt.json');result=v.check(folder);assert result['measurement_samples']==600 and result['pso_accepted']
checks=[]
def reject(name,change):
 r=copy.deepcopy(original);change(r);reader=v.read
 def replacement(p):return r if pathlib.Path(p)==folder/'receipt.json' else reader(p)
 with mock.patch.object(v,'read',side_effect=replacement):
  try:v.check(folder)
  except ValueError:checks.append(name)
  else:raise AssertionError('Altered evidence accepted: '+name)
reject('creation statistics changed',lambda r:r['groups'][0]['statistics']['create_ms'].update(p50_ms=999))
reject('release statistics changed',lambda r:r['groups'][0]['statistics']['public_release_ms'].update(p50_ms=999))
reject('process missing',lambda r:r['runs'].pop())
reject('measurement order changed',lambda r:r['runs'].__setitem__(slice(8,10),r['runs'][8:10][::-1]))
reject('extra process',lambda r:r['runs'].append(copy.deepcopy(r['runs'][-1])))
reject('validation injected into timing',lambda r:r['runs'][8]['overrides'].update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'))
reject('readback total changed',lambda r:r.update(verified_bytes=0))
reject('parsed interval changed',lambda r:r['runs'][8]['result']['samples_ms'][0].__setitem__(0,999))
reject('pipeline use changed',lambda r:r['runs'][8]['result'].update(used_pipeline_regions=0))
reject('overlapping processes',lambda r:r['runs'][1].update(started=r['runs'][0]['started']))
reject('fallback control succeeded',lambda r:r['runs'][4].update(returncode=0))
reject('LUID changed',lambda r:r['identity'].update(device_luid='0100000000000000'))
reject('historical timing promoted',lambda r:r.update(historical_timings_reused=True))
reject('GPU use promoted into CPU timing',lambda r:r.update(GPU_use_in_timing=True))
reject('driver cache classification invented',lambda r:r.update(cache_state='cold'))
reject('native creation count claimed',lambda r:r.update(native_pipeline_count_verified=True))
reject('physical reclamation claimed',lambda r:r.update(GPU_memory_reclamation_verified=True))
print(json.dumps(dict(offline_replay=result,tamper_rejections=checks,source_capsule='Windows execution bytes',native_GPU_executed=False)))
"""
 p=subprocess.run([sys.executable,'-B','-c',replay,str(root/'tools'),str(F/'accepted')],capture_output=True,text=True,timeout=60)
 assert p.returncode==0,(p.stdout,p.stderr);replayed=json.loads(p.stdout);assert len(replayed['tamper_rejections'])==17
 if '--json' in sys.argv:print(json.dumps(replayed,indent=2))
probe=read(F/'current-binding-probe/probe.stdout');ctx=read(F/'current-binding-probe/context.json')
assert all(probe[k]==value for k,value in r['identity'].items()) and probe['sample_count']==0 and ctx['identity_matches'] and ctx['historical_measurement_samples_reused']==0
q=read(F/'accepted/qualification/receipt.json');prep=read(F/'accepted/qualification/prepared/receipt.json')
assert prep['source_commit']==q['source_commit']==ctx['qualification_source_commit']==ctx['preparation_source_commit']=='2323f3ab0e4c6de6ae5c75b17c2dfdc257ac88ac'
assert r['source_commit']==ctx['campaign_source_commit'] and q['identity']==prep['identity']==r['identity'] and len(prep['probes'])==8
assert q['SDK_source_fingerprint']==prep['SDK_source_fingerprint']==r['SDK_source_fingerprint'] and q['profile_fingerprints']==r['profile_fingerprints']
py=read(F/'python-local/receipt.json');assert py['source_commit']==r['source_commit'] and py['successful'] and py['inputs_unchanged'] and py['tests']==839 and len(set(py['test_ids']))==839 and not py['skipped'] and not py['failures'] and not py['errors'] and sha((F/'python-local/unittest.log').read_bytes())==py['log']['sha256']
assert s['statistics']==r['groups'] and s['verified_images']==r['verified_images']==716 and s['verified_bytes']==r['verified_bytes']==6887920
assert s['completed_workload_tasks']==['P3-BEN-05'] and not any(s[k] for k in ('historical_timings_reused','overall_SDK_ranking','GPU_timestamp_measurement','SDL_native_handle_verified','Intel_GPU_device_created','Linux_benchmark_executed','human_display_accepted','runtime_redistributed','native_pipeline_count_verified','GPU_memory_reclamation_verified','driver_cache_state_known','GPU_use_in_timing','new_preparation_or_qualification_executed'))
assert s['cache_state']==r['cache_state']=='uncontrolled_driver_cache_repeated_public_creation' and s['qualified_inputs_reused_unchanged']
assert s['public_pipeline_counts']=={k:sum(x['result'][k] for x in r['runs'] if 'result' in x) for k in ('created_pipelines','released_pipeline_handles','used_pipeline_regions')}
assert set(s['public_pipeline_counts'].values())=={3222}
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
print('PASS unchanged qualified inputs;28 PSO CPU processes/600 fresh paired intervals/716 full images;8 statistics columns and17 tamper rejections;Python839;SDK28 unchanged;driver cache uncontrolled. No GPU execution by this verifier.')
