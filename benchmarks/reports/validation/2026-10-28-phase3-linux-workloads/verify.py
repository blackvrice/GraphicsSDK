"""Replay Linux workload preparation evidence with captured source bytes; no GPU."""
import pathlib,json,hashlib,zipfile,tempfile,subprocess,sys
R=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'docs/ROADMAP_TASKS.json').is_file())
F=R/'benchmarks/reports/validation/2026-10-28-phase3-linux-workloads';S=R/'benchmarks/reports/2026-10-28-phase3-linux-workloads'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
s=read(S/'summary.json');p=R/s['raw_receipt'];r=read(p);m=read(F/'source-manifest.json');a=m['archive'];assert sha((R/a['path']).read_bytes())==a['sha256']
assert r['source_commit']==s['source_commit']==m['source_commit']=='e4c1e3472be9bd2547ea42627993fa2cd1916dab'
art=read(F/'artifact.json')['artifacts'];assert len(art)==1 and art[0]['workflow_run']['head_sha']==r['source_commit'] and art[0]['digest']=='sha256:'+sha((F/'github-artifact.zip').read_bytes())
with zipfile.ZipFile(F/'github-artifact.zip') as z:
 files={n for n in z.namelist() if not n.endswith('/')};assert files=={x.relative_to(F/'native').as_posix() for x in (F/'native').rglob('*') if x.is_file()}
 for n in files:
  path=pathlib.PurePosixPath(n);assert not path.is_absolute() and '..' not in path.parts and '.so.' not in path.name
  assert not path.name.lower().endswith(('.exe','.dll','.so','.a','.lib','.pdb','.obj','.pyc')) and z.read(n)==(F/'native'/n).read_bytes()
with tempfile.TemporaryDirectory() as td:
 root=pathlib.Path(td)
 with zipfile.ZipFile(R/a['path']) as z:
  names={x['path'] for x in m['files']};assert set(z.namelist())==names|{'source-receipt.json'}
  for n in names:assert not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts
  for x in m['files']:assert len(z.read(x['path']))==x['bytes'] and sha(z.read(x['path']))==x['sha256']
  prefix=next(x['path'][:-len('tools/linux_workloads.py')] for x in r['source_inputs'] if x['path'].endswith('/tools/linux_workloads.py'))
  for x in r['source_inputs']+r['qualification']['source_inputs']:assert sha(z.read(x['path'][len(prefix):]))==x['sha256']
  sdk={n for n in names if n=='CMakeLists.txt' or n.startswith(('include/','src/'))};assert len(sdk)==28
  assert sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sorted(sdk)).encode())==r['qualification']['SDK_source_fingerprint']==m['SDK_source_fingerprint']
  old=read(R/'benchmarks/reports/validation/2026-10-27-phase3-linux-comparison-probe/source-manifest.json')
  with zipfile.ZipFile(R/old['archive']['path']) as prev:assert all(z.read(n)==prev.read(n) for n in sdk)
  z.extractall(root)
 replay="""
import pathlib,sys,json,copy
from unittest import mock
sys.dont_write_bytecode=True;sys.path.insert(0,sys.argv[1]);import linux_workloads as v
folder=pathlib.Path(sys.argv[2]);original=v.read(folder/'receipt.json');result=v.check(folder);checks=[]
for profile in v.port.PROFILES:
 source=v.ROOT/('benchmarks/vulkan_'+profile)/'main.cpp';text,changes=v.port.translate(profile,source.read_text(encoding='utf-8'))
 assert text.encode()==(folder/'generated'/(profile+'.cpp')).read_bytes() and len(changes)==5
 manifest=v.read(folder/'generated'/(profile+'.json'))
 # Use the captured Linux POSIX ordering; Path ordering can differ on Windows.
 assert [x['path'] for x in manifest['inputs']]==sorted(p.relative_to(v.ROOT).as_posix() for p in v.port.paths(profile))
 assert v.sha(source)==manifest['original_source_sha256']
def reject(name,change):
 r=copy.deepcopy(original);change(r);reader=v.read
 def replacement(p):return r if pathlib.Path(p)==folder/'receipt.json' else reader(p)
 with mock.patch.object(v,'read',side_effect=replacement):
  try:v.check(folder)
  except ValueError:checks.append(name)
  else:raise AssertionError('Altered evidence accepted: '+name)
reject('software promoted to BEN08 completion',lambda r:r.update(P3_BEN_08_completed=True))
reject('physical acceptance invented',lambda r:r.update(physical_GPU_accepted=True))
reject('performance accepted',lambda r:r.update(performance_accepted=True))
reject('all scenarios accepted',lambda r:r.update(all_workload_scenarios_accepted=True))
reject('performance samples invented',lambda r:r.update(performance_samples=24))
reject('validation timings promoted',lambda r:r.update(validation_timings_excluded=False))
reject('qualification hash changed',lambda r:r.update(qualification_sha256='f'*64))
reject('missing process',lambda r:r['runs'].pop())
reject('order changed',lambda r:r['runs'].__setitem__(slice(0,2),r['runs'][:2][::-1]))
reject('overlap',lambda r:r['runs'][1].update(started=r['runs'][0]['started']))
reject('wrong command',lambda r:r['runs'][0]['argv'].__setitem__(2,'other'))
reject('layer injected into plain probe',lambda r:r['runs'][0]['overrides'].update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'))
reject('control success',lambda r:r['runs'][16].update(returncode=0))
reject('readback count changed',lambda r:r.update(verified_bytes=0))
reject('parsed shader changed',lambda r:r['runs'][10]['result'].update(shader_header_sha256='f'*64))
reject('profile fingerprint changed',lambda r:r['profile_fingerprints'].update(draw='f'*64))
reject('source input hash changed',lambda r:r['source_inputs'][0].update(sha256='f'*64))
reject('raw stdout hash changed',lambda r:r['runs'][0].update(stdout_sha256='f'*64))
reject('boolean PID',lambda r:r['runs'][0].update(pid=True))
assert len(checks)==19
print(json.dumps(dict(offline_replay=result,tamper_rejections=checks,generated_sources_reproduced=4,native_GPU_executed=False)))
"""
 child=subprocess.run([sys.executable,'-B','-c',replay,str(root/'tools'),str(p.parent)],capture_output=True,text=True,timeout=60);assert child.returncode==0,(child.stdout,child.stderr);result=json.loads(child.stdout)
 if '--json' in sys.argv:print(json.dumps(result,indent=2))
assert (s['verified_images'],s['verified_buffers'],s['verified_bytes'])==(24,8,263648)
assert s['status']=='prepared_actual_physical_GPU_pending' and s['mode']=='software-diagnostic' and r['qualification']['selected']['device_type']==4
assert not any(s[k] for k in ('P3_BEN_08_completed','P3_BEN_07_completed','physical_GPU_accepted','performance_accepted','performance_samples','all_workload_scenarios_accepted','SDK_product_source_changed','Windows_consumers_changed','external_runtime_binaries_redistributed','manual_physical_workflow_dispatched','physical_probe_executions'))
assert s['diagnostic_sample_iterations']==24 and s['validation_timings_excluded'] and s['excluded_warmups']==8
py=read(F/'python-local/receipt.json');assert py['source_commit']==r['source_commit'] and py['successful'] and py['inputs_unchanged'] and py['tests']==889 and not py['failures'] and not py['errors'] and not py['skipped'] and sha((F/'python-local/unittest.log').read_bytes())==py['log']['sha256']
for base in (F,S):
 for path in base.rglob('*'):assert path.name!='__pycache__' and not path.name.lower().endswith(('.exe','.dll','.so','.a','.lib','.pdb','.obj','.pyc'))
if (F/'ci-runs.json').is_file():
 ci=read(F/'ci-runs.json');assert len(ci['workflow_runs'])==2 and all(x['conclusion']=='success' and x['head_sha']==r['source_commit'] for x in ci['workflow_runs'])
if (F/'inventory.json').is_file():
 inv=read(F/'inventory.json');assert sha((S/'summary.json').read_bytes())==inv['summary_sha256']
 assert {x.relative_to(F).as_posix() for x in F.rglob('*') if x.is_file() and x.name not in ('inventory.json','offline-verification.log')}=={x['path'] for x in inv['files']}
 for x in inv['files']:assert sha((F/x['path']).read_bytes())==x['sha256'] and (F/x['path']).stat().st_size==x['bytes']
print('PASS four generated Linux consumers reproduced;probes8/smokes8/controls24;24 images+8 buffers/263648 bytes;889 Python tests;19 tamper rejections;physical BEN07/BEN08 pending. No GPU execution by this verifier.')
