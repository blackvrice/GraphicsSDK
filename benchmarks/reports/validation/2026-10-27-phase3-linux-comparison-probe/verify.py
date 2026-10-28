"""Replay Linux software probe evidence using the captured source bytes; no GPU."""
import pathlib,json,hashlib,zipfile,tempfile,subprocess,sys
R=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'docs/ROADMAP_TASKS.json').is_file())
F=R/'benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe';S=R/'benchmarks/reports/2026-10-27-phase3-linux-comparison-probe'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
s=read(S/'summary.json');p=R/s['raw_receipt'];r=read(p);m=read(F/'source-manifest.json');a=m['archive'];assert sha((R/a['path']).read_bytes())==a['sha256']
artifact=read(F/'artifact.json')['artifacts'];assert len(artifact)==1 and artifact[0]['workflow_run']['head_sha']==r['source_commit']
assert artifact[0]['digest']=='sha256:'+sha((F/'github-artifact.zip').read_bytes())
with zipfile.ZipFile(F/'github-artifact.zip') as raw:
 files={n for n in raw.namelist() if not n.endswith('/')};assert files=={x.relative_to(F/'native').as_posix() for x in (F/'native').rglob('*') if x.is_file()}
 for n in files:
  name=pathlib.PurePosixPath(n);assert not name.is_absolute() and '..' not in name.parts and '.so.' not in name.name
  assert not name.name.lower().endswith(('.exe','.dll','.so','.a','.lib','.obj','.pdb','.pyc')) and raw.read(n)==(F/'native'/n).read_bytes()
assert r['source_commit']==s['source_commit']==m['source_commit']=='ece1c87c613802a97fe8642f1755935a5c1058f4'
with tempfile.TemporaryDirectory() as td:
 root=pathlib.Path(td)
 with zipfile.ZipFile(R/a['path']) as z:
  names={x['path'] for x in m['files']};assert set(z.namelist())==names|{'source-receipt.json'}
  for n in names:assert not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts
  for x in m['files']:assert len(z.read(x['path']))==x['bytes'] and sha(z.read(x['path']))==x['sha256']
  prefix=next(x['path'][:-len('tools/linux_comparison.py')] for x in r['source_inputs'] if x['path'].endswith('/tools/linux_comparison.py'))
  for x in r['source_inputs']:assert sha(z.read(x['path'][len(prefix):]))==x['sha256']
  sdk={n for n in names if n=='CMakeLists.txt' or n.startswith(('include/','src/'))};assert len(sdk)==m['SDK_files']==28
  assert sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sorted(sdk)).encode())==r['SDK_source_fingerprint']==m['SDK_source_fingerprint']
  old=read(R/'benchmarks/reports/validation/2026-10-27-phase3-current-allocation/source-manifest-linux-git.json')
  with zipfile.ZipFile(R/old['archive']['path']) as previous:assert all(z.read(n)==previous.read(n) for n in sdk)
  z.extractall(root)
 replay="""
import pathlib,sys,json,copy
from unittest import mock
sys.dont_write_bytecode=True;sys.path.insert(0,sys.argv[1]);import linux_comparison as v
folder=pathlib.Path(sys.argv[2]);original=v.read(folder/'receipt.json');assert v.fingerprint()==original['probe_fingerprint'];result=v.check(folder)
assert not result['P3_BEN_07_completed'] and not result['physical_probe_qualified'] and result['measurement_samples']==0
checks=[]
def reject(name,change):
 r=copy.deepcopy(original);change(r);reader=v.read
 def replacement(p):return r if pathlib.Path(p)==folder/'receipt.json' else reader(p)
 with mock.patch.object(v,'read',side_effect=replacement):
  try:v.check(folder)
  except ValueError:checks.append(name)
  else:raise AssertionError('Altered evidence accepted: '+name)
reject('software promoted to BEN07 completion',lambda r:r.update(P3_BEN_07_completed=True))
reject('software promoted to physical probe',lambda r:r.update(physical_probe_qualified=True))
reject('performance accepted',lambda r:r.update(performance_accepted=True))
reject('workload accepted',lambda r:r.update(workload_accepted=True))
reject('timing samples invented',lambda r:r.update(measurement_samples=1))
reject('source fingerprint changed',lambda r:r.update(probe_fingerprint='f'*64))
reject('kernel changed',lambda r:r['environment'].update(kernel='other'))
reject('selected driver UUID changed',lambda r:r['selected'].update(driver_uuid='f'*32))
reject('missing process',lambda r:r['runs'].pop())
reject('process order changed',lambda r:r['runs'].__setitem__(slice(0,2),r['runs'][:2][::-1]))
reject('overlapping processes',lambda r:r['runs'][1].update(started=r['runs'][0]['started']))
reject('wrong command',lambda r:r['runs'][0]['argv'].__setitem__(3,'f'*32))
reject('validation injected into plain probe',lambda r:r['runs'][0]['overrides'].update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'))
reject('fallback control succeeded',lambda r:r['runs'][8].update(returncode=0))
reject('parsed identity changed',lambda r:r['runs'][0]['result']['selected'].update(driver_uuid='f'*32))
reject('runtime receipt changed',lambda r:r.update(runtime_receipt_sha256='f'*64))
reject('inventory output changed',lambda r:r.update(inventory_sha256='f'*64))
reject('native CPU controls changed',lambda r:r.update(selection_sha256='f'*64))
reject('boolean PID accepted',lambda r:r['runs'][0].update(pid=True))
print(json.dumps(dict(offline_replay=result,tamper_rejections=checks,native_GPU_executed=False)))
"""
 child=subprocess.run([sys.executable,'-B','-c',replay,str(root/'tools'),str(p.parent)],capture_output=True,text=True,timeout=60);assert child.returncode==0,(child.stdout,child.stderr)
 result=json.loads(child.stdout);assert len(result['tamper_rejections'])==19
 if '--json' in sys.argv:print(json.dumps(result,indent=2))
assert s['status']=='prepared_actual_physical_GPU_pending' and s['mode']==r['mode']=='software-diagnostic'
assert not any(s[k] for k in ('physical_probe_qualified','P3_BEN_07_completed','workload_accepted','performance_accepted','measurement_samples','SDL_native_handle_verified','physical_probe_executions','manual_physical_workflow_dispatched','SDK_product_source_changed','Windows_consumers_changed','external_runtime_binaries_redistributed'))
assert s['selected']==r['selected'] and s['environment']==r['environment'] and r['selected']['device_type']==4 and r['selected']['device_luid'] is None
for row in r['runs']:
 if row['job']['kind']=='probe':assert {x['path'] for x in r['runtime_libraries']}<=set(row['result']['runtime_modules'])
py=read(F/'python-local/receipt.json');assert py['source_commit']==r['source_commit'] and py['successful'] and py['inputs_unchanged'] and py['tests']==876 and not py['errors'] and not py['failures'] and not py['skipped'] and sha((F/'python-local/unittest.log').read_bytes())==py['log']['sha256']
for base in (F,S):
 for path in base.rglob('*'):assert path.name!='__pycache__' and not path.name.lower().endswith(('.exe','.dll','.so','.a','.lib','.pdb','.obj','.pyc'))
if (F/'ci-runs.json').is_file():
 ci=read(F/'ci-runs.json');assert len(ci['workflow_runs'])==3 and all(x['conclusion']=='success' and x['head_sha']==r['source_commit'] for x in ci['workflow_runs'])
if (F/'inventory.json').is_file():
 inv=read(F/'inventory.json');assert sha((S/'summary.json').read_bytes())==inv['summary_sha256']
 assert {x.relative_to(F).as_posix() for x in F.rglob('*') if x.is_file() and x.name not in ('inventory.json','offline-verification.log')}=={x['path'] for x in inv['files']}
 for x in inv['files']:assert sha((F/x['path']).read_bytes())==x['sha256'] and (F/x['path']).stat().st_size==x['bytes']
print('PASS actual Linux software probes8/controls6, native CPU assertions9;876 Python tests;19 tamper rejections;SDK28 unchanged;BEN07 physical acceptance remains pending. No GPU execution by this verifier.')
