"""Verify historical evidence using exact archived validators and policy bytes."""
import hashlib,json,pathlib,sys,tempfile,zipfile
ROOT=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'tools/audit_licenses.py').is_file())
F=ROOT/'benchmarks/reports/validation/2026-10-25-phase2-intel-crash-resumed';N=F/'native'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def record(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
manifest=read(F/'source-manifest.json');archive=ROOT/manifest['archive']['path'];assert record(archive)==manifest['archive']
temporary=tempfile.TemporaryDirectory(prefix='graphics-intel-evidence-');snapshot=pathlib.Path(temporary.name).resolve()
with zipfile.ZipFile(archive) as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(manifest['files'])+1
 assert not any(pathlib.PurePosixPath(n).suffix.lower() in ('.dmp','.exe','.dll','.lib','.pdb','.so') or n.startswith('.deps/') for n in z.namelist())
 for row in manifest['files']:
  target=snapshot/row['path'];assert target.resolve().is_relative_to(snapshot)
  data=z.read(row['path']);assert len(data)==row['bytes'] and sha(data)==row['sha256']
  target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
 assert json.loads(z.read('source-receipt.json'))=={k:v for k,v in manifest.items() if k!='archive'}
 for case in ('normal-512','captured-512'):
  for p in (N/case/'source').rglob('*'):
   if p.is_file():assert p.read_bytes()==z.read(p.relative_to(N/case/'source').as_posix())
sys.path.insert(0,str(snapshot/'tools'))
from audit_licenses import sdk_fingerprint
from analyze_memory_stress import analyze,read_records,POLICY
from run_memory_stress import validate_native
from crash_capture.evidence import validate
import memory_environment as environment
s=read(N/'resumption.json');fp=s['SDK_source_fingerprint']
assert sdk_fingerprint(snapshot)==fp==manifest['SDK_source_fingerprint']
assert s['source_commit']=='59c28fc1d98fe38b3cde52b21060e4870a20a651'
assert s['status']=='bounded_pair_complete_without_av' and s['inputs_unchanged']
assert s['diagnostic_only'] and not s['memory_acceptance'] and not s['crash_cause_resolved']
assert s['serialized'] and s['adapter']['vendor_id']==0x8086 and s['adapter']['adapter_id']==107212 and not s['adapter']['software']
assert sha(POLICY.read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
assert len(s['cases'])==2
results=[]
for row in s['cases']:
 name=row['name'];captured=row['kind']=='captured';case=N/name
 assert row['guard_MiB']==512 and row['requested_measurement_seconds']==300 and row['warmup_seconds']==30
 assert row['status'] in ('completed','guard_aborted') and row['native_exit'] in (0,2)
 assert row['inputs_unchanged'] and row['recorded_environment_controls_match']
 before=read(N/(name+'-environment-before.json'));after=read(N/(name+'-environment-after.json'))
 comparison=environment.compare_snapshots(before,after,within_run=True)
 assert comparison==read(N/(name+'-environment-comparison.json')) and comparison['recorded_controls_match']
 assert before['controls']['SDK_source_fingerprint']==fp and before['observations']['adapter_id']==107212
 records=read_records(case/('memory.jsonl' if captured else 'validation.jsonl'));h,f=records[0],records[-1]
 assert h['schema']==3 and h['seconds']==300 and h['warmup']==30 and h['interval']==5
 assert h['private_growth_limit_bytes']==512*2**20 and h['gpu_validation'] and h['gpu_state_tracking']
 refs=validate_native(records,fp,'validation',107212,row['native_exit'])
 receipt=read(case/('summary.json' if captured else 'campaign.json'));assert receipt['inputs_unchanged']
 assert set(receipt['frozen_inputs'].items())<=set(s['frozen_inputs'].items())
 if captured:
  capture=read_records(case/'capture.jsonl');verified=validate(capture,row['native_exit'],receipt['collector_exit_code'])
  assert verified==receipt['capture'] and not verified['second_chance_count'] and receipt['capture_complete']
  assert not receipt['memory_acceptance'] and receipt['references']==refs
  assert receipt['core_library'].replace('\\','/').endswith('/out/intel-crash-resume-symbol-sdk/Release/GraphicsSDKCore.lib')
  assert 'local_dump' not in receipt
 else:
  assert len(receipt['processes'])==1 and receipt['processes'][0]['returncode']==row['native_exit']
  assert receipt['base_commit']==s['source_commit'] and not receipt['accepted']
 numeric={}
 try:numeric=analyze(records,fp,read(POLICY))
 except ValueError as error:numeric={'accepted':False,'rejection':str(error)}
 results.append({**row,'elapsed_seconds':f['elapsed'],'measured_seconds':f['measured_seconds'],'frames':f['frames'],
  'full_images':f['full_images'],'verified_buffer_bytes':f['verified_buffer_bytes'],'validation_errors':f['validation_errors'],
  'private_baseline_bytes':h['guard_baseline_private_bytes'],'private_final_bytes':records[-2]['private_bytes'],
  'private_growth_bytes':records[-2]['private_bytes']-h['guard_baseline_private_bytes'],'native_references':refs,
  'numeric_analysis_only':numeric,'memory_acceptance':False})
controls=read(F/'controls/summary.json');assert len(controls['cases'])==9 and controls['overwrite_refused']
for case in controls['cases']:
 records=read_records(F/'controls'/(case['case']+'.jsonl'))
 result=validate(records,case['returncode'],case['returncode'])
 recorded=dict(case['result']);recorded.pop('local_minidump_sha256',None);assert result==recorded
assert not any(p.suffix.lower() in ('.dmp','.exe','.dll','.lib','.pdb') for p in F.rglob('*'))
initial=read(F/'initial-environment-query-failure/resumption.json')
assert initial['status']=='investigation_failed' and not initial['cases'] and 'Read-only environment query failed' in initial['error']
binaries=read(F/'binary-identities.json')
for row in binaries:
 assert not row['redistributed']
 assert [h for p,h in s['frozen_inputs'].items() if p.replace('\\','/').endswith('/'+row['path'])]==[row['sha256']]
symbol=read(F/'setup/symbol-resolution.json');assert symbol['sdk_symbols']>100 and not symbol['symbol_server_used']
for suffix,key in (('gfx_memory_stress_symbols.exe','executable_sha256'),('gfx_memory_stress_symbols.pdb','pdb_sha256')):
 assert [b['sha256'] for b in binaries if b['path'].endswith('/'+suffix)]==[symbol[key]]
compile_text='\n'.join(p.read_text(encoding='utf-16') for p in (F/'setup/out/intel-crash-resume-symbol-sdk').rglob('CL.command.1.tlog'))
assert all(flag in compile_text for flag in ('/Zi','/O2','/MD'))
link_text='\n'.join(p.read_text(encoding='utf-16') for p in (F/'setup/out/intel-crash-resume-capture').rglob('link.command.1.tlog'))
assert 'INTEL-CRASH-RESUME-SYMBOL-SDK\\RELEASE\\GRAPHICSSDKCORE.LIB' in link_text and '/DEBUG' in link_text
full=read(F/'setup/full-python-receipt.json');assert full['successful'] and full['tests']==656
assert not any(full[k] for k in ('errors','failures','skipped','expected_failures','unexpected_successes'))
log=(F/'setup/full-python-unittest.log').read_bytes();assert sha(log)==full['log']['sha256'] and len(log)==full['log']['bytes']
if '--live-inputs' in sys.argv:
 for p,h in s['frozen_inputs'].items():assert sha(pathlib.Path(p).read_bytes())==h,p
 print('PASS live frozen binary/PDB/source/policy inputs unchanged')
summary=dict(schema=1,task='P2-ACC-02',resumed_by_user=True,task_status='todo',gate='required',source_base_commit=s['source_commit'],
 SDK_source_fingerprint=fp,SDK_CXX_or_CMakeLists_modified=False,diagnostic_only=True,memory_acceptance=False,crash_cause_resolved=False,
 result='No AV in this bounded pair; original fault remains unresolved',results=results,selected_driver=before['selected_driver'],
 source_archive=manifest['archive'],native_collector_controls=9,overwrite_refused=True,full_Python_tests=full['tests'],
 symbol_resolution=symbol,evidence=[record(p) for p in sorted(F.rglob('*')) if p.is_file()])
if '--summary' in sys.argv:
 target=ROOT/'benchmarks/reports/2026-10-25-phase2-intel-crash-resumed/summary.json';target.parent.mkdir(parents=True,exist_ok=True)
 target.write_text(json.dumps(summary,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
else:
 expected=read(ROOT/'benchmarks/reports/2026-10-25-phase2-intel-crash-resumed/summary.json');assert expected==summary
print('PASS exact source archive, native references, collector controls, environment and diagnostics; AV cause unresolved')
print(json.dumps([dict(name=r['name'],status=r['status'],elapsed=r['elapsed_seconds'],measured=r['measured_seconds'],frames=r['frames'],growth_MiB=r['private_growth_bytes']/2**20,numeric_pass=r['numeric_analysis_only']['accepted']) for r in results]))
