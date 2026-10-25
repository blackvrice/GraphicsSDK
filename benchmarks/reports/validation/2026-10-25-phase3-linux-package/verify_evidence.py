import hashlib,json,pathlib,posixpath,re,shlex,subprocess,sys,zipfile
ROOT=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'tools/audit_licenses.py').is_file())
sys.path.insert(0,str(ROOT/'tools'))
from audit_licenses import sdk_fingerprint
from run_linux_validation import SUITES,EXPECTED
F=ROOT/'benchmarks/reports/validation/2026-10-25-phase3-linux-package'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def record(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
context=read(F/'ci-context.json');SOURCE=context['source'];RUN=context['run'];JOB=context['job']
metadata=read(F/'artifact-metadata.json')['artifacts'];assert len(metadata)==1
a=metadata[0];assert a['workflow_run']['head_sha']==SOURCE and a['workflow_run']['id']==RUN
assert a['digest']=='sha256:'+sha((F/'artifact.zip').read_bytes())
native=F/'native'
if not native.exists():
 with zipfile.ZipFile(F/'artifact.zip') as z:
  assert len(z.infolist())<1000 and sum(i.file_size for i in z.infolist())<200000000
  for i in z.infolist():
   assert (native/i.filename).resolve().is_relative_to(native.resolve()) and i.file_size<50000000
   assert (i.external_attr>>16)&0o170000!=0o120000
  z.extractall(native)
r=read(native/'receipt.json');run=read(F/'run-status.json');jobs=read(F/'job-status.json')['jobs']
assert r['task']=='P3-LNX-12' and r['source_commit']==SOURCE and r['github_run_id']==str(RUN)
assert run['id']==RUN and run['head_sha']==SOURCE and run['conclusion']=='success'
assert len(jobs)==1 and jobs[0]['id']==JOB and jobs[0]['conclusion']=='success'
assert 'PASS relocated external consumers=9 Vulkan_assertions=2666 payload_files=20' in (F/'job.log').read_text(encoding='utf-8-sig')
assert 'Ran 13 tests' in (F/'job.log').read_text(encoding='utf-8-sig')
for row in r['diagnostics']+[r['package'],r['notice']]:
 prefix='out/linux-package-evidence/';assert row['path'].startswith(prefix)
 p=native/row['path'][len(prefix):];assert p.resolve().is_relative_to(native.resolve())
 assert p.stat().st_size==row['bytes'] and sha(p.read_bytes())==row['sha256'],row['path']
git=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false']
def blob(name):return subprocess.check_output(git+['show',SOURCE+':'+name],cwd=ROOT)
for row in r['inputs']:
 b=blob(row['path']);assert len(b)==row['bytes'] and sha(b)==row['sha256'],row['path']
assert not subprocess.check_output(git+['diff','ff6631d',SOURCE,'--name-only','--','include','src','CMakeLists.txt'],cwd=ROOT).strip()
paths=subprocess.check_output(git+['ls-tree','-r','--name-only',SOURCE],cwd=ROOT).decode().splitlines()
sdk=[p for p in paths if p=='CMakeLists.txt' or p.startswith('include/') and p.endswith('.hpp') or p.startswith('src/') and p.endswith(('.hpp','.cpp'))]
assert len(sdk)==27
assert sha(''.join(p+'|'+sha(blob(p))+'\n' for p in sorted(sdk)).encode())==r['native_SDK_source_fingerprint']=='ca69fbe86eb5fb231503e070927322b95a80fbde10a1a951133f640b0dec2830'
assert sdk_fingerprint(ROOT) in ('2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401',r['native_SDK_source_fingerprint'])
assert sha((ROOT/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
assert r['SDK_library']==dict(bytes=413918,sha256='c6870603fd53a581c3ac6a4fcb70e6a58220d0ff0433f17f51f2e8a045e38b32')
assert r['original_build_absent'] and r['original_install_absent'] and r['external_project'].startswith('/tmp/')
assert not r['hardware_GPU_accepted'] and not r['required_validation_this_run']
assert len(r['payload'])==20
payload={x['path']:x for x in r['payload']}
with zipfile.ZipFile(native/'GraphicsSDK-linux-verification.zip') as z:
 assert set(z.namelist())==set(payload) and len(z.namelist())==20
 for name,row in payload.items():
  b=z.read(name);assert len(b)==row['bytes'] and sha(b)==row['sha256']
  if name.startswith('include/'):assert b==blob(name)
  if name=='share/GraphicsSDK/LICENSE':assert b==blob('LICENSE')
  if name=='share/GraphicsSDK/THIRD_PARTY_NOTICES.txt':assert b==(native/'THIRD_PARTY_NOTICES.txt').read_bytes()
  if name.endswith('.cmake'):assert b'/home/runner/work/' not in b and b'/tmp/' not in b
 archive=z.read('lib/libGraphicsSDKCore.a')
 assert sha(archive)==r['SDK_library']['sha256'] and archive.startswith(b'!<arch>\n')
 members={};offset=8;names=b''
 while offset<len(archive):
  h=archive[offset:offset+60];assert h[-2:]==b'`\n';size=int(h[48:58]);name=h[:16].decode().strip()
  data=archive[offset+60:offset+60+size];offset+=60+size+(size%2)
  if name=='//':names=data
  elif name not in ('/','/SYM64/'):
   if name.startswith('/'):
    start=int(name[1:]);name=names[start:names.index(b'/\n',start)].decode()
   else:name=name.rstrip('/')
   members[name]=sha(data)
 assert members==r['archive_members'] and len(members)==6
policy=json.loads(blob('licenses/linux-vulkan-policy.json'));assert r['reviewed_headers']==policy['dependency']
vendor_blobs={}
for x in policy['dependency']['reviewed_headers']:
 b=subprocess.check_output(git+['-C',str(ROOT/'.deps/Vulkan-Headers'),'show',policy['dependency']['commit']+':'+x['path']])
 assert sha(b)==x['sha256'] and sha(b.replace(b'\n',b'\r\n'))==x['Windows_CRLF_sha256']
 vendor_blobs[x['path']]=b
apache=subprocess.check_output(git+['-C',str(ROOT/'.deps/Vulkan-Headers'),'show',policy['dependency']['commit']+':LICENSES/Apache-2.0.txt'])
assert r['apache_license']==dict(bytes=len(apache),sha256=sha(apache))
notices=['GraphicsSDK installed static SDK verification payload\n','GraphicsSDK — MIT\n'+blob('LICENSE').decode(),'Compiled using reviewed Vulkan-Headers. Selected license: Apache-2.0.\nThe external Vulkan loader, ICD, validation layer and system/toolchain files are not bundled.\n']
for name,b in sorted(vendor_blobs.items()):
 text=b.decode();start=text.find('/*');end=text.find('*/',start);assert 'Copyright' in text[start:end] and 'SPDX-License-Identifier: Apache-2.0' in text
 notices.append(name+'\n'+text[start:end+2])
notices.append('Apache License 2.0 — full text\n'+apache.decode())
assert ('\n\n'.join(notices)+'\n').encode()==(native/'THIRD_PARTY_NOTICES.txt').read_bytes()
vendor={str(pathlib.PurePosixPath('/home/runner/work/GraphicsSDK/GraphicsSDK/.deps/Vulkan-Headers')/x['path']):x for x in policy['dependency']['reviewed_headers']}
inputs={x['path']:x for x in r['actual_inputs']}
assert {p for p,x in inputs.items() if x['category']=='vendor'}==set(vendor)
for p,x in inputs.items():
 if x['category']=='vendor':assert x['sha256']==vendor[p]['sha256']
 elif x['category']=='own':assert x['sha256']==sha(blob(str(pathlib.PurePosixPath(p).relative_to('/home/runner/work/GraphicsSDK/GraphicsSDK'))))
 elif x['category']=='copied':assert x['sha256']==r['external_client_copies'][p]['sha256']==sha(blob(r['external_client_copies'][p]['source']))
 elif x['category']=='installed':assert x['sha256']==payload[str(pathlib.PurePosixPath(p).relative_to(r['relocated_prefix']))]['sha256']
 elif x['category']=='installed_archive':assert x['sha256']==r['SDK_library']['sha256']
 elif x['category'].startswith('system'):assert p.startswith(('/usr/include/','/usr/lib/','/lib/'))
 else:assert x['category']=='client_object'
assert len(r['captured_commands']['sdk'])==6 and len(r['captured_commands']['consumer'])==18
assert sorted(r['archive_members'].values())==sorted(x['output_sha256'] for x in r['captured_commands']['sdk'])
for kind,rows in r['captured_commands'].items():
 for row in rows:
  assert row['returncode']==0
  if row['mode']=='compile':
   assert '-MD' in row['argv'] and '-MMD' not in row['argv']
   assert all(inputs[p]['category'] in ({'own','vendor','system'} if kind=='sdk' else {'copied','installed','system'}) for p in row['inputs'])
  else:assert '-Wl,-t,-t' in row['argv'] and any(v.startswith('-Wl,-Map=') for v in row['argv'])
assert len(list((native/'consumer-capture').glob('*.map')))==9
for kind in ('sdk','consumer'):
 raw=[]
 for path in sorted((native/(kind+'-capture')).glob('*.json')):
  row=read(path);enriched=next(x for x in r['captured_commands'][kind] if x['argv']==row['argv'] and x['cwd']==row['cwd'])
  assert row=={k:v for k,v in enriched.items() if k!='inputs'}
  if row['mode']=='compile':
   dep=path.with_suffix('.dep');assert sha(dep.read_bytes())==row['dependency_sha256']
   text=dep.read_text().replace('\\\n','');tokens=shlex.split(text.split(':',1)[1])
   lexical={posixpath.normpath(posixpath.join(row['cwd'],p)) for p in tokens}
   assert lexical==set(enriched['inputs']),lexical^set(enriched['inputs'])
  else:
   actual=set();members=0
   for line in path.with_suffix('.stdout').read_text().splitlines():
    if not line.strip():continue
    m=re.fullmatch(r'\(([^)]+)\)(.+)',line);members+=bool(m)
    p=posixpath.normpath(posixpath.join(row['cwd'],m[1] if m else line))
    # GCC toolchain links use versioned symlinks; all exact paths were hashed by the native audit.
    actual.add(p)
   assert r['relocated_prefix']+'/lib/libGraphicsSDKCore.a' in actual and members>0
   assert 'libGraphicsSDKCore.a(' in path.with_suffix('.map').read_text()
 raw.append(row)
owners=read(native/'system-input-owners.json')
assert {x['path'] for x in owners}=={p for p,x in inputs.items() if x['category'].startswith('system')}
assert all(x['attempts'][-1]['returncode']==0 and x['attempts'][-1]['stdout'].strip() for x in owners)
assert all(x['returncode']==0 for x in read(native/'commands.json'))
assert len(r['results'])==8 and sum(x['assertions'] for x in r['results'])==2666
for row in r['results']:
 name=row['suite'];text=(native/('installed-'+name+'.stdout')).read_text();debug=(native/('installed-'+name+'.stderr')).read_text()
 assert re.findall(SUITES[name]['pattern'],text)==[tuple(str(row[k]) for k in SUITES[name]['fields'])]
 assert row['adapters']==1 and all(row[k]==v for k,v in EXPECTED[name].items())
 assert 'source='+r['native_SDK_source_fingerprint']+' validation=0' in text
 assert 'llvmpipe (' in text and 'libvulkan_lvp.so' in debug and 'libVkLayer_khronos_validation.so' not in debug
 assert not re.search(r'NEEDED.*(?:vulkan|VkLayer|lvp)',(native/('elf-'+name+'.stdout')).read_text(),re.I)
assert 'software=1' in (native/'installed-buffer.stdout').read_text()
assert 'GraphicsSDK 0.0.1' in (native/'installed-cpu.stdout').read_text()
assert sha((F/'failed-12781ea-artifact.zip').read_bytes())==read(F/'failed-12781ea-artifact-metadata.json')['artifacts'][0]['digest'].removeprefix('sha256:')
for name,count in (('portable',92),('windows',99)):
 text=(F/('common-'+name+'-job.log')).read_text(encoding='utf-8-sig')
 prior=(F.parent/'2026-10-25-phase3-linux-validation'/('common-'+name+'-job.log')).read_text(encoding='utf-8-sig')
 errors=re.findall(r'ERROR: ([^\r\n]+)',text);old=re.findall(r'ERROR: ([^\r\n]+)',prior)
 assert len(errors)==len(old)==count and sorted(errors)==sorted(old)
 assert 'Ran 627 tests' in text and 'FAILED (errors='+str(count)+')' in text
 assert len(re.findall(r'test_\w+ \(test_linux_package_audit[^\r\n]*\.\.\. ok',text))==13
compile_jobs=read(F/'compile-job-status.json')['jobs'];assert len(compile_jobs)==1 and compile_jobs[0]['conclusion']=='success'
receipts=[json.loads(l.split('GFX_LINUX_COMPILE_RECEIPT=',1)[1]) for l in (F/'compile-job.log').read_text(encoding='utf-8-sig').splitlines() if 'GFX_LINUX_COMPILE_RECEIPT=' in l]
assert len(receipts)==1 and receipts[0]['source_commit']=='12781ea0ccea5da7a0f4343243c700334932b031' and receipts[0]['native_SDK_source_fingerprint']==r['native_SDK_source_fingerprint']
assert len(receipts[0]['profiles'])==2 and all(x['CPU_tests']==11 and x['GPU_tests']==0 for x in receipts[0]['profiles'])
print('PASS native Linux package provenance, actual GNU closure, relocated consumers=9 / Vulkan assertions=2666 / payload=20; unchanged SDK identities')
if '--summary' in sys.argv:
 s=dict(schema_version=1,task='P3-LNX-12',source_commit=SOURCE,workflow_run=dict(id=RUN,job_id=JOB,url=run['html_url'],conclusion='success'),native_SDK_source_fingerprint=r['native_SDK_source_fingerprint'],Windows_SDK_source_fingerprint=sdk_fingerprint(ROOT),SDK_library=r['SDK_library'],platform=r['platform'],results=r['results'],external_consumers=9,Vulkan_assertions=2666,negative_audit_unit_tests=13,SDK_compile_units=6,consumer_compile_units=9,consumer_links=9,payload_files=20,actual_input_categories={k:sum(x['category']==k for x in inputs.values()) for k in sorted({x['category'] for x in inputs.values()})},notice_added_by=r['notice_added_by'],hardware_GPU_accepted=False,required_validation_this_run=False,SDK_CXX_or_CMakeLists_modified=False,verification=dict(result='pass',checks=['GitHub source/run/job/artifact digest verified','All receipt diagnostic and tested Git input hashes verified','Actual GNU SDK/consumer dependency closure and link trace/map inventory verified','Installed Core archive matches six compiled objects and immutable native baseline','Exact 20-file ZIP matches installed header/library/CMake/license inventory','Nine relocated external consumers passed; eight software Vulkan suites / 2666 assertions','13 fail-closed parser, contamination, notice, header, archive and payload controls']),evidence=[record(p) for p in sorted(F.rglob('*')) if p.is_file()])
 s['Windows_SDK_source_fingerprint']='2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401'
 s['native_compile_CI']=dict(id=40187584233,source_commit='12781ea0ccea5da7a0f4343243c700334932b031',CPU_tests=22,GPU_tests=0,conclusion='success')
 s['common_CI']=dict(id=40187751574,Python_tests=627,Linux_errors=92,Windows_errors=99,error_test_identities_unchanged=True,new_audit_tests_passed_on_both_platforms=13,followup='P3-LNX-14')
 dest=ROOT/'benchmarks/reports/2026-10-25-phase3-linux-package/summary.json';dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
