import hashlib,json,pathlib,re,subprocess,sys,zipfile
ROOT=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'tools/audit_licenses.py').is_file())
F=ROOT/'benchmarks/reports/validation/2026-10-25-phase3-linux-hardware-readiness'
SOURCE='0ee90428c04ebd18c2e248dbb1ea1fdc5c6ab818';RUN=40197045560
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def record(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
git=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false']
run=read(F/'run-status.json');jobs=read(F/'guard-job-status.json')['jobs'];a=read(F/'artifact-metadata.json')['artifacts']
assert run['id']==RUN and run['head_sha']==SOURCE and run['conclusion']=='success'
assert len(jobs)==1 and jobs[0]['id']==121180037437 and jobs[0]['conclusion']=='success'
assert len(a)==1 and a[0]['workflow_run']['head_sha']==SOURCE and a[0]['workflow_run']['id']==RUN
assert a[0]['digest']=='sha256:'+sha((F/'artifact.zip').read_bytes())
native=F/'native'
with zipfile.ZipFile(F/'artifact.zip') as z:
 assert len(z.infolist())<100 and len(z.namelist())==len(set(z.namelist())) and sum(i.file_size for i in z.infolist())<10000000
 for i in z.infolist():
  assert (native/i.filename).resolve().is_relative_to(native.resolve()) and i.file_size<2000000 and (i.external_attr>>16)&0o170000!=0o120000
  assert z.read(i)==(native/i.filename).read_bytes()
r=read(native/'preflight.json')
assert r['source_commit']==SOURCE and not r['physical_preflight_passed'] and not r['hardware_GPU_accepted'] and not (native/'receipt.json').exists()
assert r['rejection']=='Physical GPU required; software/CPU or empty candidate set rejected'
identity=r['identity'];assert identity['software_adapters']==1 and identity['hardware_adapters']==0 and len(identity['adapters'])==1 and identity['adapters'][0]['device_type']==4
assert identity['adapters'][0]['id']=='5892941257450743403' and type(identity['adapters'][0]['id']) is str
assert read(native/'selection-1.stdout')==read(native/'selection-2.stdout')==identity
assert r['native_SDK_source_fingerprint']=='ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830'
assert r['SDK_library']['sha256']=='c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32' and r['SDK_library']['bytes']==413918
assert {pathlib.PurePosixPath(x['path']).name for x in r['runtime_libraries']}=={'libvulkan.so.1.3.275','libvulkan_lvp.so'}
for i in (1,2):
 debug=(native/('selection-'+str(i)+'.stderr')).read_text(encoding='utf-8-sig')
 assert 'calling init:' in debug and 'libvulkan_lvp.so' in debug
commands=read(native/'commands.json');assert all(c['returncode']==c['expected']==0 for c in commands)
assert next(c for c in commands if c['name']=='build-sdk')['argv'][-2:]==['--target','GraphicsSDKCore']
assert '-DGFX_BUILD_TESTS=OFF' not in next(c for c in commands if c['name']=='configure-sdk')['argv']
log=(F/'guard-job.log').read_text(encoding='utf-8-sig')
assert 'Ran 14 tests' in log and 'PASS actual software ICD rejected; physical GPU acceptance remains pending' in log and r['rejection'] in log
assert 'GFX_VULKAN_VALIDATION_TESTS requires GFX_BUILD_TESTS' in (F/'failed-a8668d9-job.log').read_text(encoding='utf-8-sig')
assert read(F/'manual-run-status.json')['total_count']==0
env=read(F/'environment/host-inventory.json');assert not env['SshConfigExists'] and not env['LinuxGpuTargetProvided'] and not env['PhysicalGpuAcceptance']
for row in read(F/'environment/commands.json'):
 for name in ('stdout','stderr'):
  data=(F/'environment'/(row['name']+'.'+name)).read_bytes()
  assert row[name]==data.decode('utf-16le' if b'\0' in data else 'utf-8',errors='replace')
rows=read(F/'environment/commands.json');assert rows[0]['returncode']==4294967295 and rows[1]['returncode']==0
common_jobs=read(F/'common-job-status.json')['jobs'];assert len(common_jobs)==2 and all(j['conclusion']=='success' for j in common_jobs)
receipts={}
for platform in ('linux','windows'):
 x=read(F/'common'/(platform+'-receipt.json'));receipts[platform]=x
 assert x['source_commit']==SOURCE and x['github_run_id']=='40197045611' and x['tests']==654 and x['successful'] and x['inputs_unchanged']
 assert not x['tracked_status_before'] and not x['tracked_status_after'] and not any(x[k] for k in ('failures','errors','skipped','expected_failures','unexpected_successes'))
 text=(F/('common-'+platform+'-job.log')).read_text(encoding='utf-8-sig')
 assert [json.loads(l.split('GFX_PYTHON_CONTRACTS_RECEIPT=',1)[1]) for l in text.splitlines() if 'GFX_PYTHON_CONTRACTS_RECEIPT=' in l]==[x]
 before=read(ROOT/'benchmarks/reports/validation/2026-10-25-phase3-python-ci/native'/platform/'receipt.json')
 assert len(set(x['test_ids']))==654 and set(before['test_ids'])<=set(x['test_ids'])
 added=set(x['test_ids'])-set(before['test_ids']);assert len(added)==14 and all(k.startswith('test_linux_hardware.') for k in added)
assert receipts['linux']['test_ids']==receipts['windows']['test_ids']
compile_receipts=[json.loads(l.split('GFX_LINUX_COMPILE_RECEIPT=',1)[1]) for l in (F/'compile-job.log').read_text(encoding='utf-8-sig').splitlines() if 'GFX_LINUX_COMPILE_RECEIPT=' in l]
assert len(compile_receipts)==1 and compile_receipts[0]['source_commit']=='a8668d9a26ce4ad4780f9549493c7abaedfa3fac'
assert all(p['CPU_tests']==11 and p['GPU_tests']==0 for p in compile_receipts[0]['profiles'])
assert not subprocess.check_output(git+['diff','f0ece58',SOURCE,'--name-only','--','include','src','CMakeLists.txt','benchmarks','licenses','tools/memory_stress_policy.json'],cwd=ROOT).strip()
source_inputs=[]
for name in ('tools/linux_hardware.py','tests/test_linux_hardware.py','.github/workflows/linux-hardware-guards.yml','.github/workflows/linux-hardware-acceptance.yml'):
 data=subprocess.check_output(git+['show',SOURCE+':'+name],cwd=ROOT);assert data==(ROOT/name).read_bytes();source_inputs.append(dict(path=name,bytes=len(data),sha256=sha(data)))
workflow=(ROOT/'.github/workflows/linux-hardware-acceptance.yml').read_text(encoding='utf-8')
assert 'workflow_dispatch:' in workflow and 'self-hosted, Linux, X64, graphicssdk-gpu' in workflow and '  push:' not in workflow and '  pull_request:' not in workflow
print('PASS readiness only: real software ICD explicitly rejected; physical acceptance false/absent; 14 gate controls and 654 Python tests per platform passed; manual hardware run count 0')
if '--summary' in sys.argv:
 s=dict(schema_version=1,task='P3-LNX-15',task_status='todo',source_commit=SOURCE,scope='Execution readiness and actual software rejection only; physical positive path unexecuted',hardware_GPU_accepted=False,physical_preflight_passed=False,physical_positive_path_executed=False,hardware_suite_executions=0,required_environment='Reachable physical Linux GPU SSH target or registered GPU runner, physical ICD path and exact UUID set',software_rejection_CI=dict(id=RUN,job_id=121180037437,url=run['html_url'],conclusion='success',expected_preflight_exit=1),software_observation=identity,preflight=r,synthetic_gate_tests=14,manual_hardware_runs_observed=0,common_CI=dict(id=40197045611,Python_tests_per_platform=654,errors=0,failures=0,skipped=0,all_previous_640_tests_retained=True),native_compile_CI=dict(id=40196873742,source_commit='a8668d9a26ce4ad4780f9549493c7abaedfa3fac',CPU_tests=22,GPU_tests=0),SDK_CXX_or_CMakeLists_modified=False,source_inputs=source_inputs,verification=dict(result='pass',meaning='Readiness/negative gate verified; this is not P3-LNX-15 hardware acceptance'),evidence=[record(p) for p in sorted(F.rglob('*')) if p.is_file()])
 dest=ROOT/'benchmarks/reports/2026-10-25-phase3-linux-hardware-readiness/summary.json';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
