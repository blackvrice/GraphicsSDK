import collections,hashlib,json,pathlib,re,subprocess,sys,zipfile
ROOT=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'tools/audit_licenses.py').is_file())
F=ROOT/'benchmarks/reports/validation/2026-10-25-phase3-python-ci'
SOURCE='35d0808259ce87bf6d3fed2649c8b79e062cb83c';RUN=40194816503
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def record(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
git=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false']
run=read(F/'run-status.json');jobs=read(F/'job-status.json')['jobs'];artifacts=read(F/'artifact-metadata.json')['artifacts']
assert run['head_sha']==SOURCE and run['id']==RUN and run['conclusion']=='success'
assert len(jobs)==2 and {j['name'] for j in jobs}=={'windows','portable-core'} and all(j['conclusion']=='success' for j in jobs)
assert len(artifacts)==2
receipts={};identities={}
for platform,job_name in [('linux','portable-core'),('windows','windows')]:
 a=next(a for a in artifacts if a['name']=='python-contracts-'+platform+'-'+SOURCE)
 assert a['workflow_run']['id']==RUN and a['workflow_run']['head_sha']==SOURCE
 archive=F/('artifact-'+platform+'.zip');assert a['digest']=='sha256:'+sha(archive.read_bytes())
 native=F/'native'/platform
 with zipfile.ZipFile(archive) as z:
  assert len(z.infolist())==2 and set(z.namelist())=={'receipt.json','unittest.log'}
  for i in z.infolist():
   assert i.file_size<2000000 and (i.external_attr>>16)&0o170000!=0o120000
   assert (native/i.filename).resolve().is_relative_to(native.resolve())
   if not native.exists():continue
   assert z.read(i)==(native/i.filename).read_bytes()
  if not native.exists():z.extractall(native)
 r=read(native/'receipt.json');receipts[platform]=r
 assert r['task']=='P3-LNX-14' and r['source_commit']==SOURCE and r['github_run_id']==str(RUN)
 assert r['platform']['system']==('Windows' if platform=='windows' else 'Linux')
 assert r['successful'] and r['inputs_unchanged'] and not r['tracked_status_before'] and not r['tracked_status_after']
 assert r['tests']==640 and r['subtests']==540 and len(r['test_ids'])==len(set(r['test_ids']))==640
 assert not any(r[k] for k in ('errors','failures','skipped','expected_failures','unexpected_successes'))
 assert not r['native_GPU_executed_by_this_Python_suite'] and not r['memory_or_Intel_investigation_resumed']
 b=(native/'unittest.log').read_bytes();assert len(b)==r['log']['bytes'] and sha(b)==r['log']['sha256'] and b'Ran 640 tests' in b and b'\nOK\n' in b
 log=(F/(platform+'-job.log')).read_text(encoding='utf-8-sig')
 logged=[json.loads(l.split('GFX_PYTHON_CONTRACTS_RECEIPT=',1)[1]) for l in log.splitlines() if 'GFX_PYTHON_CONTRACTS_RECEIPT=' in l]
 assert logged==[r]
 assert '100% tests passed, 0 tests failed out of '+('37' if platform=='windows' else '11') in log
 assert len(r['inputs'])==174 and len({x['path'] for x in r['inputs']})==174
 names=[x['path'] for x in r['inputs']]
 raw=subprocess.check_output(git+['cat-file','--batch'],input=''.join(SOURCE+':'+p+'\n' for p in names).encode(),cwd=ROOT)
 attrs=subprocess.check_output(git+['check-attr','--cached','-z','text','eol','--stdin'],input=''.join(p+'\0' for p in names).encode(),cwd=ROOT).decode().split('\0')
 attributes={}
 for i in range(0,len(attrs)-1,3):attributes.setdefault(attrs[i],{})[attrs[i+1]]=attrs[i+2]
 offset=0;kinds=collections.Counter();sdk=[]
 for row in r['inputs']:
  end=raw.index(b'\n',offset);header=raw[offset:end].split();assert len(header)==3 and header[1]==b'blob';size=int(header[2]);b=raw[end+1:end+1+size];offset=end+size+2
  if row['sha256']==sha(b):kind='Git_blob_bytes'
  else:
   attr=attributes[row['path']]
   assert platform=='windows' and attr['text']!='unset' and attr['eol']!='lf',(platform,row['path'],attr)
   crlf=b.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
   assert row['sha256']==sha(crlf) and row['bytes']==len(crlf),row['path']
   kind='Windows_CRLF_checkout'
  if kind=='Git_blob_bytes':assert row['bytes']==len(b)
  kinds[kind]+=1
  p=row['path']
  if p=='CMakeLists.txt' or p.startswith('include/') and p.endswith('.hpp') or p.startswith('src/') and p.endswith(('.cpp','.hpp')):sdk.append(row)
 assert len(sdk)==27
 assert sha(''.join(x['path']+'|'+x['sha256']+'\n' for x in sorted(sdk,key=lambda x:x['path'])).encode())==r['SDK_source_fingerprint']
 identities[platform]=dict(SDK_source_fingerprint=r['SDK_source_fingerprint'],inputs=174,checkout_byte_categories=dict(kinds),tests=640,subtests=540,skipped=0)
assert receipts['linux']['test_ids']==receipts['windows']['test_ids']
assert receipts['linux']['SDK_source_fingerprint']=='ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830'
assert receipts['windows']['SDK_source_fingerprint']=='09ce93c4405f86cd69b9f6ac1484041bfe42466cb40d8f05767d987fc86a2da5'
assert not subprocess.check_output(git+['diff','ab65d2d',SOURCE,'--name-only','--','include','src','CMakeLists.txt','benchmarks','licenses','tools/memory_stress_policy.json'],cwd=ROOT).strip()
assert not subprocess.check_output(git+['diff','ab65d2d',SOURCE,'--name-only','--','tools/run_vulkan_loader_fault.py','tools/run_vulkan_pool_churn.py','tools/run_vulkan_pool_rebase_fault.py','tools/run_vulkan_buffer_pool_fault.py'],cwd=ROOT).strip()
before=read(F/'before-evidence.json');before_ids={}
for row in before['runs']:
 b=(ROOT/row['path']).read_bytes();assert len(b)==row['bytes'] and sha(b)==row['sha256']
 # Historical evidence must also match the pre-fix Git tree, byte for byte.
 old=subprocess.check_output(git+['show','ab65d2d:'+row['path']],cwd=ROOT);assert old==b
 text=b.decode('utf-8-sig');assert 'Ran '+str(row['tests'])+' tests' in text and 'FAILED (errors='+str(row['errors'])+')' in text
 assert len(re.findall(r'ERROR: ',text))==row['errors']
 ids=set(re.findall(r'\((test_\w+\.\w+\.test_\w+)\) \.\.\.',text));assert len(ids)==row['tests'],(len(ids),row)
 before_ids[row['tests'],row['platform']]=ids
assert before_ids[627,'windows']==before_ids[627,'portable']
assert before_ids[614,'windows']==before_ids[614,'portable']
assert before_ids[614,'windows']<=before_ids[627,'windows']<=set(receipts['windows']['test_ids'])
added=set(receipts['windows']['test_ids'])-before_ids[627,'windows'];assert len(added)==13
assert sum(x.startswith('test_recorded_sources.') for x in added)==10
assert sum(x.startswith('test_fixed_memory.') for x in added)==2
assert sum(x.startswith('test_memory_campaign.') for x in added)==1
lock=read(ROOT/'tests/recorded_sources.json');snapshots=[]
for key,entry in lock['sources'].items():
 p=ROOT/entry['archive']['path'];assert sha(p.read_bytes())==entry['archive']['sha256']
 assert sha(''.join(x['path']+'|'+x['sha256']+'\n' for x in entry['files']).encode())==entry['source_fingerprint']
 with zipfile.ZipFile(p) as z:
  for row in entry['files']:assert sha(z.read(row['path']))==row['sha256']
 snapshots.append(dict(name=key,archive=record(p),source_fingerprint=entry['source_fingerprint'],source_files=len(entry['files'])))
compile_jobs=read(F/'compile-job-status.json')['jobs'];assert len(compile_jobs)==1 and compile_jobs[0]['conclusion']=='success'
compiled=[json.loads(l.split('GFX_LINUX_COMPILE_RECEIPT=',1)[1]) for l in (F/'compile-job.log').read_text(encoding='utf-8-sig').splitlines() if 'GFX_LINUX_COMPILE_RECEIPT=' in l]
assert len(compiled)==1 and compiled[0]['source_commit']==SOURCE and compiled[0]['native_SDK_source_fingerprint']==receipts['linux']['SDK_source_fingerprint']
assert len(compiled[0]['profiles'])==2 and all(p['CPU_tests']==11 and p['GPU_tests']==0 for p in compiled[0]['profiles'])
sys.path.insert(0,str(ROOT/'tools'));from audit_licenses import sdk_fingerprint
assert sdk_fingerprint(ROOT) in ('2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401',*[r['SDK_source_fingerprint'] for r in receipts.values()])
assert sha((ROOT/'tools/memory_stress_policy.json').read_bytes())==sha(subprocess.check_output(git+['show','ab65d2d:tools/memory_stress_policy.json'],cwd=ROOT)) or sha((ROOT/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
print('PASS Windows/Linux fresh CI 640 tests each, 540 subtests each, errors/failures/skips=0; all 627 old tests retained; 174 inputs each verified; historical evidence and SDK contracts preserved')
if '--summary' in sys.argv:
 s=dict(schema_version=1,task='P3-LNX-14',source_commit=SOURCE,workflow_run=dict(id=RUN,url=run['html_url'],conclusion='success'),platforms=identities,total_Python_tests=1280,total_subtests=1080,errors=0,failures=0,skipped=0,new_regression_tests=sorted(added),all_prior_627_tests_preserved=True,SDK_CXX_or_CMakeLists_modified=False,live_source_validators_modified=False,historical_replay_sources=snapshots,before=before['runs'],native_compile_CI=dict(id=40194816496,CPU_tests=22,GPU_tests=0,conclusion='success'),native_hardware_GPU_accepted=False,memory_or_Intel_investigation_resumed=False,verification=dict(result='pass',checks=['Successful fresh Windows/Linux full CI, logged/native receipts and artifact SHA-256 verified','640 test methods and 540 subtests on each platform, zero exclusions/skips/errors/failures','All 627 prior test IDs retained, 13 targeted regression tests added','174 checked inputs on each platform match exact Git bytes or explicit Windows checkout line endings','Per-platform SDK fingerprints reconstructed from 27 actual file hashes without changing historical identity','Three pinned historical source ZIPs and 32 exact source entries verified','614/627 baseline logs unchanged, SDK source/old benchmark data/runtime validators preserved','Windows CTest 37, Linux portable CTest 11, native Vulkan compile CPU 22 passed']),evidence=[record(p) for p in sorted(F.rglob('*')) if p.is_file()])
 dest=ROOT/'benchmarks/reports/2026-10-25-phase3-python-ci/summary.json';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
