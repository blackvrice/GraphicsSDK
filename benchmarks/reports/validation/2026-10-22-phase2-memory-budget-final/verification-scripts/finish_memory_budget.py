import pathlib,json,hashlib,subprocess,sys,zipfile,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-memory-budget-final';pilot=root/'benchmarks/reports/validation/2026-10-22-phase2-memory-budget-pilot';report=root/'benchmarks/reports/2026-10-22-phase2-memory-budget'
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint,record_file,audit_package
from analyze_memory_stress import analyze,read_records
from run_adapter_validation import validate_list,validate_selection
from run_presentation_validation import validate_events,validate_pacing
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def record(p):return record_file(root,p)
def txt(p):return p.read_text(encoding='utf-8',errors='replace')
def write(p,value):p.write_bytes((json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode())
fp=sdk_fingerprint(root);assert fp=='9f3235c37c9a907b1156fc5d3577f6d5e867529a78d1147800c96b1b3e00c7f3'
before=load(v/'inputs-before.json');assert before==load(v/'inputs-after.json') and before['SDK_source_fingerprint']==fp
for path,h in before['inputs'].items():assert digest(root/path)==h,path
commands=load(v/'commands.json');assert len(commands)==25 and all(x['returncode']==0 and x['attempt']==1 for x in commands)
assert all(x['returncode']==0 for x in load(pilot/'commands.json'))
for name,count in [('core',28),('no-backend',6),('benchmark',72),('presentation',7)]:
 t=txt(v/(name+'-ctest-1.log'));assert f'100% tests passed, 0 tests failed out of {count}' in t and 'Skipped' not in t
assert 'Ran 147 tests' in txt(v/'python-tests-1.log') and '\nOK' in txt(v/'python-tests-1.log')
events=[]
for line in txt(v/'core-native.log').splitlines():
 if line.startswith('{"event":"budget'):
  events.append(json.loads(line))
headers=[r for r in events if r['event']=='budget_test'];results=[r for r in events if r['event']=='budget_result'];snapshots=[r for r in events if r['event']=='budget']
assert len(headers)==len(results)==2 and all(r['sdk_sha256']==fp for r in headers) and {r['validation'] for r in headers}=={False,True}
assert len(snapshots)==48 and all(r['native_matched'] for r in snapshots)
assert all(r['adapters']==3 and r['observations']==24 and r['positive_controls']==2 and r['recoveries']==3 and r['verified_buffer_bytes']==196608 and r['passed'] for r in results)
assert not any(r['event']=='budget_unavailable' for r in events)
for name in ('consumer-budget','consumer-budget-validation'):
 text=txt(v/(name+'-1.log'));assert text.count('installed_budget_id=')==12 and 'PASS installed budget consumer: 3 adapters, 12 snapshots, same-ID recovery' in text
default=load(v/'presentation-campaign/summary.json');explicit=load(v/'adapter-campaign/summary.json')
assert default['sdk_build_source_sha256']==explicit['sdk_build_source_sha256']==fp
entries=validate_list([json.loads(s) for s in txt(v/'adapter-campaign/adapters.jsonl').splitlines()],fp);ids={a['adapter_id'] for a in entries}
assert len(entries)==3 and {r['adapter_id'] for r in snapshots}==ids and len(explicit['negative_cases'])==8
for folder in ('adapter-campaign','presentation-campaign'):
 a=load(v/f'{folder}/inputs-before.json');assert a==load(v/f'{folder}/inputs-after.json')
 for r in a:assert digest(root/r['path'])==r['sha256'],r['path']
for c in explicit['cases']:
 parsed=[json.loads(s) for s in txt(root/c['evidence']['path']).splitlines()];assert validate_selection(parsed,c['adapter'])==c['result']
for c in default['cases']:
 parsed=[json.loads(s) for s in txt(root/c['evidence']['path']).splitlines()];assert validate_events(parsed,c['case'].split('-')[0],c['case'].endswith('-vsync'))==c['result']
for c in default['pacing_cases']:
 fps=c['result']['target_fps'];parsed=[json.loads(s) for s in txt(v/f'presentation-campaign/pacing-{fps}.jsonl').splitlines()];assert validate_pacing(parsed,fps,fps+1)==c['result']
images=sum(c['result']['full_images'] for c in default['cases']+explicit['cases']);pixels=sum(c['result']['verified_pixels'] for c in default['cases']+explicit['cases']);assert images==441 and pixels==67393539
assert all(r['returncode']==0 for r in load(v/'audit-commands.json'))
profiles=[('sdk','windows-sdk-memory-budget-release',19,1),('benchmarks-phase1','windows-benchmarks-memory-budget-release',977,22),('sdk-budget','windows-sdk-memory-budget-validation-release',39,1),('sdk-stress','windows-sdk-memory-budget-stress-release',25,1),('sdk-crash','windows-sdk-memory-budget-crash-release',27,1)]
audits=[];artifacts={}
for profile,folder,count,components in profiles:
 inventory=root/'licenses/generated'/folder;data=load(inventory/'inventory.json');assert data['sdk_build_source_sha256']==fp and len(data['files'])==count and len(data['components'])==components
 for r in data['files']+data['artifacts']+data['compiler_read_evidence']+data.get('linker_read_evidence',[]):assert digest(root/r['path'])==r['sha256'],r['path']
 for r in data['artifacts']:artifacts[r['path']]=r
 for r in data['compiler_read_evidence']+data.get('linker_read_evidence',[])+list(data['links'].values()):
  p=root/r['path'];dest=v/'build-traces'/p.relative_to(root/'out');dest.parent.mkdir(parents=True,exist_ok=True)
  if dest.exists():assert dest.read_bytes()==p.read_bytes()
  else:shutil.copyfile(p,dest)
 if profile=='sdk-budget':
  assert len(data['linker_read_evidence'])==6
  for r in data['files']:
   if 'verified_source' in r:assert r['sha256']==r['verified_source']['sha256']
 if profile=='sdk-crash':assert len(data['linker_read_evidence'])==8
 audits.append({'profile':profile,'compiler_inputs':count,'components':components,'inventory':record(inventory/'inventory.json')})
previous=root/'benchmarks/reports/2026-10-22-phase2-crash-capture';old=load(previous/'evidence.json')
# Historical live out/ binaries were deliberately rebuilt. Preserve raw evidence,
# audits, source archive and seven SDK ZIPs, without claiming old live artifacts match.
for r in old['files']+old['SDK_packages']+[old['summary'],old['source_archive'],old['previous_summary'],old['previous_evidence']]:assert digest(root/r['path'])==r['sha256'],r['path']
with zipfile.ZipFile(root/old['source_archive']['path']) as z:
 for r in old['source_files']:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
assert len(old['SDK_packages'])==7
historical=root/'benchmarks/reports/2026-10-22-phase2-memory-stress';h=load(historical/'summary.json');assert not h['memory_stress_accepted']
try:analyze(read_records(root/'benchmarks/reports/validation/2026-10-22-phase2-memory-stress/campaign/validation.jsonl'),h['observed_SDK_source_fingerprint'])
except ValueError as error:assert str(error)=='Memory plateau gate failed: private_bytes'
else:raise AssertionError('Original failed memory gate changed')
assert digest(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
package=root/'out/GraphicsSDK-0.0.1-dev-windows-x64-memory-budget.zip';pkg=audit_package(root,package)
assert (root/'out/core/Release/GraphicsSDKCore.lib').read_bytes()==(root/'out/install/lib/GraphicsSDKCore.lib').read_bytes()
for script in ('pilot_memory_budget.py','verify_memory_budget.py','package_memory_budget.py','finish_memory_budget.py'):
 dest=v/'verification-scripts'/script;dest.parent.mkdir(exist_ok=True);shutil.copyfile(root/'out'/script,dest)
report.mkdir(exist_ok=False)
summary={'SDK_source_fingerprint':fp,'production_SDK_changed':True,'phase2_complete':False,'memory_stress_accepted':False,'Intel_AV_cause_resolved':False,
 'api':'Device::queryMemoryBudget(MemorySegment)','native_snapshot_observations':48,'native_hardware_retain_release_controls':4,'native_same_ID_recoveries':6,'native_verified_buffer_bytes':393216,'native_observations':snapshots,
 'installed_consumer_snapshot_observations':24,'installed_same_ID_recoveries':6,'adapters':entries,'core_ctest':28,'no_backend_ctest':6,'common_correctness_ctest':72,'presentation_ctest':7,'python_tests':147,
 'window_campaign_full_images':images,'window_campaign_verified_pixels':pixels,'compiler_retries_this_change':0,'audits':audits,'SDK_package':pkg,'previous_SDK_packages':old['SDK_packages'],
 'original_failed_memory_gate_preserved':True,'original_policy_unchanged':True,'historical_evidence_preserved':True,'old_live_out_artifacts_rebuilt':True,'new_long_soak_claim':False,'continuous_peak_claim':False,'new_performance_claim':False,
 'actual_unsupported_query_seen_this_host':False,'nonzero_reservation_forced':False,'actual_over_budget_state_forced':False,'remaining':['original_long_GBV_memory_gate','previous_Intel_AV_cause','manual_display_DPI_unexpected_TDR_hot_unplug','Vulkan_Linux_Metal_macOS']}
write(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','docs/BUILD.md','docs/ARCHITECTURE.md','docs/PUBLIC_API.md','docs/TESTING.md','docs/ROADMAP.md','docs/DEPENDENCY_LICENSES.md','docs/PHASE2_MEMORY_BUDGET_REPORT.md')}
paths|={p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
source_hash=hashlib.sha256(''.join(p.relative_to(root).as_posix()+'|'+digest(p)+'\n' for p in sorted(paths)).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/memory-budget-{fp}-{source_hash[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
 for p in sorted(paths):z.writestr(p.relative_to(root).as_posix(),p.read_bytes())
folders=[v,pilot]+[root/'licenses/generated'/folder for _,folder,_,_ in profiles]
files=[record(p) for folder in folders for p in sorted(folder.rglob('*')) if p.is_file()]
evidence={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'SDK_source_fingerprint':fp,'summary':record(report/'summary.json'),'source_archive':record(archive),'source_files':[record(p) for p in sorted(paths)],'files':files,
 'SDK_packages':[record(package)]+old['SDK_packages'],'artifacts':list(artifacts.values()),'previous_summary':record(previous/'summary.json'),'previous_evidence':record(previous/'evidence.json')}
write(report/'evidence.json',evidence)
print(f'PASS 28/6/72/7 CTests,147 Python,48 native/24 installed snapshots,441 window images; five audits/new SDK ZIP; {len(files)} frozen evidence files; seven old SDK ZIPs and failed gates retained')
