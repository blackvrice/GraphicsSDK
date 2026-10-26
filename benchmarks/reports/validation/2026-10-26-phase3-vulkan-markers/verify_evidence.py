"""Replay the exact recorded sources, preserving platform byte identities."""
import hashlib,importlib.util,json,pathlib,sys,tempfile,zipfile
sys.dont_write_bytecode=True
ROOT=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'tools/audit_licenses.py').exists())
F=pathlib.Path(__file__).resolve().parent
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def record(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
temporary=[];snapshots={};manifests={}
for kind in ('windows','linux-git'):
 m=read(F/('source-manifest-'+kind+'.json'));manifests[kind]=m;archive=ROOT/m['archive']['path'];assert record(archive)==m['archive']
 temp=tempfile.TemporaryDirectory();temporary.append(temp);target=pathlib.Path(temp.name).resolve();snapshots[kind]=target
 with zipfile.ZipFile(archive) as z:
  assert len(z.namelist())==len(set(z.namelist()))==len(m['files'])+1
  assert json.loads(z.read('source-receipt.json'))=={k:v for k,v in m.items() if k!='archive'}
  for row in m['files']:
   p=target/row['path'];assert p.resolve().is_relative_to(target)
   assert not row['path'].startswith('.deps/') and p.suffix.lower() not in ('.dll','.exe','.lib','.pdb','.dmp','.so','.a','.pyc')
   data=z.read(row['path']);assert len(data)==row['bytes'] and sha(data)==row['sha256'];p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
sys.path.insert(0,str(snapshots['windows']/'tools'))
from audit_licenses import sdk_fingerprint
from run_vulkan_markers import validate_rows
for kind in snapshots:assert sdk_fingerprint(snapshots[kind])==manifests[kind]['SDK_source_fingerprint']
assert manifests['windows']['SDK_source_fingerprint']=='e4afbc3b39066425ce144e8b0171c4e4ac8ba893fc71419bfba2b9804d459634'
assert manifests['linux-git']['SDK_source_fingerprint']=='1c08c10c0ee727495e9a1ffa2eee8ec509de482023ba7941d298091c907b2e4a'
for row in manifests['windows']['files']:
 name=row['path'];assert (snapshots['windows']/name).read_bytes().replace(b'\r\n',b'\n')==(snapshots['linux-git']/name).read_bytes().replace(b'\r\n',b'\n')
results={}
for platform,kind in (('windows','windows'),('linux','linux-git')):
 campaign=read(F/platform/'campaign.json');fp=manifests[kind]['SDK_source_fingerprint']
 assert campaign['status']=='passed' and campaign['inputs_unchanged'] and campaign['serialized'] and campaign['SDK_source_fingerprint']==fp
 assert len(campaign['cases'])==(4 if platform=='windows' else 2)
 require_software=platform=='linux';assert campaign['require_software'] is require_software
 checked=[]
 for case in campaign['cases']:
  name=case['name'];rows=[json.loads(l) for l in (F/platform/(name+'.stdout')).read_text(encoding='utf-8').splitlines()]
  result=validate_rows(rows,fp,name.startswith('validation'),name.endswith('missing-proc'),require_software)
  assert result==case['result'] and case['returncode']==0 and case['environment']['VK_INSTANCE_LAYERS']=='VK_LAYER_GFX_marker'
  if platform=='windows':assert {r['vendor'] for r in rows if r.get('event')=='adapter'}=={0x10de,0x8086}
  checked.append(result)
 for path,h in campaign['frozen_inputs'].items():
  relative=path.replace('\\','/').rsplit('/GraphicsSDK/',1)[-1]
  if (snapshots[kind]/relative).is_file():assert sha((snapshots[kind]/relative).read_bytes())==h
 results[platform]=checked
runs=read(F/'ci/runs.json')
for filename,key in (('linux-artifact.zip','linux_marker'),('linux-first-artifact.zip','first_linux_marker')):
 data=(F/'ci'/filename).read_bytes();assert sha(data)==runs[key]['artifact_sha256']
 with zipfile.ZipFile(F/'ci'/filename) as z:
  folder='linux' if key=='linux_marker' else 'linux-first-failure'
  for name in z.namelist():
   if not name.endswith('/'):assert z.read(name)==(F/folder/name).read_bytes()
failed=read(F/'linux-first-failure/campaign.json');assert failed['status']=='failed' and failed['inputs_unchanged'] and failed['error']=='Missing forwarding, rejection or lifetime checks'
rows=[json.loads(l) for l in (F/'linux-first-failure/normal.stdout').read_text(encoding='utf-8').splitlines()]
assert failed['cases'][0]['returncode']==0 and rows[-1]['assertions']==29 and rows[-1]['native_calls']==6
spec=importlib.util.spec_from_file_location('recorded_failed_checker',F/'ci/failed-tool.py');old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
assert sha((F/'ci/failed-tool.py').read_bytes()) in failed['frozen_inputs'].values()
try:old.validate_rows(rows,failed['SDK_source_fingerprint'],False,False,True)
except ValueError:pass
else:raise AssertionError('Old fixed-30 checker unexpectedly passed')
validate_rows(rows,failed['SDK_source_fingerprint'],False,False,True)
assert read(F/'observer-first-failure/campaign.json')['status']=='failed'
first=(F/'native-first/ctest.log').read_text(encoding='utf-8-sig',errors='replace');last=(F/'native-final/ctest.log').read_text(encoding='utf-8-sig',errors='replace')
assert read(F/'native-first/command.json')['returncode']!=0 and '2 tests failed out of 58' in first
assert 'fence_wait_gpu' in first and 'fence_wait_validation' in first
assert read(F/'native-final/command.json')['returncode']==0 and '100% tests passed, 0 tests failed out of 58' in last
assert all(r['returncode']==0 for r in read(F/'native-isolated/receipt.json'))
full=read(F/'setup/full-python-receipt.json');assert full['successful'] and full['inputs_unchanged'] and full['tests']==667
assert not any(full[k] for k in ('errors','failures','skipped','expected_failures','unexpected_successes'))
log=(F/'setup/full-python-unittest.log').read_bytes();assert len(log)==full['log']['bytes'] and sha(log)==full['log']['sha256']
ci={}
for platform in ('windows','linux'):
 log=read(F/('ci/'+platform+'-build-job.json'))['decoded_log']
 receipt=json.loads(next(l.split('GFX_PYTHON_CONTRACTS_RECEIPT=',1)[1] for l in log.splitlines() if 'GFX_PYTHON_CONTRACTS_RECEIPT=' in l))
 assert receipt['source_commit']==runs['implementation_commit'] and receipt['tests']==667 and receipt['successful'] and receipt['inputs_unchanged']
 assert not any(receipt[k] for k in ('errors','failures','skipped','expected_failures','unexpected_successes'))
 count=37 if platform=='windows' else 11;assert f'100% tests passed, 0 tests failed out of {count}' in log
 ci[platform]=dict(tests=667,native_CTests=count,SDK_source_fingerprint=receipt['SDK_source_fingerprint'])
core_log=read(F/'ci/linux-core-sync-job.json')['decoded_log'];core=json.loads(next(l.split('GFX_LINUX_VALIDATION_RECEIPT=',1)[1] for l in core_log.splitlines() if 'GFX_LINUX_VALIDATION_RECEIPT=' in l))
assert core['source_commit']==runs['broader_linux_core_sync']['commit'] and core['native_SDK_source_fingerprint']==manifests['linux-git']['SDK_source_fingerprint']
assert runs['broader_linux_core_sync']['conclusion']=='success'
binaries=read(F/'binary-identities.json');assert len(binaries)==6 and all(b['redistributed'] is False for b in binaries)
assert binaries[0]['sha256']==binaries[1]['sha256']
assert not any(p.suffix.lower() in ('.dll','.exe','.lib','.pdb','.dmp','.so','.a','.pyc') for p in F.rglob('*') if p.is_file())
summary=dict(schema=1,task='P3-CAP-01',status='done',gate='required',implementation_commit=runs['implementation_commit'],
 Windows_SDK_source_fingerprint=manifests['windows']['SDK_source_fingerprint'],Linux_SDK_source_fingerprint=manifests['linux-git']['SDK_source_fingerprint'],
 public_headers_or_D3D12_implementation_changed=False,results=results,Windows_native_labels=24,Linux_native_labels=6,native_CTests=58,full_Python_tests=667,
 CI=ci,initial_failures_preserved=True,initial_native_fence_exceptions_cause_resolved=False,
 source_archives=[manifests[k]['archive'] for k in ('windows','linux-git')],linux_physical_GPU_acceptance=False,
 evidence=[record(p) for p in sorted(F.rglob('*')) if p.is_file()])
target=ROOT/'benchmarks/reports/2026-10-26-phase3-vulkan-markers/summary.json'
if '--summary' in sys.argv:
 target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
else:assert read(target)==summary
print('PASS actual labels/forwarding and Unsupported contracts; exact Windows/Linux archives; all failures retained; 58 native and 667 Python tests')
