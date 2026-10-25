import hashlib,json,pathlib,subprocess,sys,tempfile,zipfile
ROOT=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'tools/audit_licenses.py').is_file())
F=ROOT/'benchmarks/reports/validation/2026-10-25-phase2-memory-resumed';N=F/'native'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def record(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
manifest=read(F/'source-manifest.json');archive=ROOT/manifest['archive']['path']
assert sha(archive.read_bytes())==manifest['archive']['sha256']
snapshot=tempfile.TemporaryDirectory(prefix='graphics-memory-evidence-');snapshot_root=pathlib.Path(snapshot.name).resolve()
with zipfile.ZipFile(archive) as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(manifest['files'])+1
 for row in manifest['files']:
  target=snapshot_root/row['path'];assert target.resolve().is_relative_to(snapshot_root) and '..' not in pathlib.PurePosixPath(row['path']).parts
  data=z.read(row['path']);assert len(data)==row['bytes'] and sha(data)==row['sha256'];target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
# Use the exact recorded validators/policy, including their original Windows bytes.
# Current checkout code and line endings are not substituted for historical evidence.
sys.path.insert(0,str(snapshot_root/'tools'))
import run_memory_stress as stress
import memory_environment as environment
from audit_licenses import sdk_fingerprint
from analyze_memory_stress import analyze,read_records,validate_campaign,OWNED
s=read(N/'resumption.json');fp=s['SDK_source_fingerprint'];policy=read(stress.POLICY)
assert fp=='2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401'
assert s['source_commit']=='a93af3b52719dfb9402081cf4b633351d6057928'
assert not s['accepted'] and s['status']=='rejected' and s['inputs_unchanged'] and s['recorded_environment_controls_match']
assert s['guard_MiB']==512 and s['policy_sha256']==sha(stress.POLICY.read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
assert len(s['cases'])==3 and [c['mode'] for c in s['cases']]==['gpu','validation','warp']
assert len(s['errors'])==1 and s['errors'][0]['mode']=='validation'
manifest=read(F/'source-manifest.json');archive=ROOT/manifest['archive']['path']
assert sha(archive.read_bytes())==manifest['archive']['sha256'] and archive.stat().st_size==manifest['archive']['bytes']
with zipfile.ZipFile(archive) as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(manifest['files'])+1
 assert not any(p.startswith('.deps/') or pathlib.PurePosixPath(p).suffix.lower() in ('.dll','.exe','.lib','.so') for p in z.namelist())
 for row in manifest['files']:
  b=z.read(row['path']);assert len(b)==row['bytes'] and sha(b)==row['sha256']
 sdk=[r for r in manifest['files'] if r['path']=='CMakeLists.txt' or r['path'].startswith('include/') and r['path'].endswith('.hpp') or r['path'].startswith('src/') and r['path'].endswith(('.cpp','.hpp'))]
 assert len(sdk)==27 and sha(''.join(r['path']+'|'+r['sha256']+'\n' for r in sorted(sdk,key=lambda x:x['path'])).encode())==fp
 for mode in ('gpu','validation','warp'):
  for p in (N/mode/'source').rglob('*'):
   if p.is_file():assert p.read_bytes()==z.read(p.relative_to(N/mode/'source').as_posix())
 binaries=read(F/'binary-identities.json');assert len(binaries)==3 and all(b['contains_current_SDK_fingerprint'] for b in binaries)
 for row in binaries:
  matches=[v for p,v in s['frozen_inputs'].items() if p.replace('\\','/').endswith('/'+row['path'])];assert matches==[row['sha256']]
before=read(N/'environment-before.json');after=read(N/'environment-after.json')
environment.validate_snapshot(before);environment.validate_snapshot(after)
comparison=environment.compare_snapshots(before,after,within_run=True);assert comparison==read(N/'environment-comparison.json') and comparison['recorded_controls_match']
assert before['controls']['adapter']['vendor_id']==0x10de and before['observations']['adapter_id']==96751
assert s['hardware']['adapter_id']==96751 and s['warp']['adapter_id']==107125 and s['warp']['software'] and not s['hardware']['software']
results=[]
for mode,exitcode in [('gpu',0),('validation',2),('warp',0)]:
 case=N/mode;rows=read_records(case/(mode+'.jsonl'));h,f=rows[0],rows[-1]
 c=read(case/'campaign.json');a=read(case/'acceptance.json');r=next(x for x in s['cases'] if x['mode']==mode)
 assert c['inputs_unchanged'] and not c['accepted'] and not c['diagnostic_only'] and len(c['processes'])==1
 assert set(c['frozen_inputs'].items())<=set(s['frozen_inputs'].items())
 assert c['processes'][0]['returncode']==exitcode and c['processes'][0]['status']=='exited' and c['base_commit']==s['source_commit']
 assert r['acceptance']==a and r['runner_exit']==(1 if exitcode else 0) and r['inputs_unchanged']
 assert h['schema']==3 and h['seconds']==policy['minimum_seconds'][mode] and h['warmup']==30 and h['interval']==5 and h['private_growth_limit_bytes']==512*2**20
 adapter=107125 if mode=='warp' else 96751
 native=stress.validate_native(rows,fp,mode,adapter,exitcode)
 assert h['adapter_id']==h['observer_adapter_id']==h['requested_adapter_id']==f['adapter_id']==adapter
 assert h['gpu_validation']==h['gpu_state_tracking']==(mode=='validation')
 assert all(all(row[k]==0 for k in OWNED) for row in rows if row.get('phase') in ('drained','final','guard_aborted'))
 assert f['validation_errors']==0 and f['statistics_contracts']
 result=dict(mode=mode,adapter_id=adapter,native_exit=exitcode,runner_exit=r['runner_exit'],measured_seconds=f['measured_seconds'],elapsed_seconds=f['elapsed'],frames=f['frames'],full_images=f['full_images'],verified_buffer_bytes=f['verified_buffer_bytes'],status=f['status'],stop_reason=f['stop_reason'],private_baseline_bytes=h['guard_baseline_private_bytes'],private_final_bytes=rows[-2]['private_bytes'],private_growth_bytes=rows[-2]['private_bytes']-h['guard_baseline_private_bytes'],owned_after_drain=0,public_memory_budget=native['public_memory_budget'])
 if mode!='validation':
  checked=analyze(rows,fp,policy);assert checked['accepted'] and not a['errors'];expected={**native,**checked};assert a['campaigns']==[expected]
  result.update(accepted=True,metrics=checked['metrics'],drained_samples=checked['drained_samples'],tail_samples=checked['tail_samples'])
 else:
  assert a['campaigns']==[native] and a['errors']==[dict(mode='validation',error='Private growth guard aborted: validation')]
  assert f['measured_seconds']==0 and f['stop_reason']=='private_growth_limit' and result['private_growth_bytes']>512*2**20
  try:analyze(rows,fp,policy)
  except ValueError as error:result['original_analyzer_rejection']=str(error)
  else:raise AssertionError('Aborted GBV accepted')
  result['accepted']=False
 results.append(result)
validate_campaign(results);assert sum(r['accepted'] for r in results)==2
assert sum(r['frames'] for r in results)==161424 and sum(r['full_images'] for r in results)==484272
git=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false']
for row in manifest['files']:
 if row['path'].startswith('out/'):continue
 original=subprocess.check_output(git+['show',s['source_commit']+':'+row['path']],cwd=ROOT)
 assert original.replace(b'\r\n',b'\n')==(snapshot_root/row['path']).read_bytes().replace(b'\r\n',b'\n'),row['path']
assert 'Ran 57 tests' in (F/'setup/memory-python-tests.log').read_text(encoding='utf-8-sig') and '\nOK' in (F/'setup/memory-python-tests.log').read_text(encoding='utf-8-sig')
full=read(F/'setup/full-python-receipt.json');assert full['tests']==654 and full['successful'] and full['inputs_unchanged'] and not any(full[k] for k in ('errors','failures','skipped','expected_failures','unexpected_successes'))
log=(F/'setup/full-python-unittest.log').read_bytes();assert len(log)==full['log']['bytes'] and sha(log)==full['log']['sha256']
if '--live-inputs' in sys.argv:
 assert sdk_fingerprint(ROOT)==fp
 for path,digest in s['frozen_inputs'].items():assert sha(pathlib.Path(path).read_bytes())==digest,path
 print('PASS all live frozen inputs match the executed binary/source hashes')
print('PASS original policy/data verified: GPU 900s and WARP 300s accepted; full GBV guard abort preserved; overall acceptance=false')
if '--summary' in sys.argv:
 summary=dict(schema_version=1,task='P2-ACC-01',task_status='todo',gate='required',resumed_by_user=True,source_commit=s['source_commit'],SDK_source_fingerprint=fp,SDK_CXX_or_CMakeLists_modified=False,original_policy_preserved=True,policy_sha256=s['policy_sha256'],accepted=False,overall_result='rejected: full GBV private growth guard during warmup',started_utc=s['started_utc'],completed_utc=s['completed_utc'],results=results,total_frames=sum(r['frames'] for r in results),total_full_images=sum(r['full_images'] for r in results),total_verified_buffer_bytes=sum(r['verified_buffer_bytes'] for r in results),inputs_unchanged=True,recorded_environment_controls_match=True,selected_adapter=before['controls']['adapter'],selected_driver=before['selected_driver'],os=before['controls']['os'],Intel_AV_investigation_resumed=False,memory_Python_tests=57,source_archive=manifest['archive'],verification=dict(result='pass',meaning='Evidence/strict rejection verified; original three-mode memory gate remains incomplete'),evidence=[record(p) for p in sorted(F.rglob('*')) if p.is_file()])
 summary['full_Python_tests']=654
 summary['historical_validation_scope']='Exact source ZIP validators and policy restored byte-for-byte; original SDK fingerprint is not normalized or reassigned'
 dest=ROOT/'benchmarks/reports/2026-10-25-phase2-memory-resumed/summary.json';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
