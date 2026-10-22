import pathlib,json,hashlib,subprocess,sys,zipfile,shutil,re
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-memory-campaign-final';initial=root/'benchmarks/reports/validation/2026-10-22-phase2-memory-campaign';report=root/'benchmarks/reports/2026-10-22-phase2-memory-campaign'
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint,record_file,audit_package
from analyze_memory_stress import analyze,read_records
from run_memory_stress import validate_native
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def record(p):return record_file(root,p)
def output(p,data):p.write_bytes((json.dumps(data,indent=2,ensure_ascii=False)+'\n').encode())
fp=sdk_fingerprint(root);assert fp=='85190ccf45a1c6c57473c25c8bef5c69e5387a5631a742d5561aef8b0e659970'
for folder in (initial,v):
 before=load(folder/'inputs-before.json')
 for path,h in before['inputs'].items():
  if folder==v or path not in ('tools/analyze_memory_stress.py','tools/run_memory_stress.py','tests/test_memory_campaign.py'):
   assert digest(root/path)==h,path
 if folder==initial:assert before==load(folder/'inputs-after-interrupted.json')
 else:output(folder/'inputs-after.json',before)
assert '100% tests passed, 0 tests failed out of 25' in (v/'core-ctest.log').read_text()
assert 'Ran 139 tests' in (v/'python-tests.log').read_text() and '\nOK' in (v/'python-tests.log').read_text()
entries=load(v/'adapters.json');cases=[]
for i,entry in enumerate(entries):
 folder=v/f'diagnostic-{i}';manifest=load(folder/'campaign.json');a=load(folder/'acceptance.json');r=read_records(folder/('warp.jsonl' if entry['software'] else 'gpu.jsonl'))
 assert manifest['inputs_unchanged'] and not a['accepted'] and not a['errors'] and manifest['processes'][0]['returncode']==0
 result=validate_native(r,fp,'warp' if entry['software'] else 'gpu',entry['adapter_id'],0);cases.append(result)
guards=[]
for i in (0,1):
 folder=v/f'gbv-guard-{i}';m=load(folder/'campaign.json');a=load(folder/'acceptance.json');r=read_records(folder/'validation.jsonl')
 assert m['inputs_unchanged'] and not a['accepted'] and m['processes'][0]['returncode']==2
 result=validate_native(r,fp,'validation',entries[i]['adapter_id'],2);result.update(private_growth_limit_bytes=r[0]['private_growth_limit_bytes'],elapsed=r[-1]['elapsed'],growth_observed_bytes=r[-2]['private_bytes']-r[0]['guard_baseline_private_bytes']);guards.append(result)
 try:analyze(r,fp)
 except ValueError:pass
 else:raise AssertionError('Guard abort accepted')
all_folder=v/'all-continue-512';m=load(all_folder/'campaign.json');a=load(all_folder/'acceptance.json')
assert m['inputs_unchanged'] and [p['returncode'] for p in m['processes']]==[0,2,0] and not a['accepted'] and len(a['errors'])==1
for p in m['processes']:validate_native(read_records(all_folder/(p['mode']+'.jsonl')),fp,p['mode'],None,p['returncode'])
# Preserve the direct CLI run in the aggregate transcript too.
receipts=load(v/'commands.json')
receipts.append({'name':'all-continue-512','command':[sys.executable,'tools/run_memory_stress.py','--diagnostic-seconds','10','--output',str(all_folder)],'returncode':1,'expected':1})
receipts.append({'name':'runner-missing-adapter','command':load(v/'runner-missing/campaign.json')['processes'][0]['command'],'native_returncode':1,'runner_returncode':1,'accepted':False})
output(v/'commands.json',receipts)
assert all(r.get('returncode',r.get('runner_returncode'))==r.get('expected',1) for r in receipts)
missing=load(v/'runner-missing/acceptance.json');assert not missing['accepted'] and 'exit=1' in missing['errors'][0]['error']
fault=load(initial/'gbv-guard-1/campaign.json');assert fault['processes'][0]['returncode']==3221225477 and fault['inputs_unchanged']
assert not load(initial/'gbv-guard-1/acceptance.json')['accepted']
event=load(initial/'intel-fault-events.json');assert '0xc0000005' in event['Message'] and 'unknown' in event['Message']
for name in ('native-cpu-observer','sdk-lifetime-hardware','sdk-lifetime-warp'):
 r=read_records(v/(name+'.jsonl'));f=r[-1]
 assert f.get('isolation_passed',f.get('lifetime_contracts_passed')) and f['validation_errors']==0
for script in ('verify_memory_campaign.py','verify_memory_campaign_final.py','resume_memory_campaign_final.py','audit_memory_campaign.py','finish_memory_campaign.py'):
 target=v/'verification-scripts'/script;target.parent.mkdir(exist_ok=True);shutil.copyfile(root/'out'/script,target)
output(v/'verification-notes.json',{'initial_harness_stopped':'Intel did not return an abort footer: native access violation',
 'low_limit_harness_assumption_corrected':'64/128MiB include positive-control startup costs; all three modes aborted; preserved separately',
 'final_continuation_control':'all-continue-512: native GPU 0, GBV 2, WARP 0; runner 1',
 'original_policy_unchanged':True,'no_compiler_retry_this_change':True,'memory_acceptance':False})
previous=root/'benchmarks/reports/2026-10-22-phase2-adapters';old=load(previous/'evidence.json')
for r in old['files']+[old['summary'],old['source_archive'],old['SDK_package'],old['previous_summary'],old['previous_evidence']]+old['previous_SDK_packages']+old['artifacts']:
 assert digest(root/r['path'])==r['sha256'],r['path']
for r in old['source_files']:
 with zipfile.ZipFile(root/old['source_archive']['path']) as z:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
package=audit_package(root,root/old['SDK_package']['path'])
policy=root/'tools/memory_stress_policy.json'
assert digest(policy)=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
historical=root/'benchmarks/reports/2026-10-22-phase2-memory-stress';h=load(historical/'summary.json');assert not h['memory_stress_accepted']
try:analyze(read_records(root/'benchmarks/reports/validation/2026-10-22-phase2-memory-stress/campaign/validation.jsonl'),h['observed_SDK_source_fingerprint'])
except ValueError as error:assert str(error)=='Memory plateau gate failed: private_bytes'
else:raise AssertionError('Original gate changed')
inventory=root/'licenses/generated/windows-sdk-memory-campaign-release';data=load(inventory/'inventory.json');assert len(data['files'])==25 and len(data['components'])==1 and len(data['linker_read_evidence'])==5
for r in data['files']+data['artifacts']+data['compiler_read_evidence']+data['linker_read_evidence']:
 assert digest(root/r['path'])==r['sha256'],r['path']
for r in data['compiler_read_evidence']+data['linker_read_evidence']+list(data['links'].values()):
 p=root/r['path'];target=v/'build-traces'/p.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
assert all(r['returncode']==0 for r in load(v/'audit-commands.json'))
for name in ('environment.json','drivers.json'):shutil.copyfile(initial/name,v/name)
report.mkdir(exist_ok=False)
summary={'SDK_source_fingerprint':fp,'production_SDK_changed':False,'phase2_complete':False,'memory_stress_accepted':False,
 'core_ctest':25,'python_tests':139,'added_python_tests':15,'diagnostics':cases,'guards':guards,
 'continuation':{'native_exit_codes':[0,2,0],'runner_exit_code':1,'default_guard_bytes':512*1024**2,'diagnostic_seconds':10,'campaign':record(all_folder/'campaign.json')},
 'initial_Intel_failure':{'native_exit_code':3221225477,'exception_hex':'0xc0000005','wall_seconds':fault['processes'][0]['wall_seconds'],'complete_footer':False,'cause_resolved':False,'faulting_module':'unknown','campaign':record(initial/'gbv-guard-1/campaign.json'),'event':record(initial/'intel-fault-events.json')},
 'native_cli_rejection_controls':11,'runner_missing_ID_rejected':True,'native_CPU_observer_smoke_passed':True,'SDK_lifetime_smoke_modes':['hardware','warp'],
 'stress_inventory':record(inventory/'inventory.json'),'compiler_read_inputs':25,'components':1,'linker_closures':5,
 'original_policy_unchanged':True,'original_failed_memory_gate_preserved':True,'previous_evidence_preserved':True,'SDK_package':package,'new_SDK_package':False,'SDK_packages_preserved':7,
 'new_performance_claim':False,'continuous_peak_claim':False,'remaining':['original_long_GBV_memory_gate','Intel_native_access_violation_root_cause','manual_display_DPI_unexpected_TDR_hot_unplug','Vulkan_Linux_Metal_macOS']}
output(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','docs/BUILD.md','docs/ARCHITECTURE.md','docs/PUBLIC_API.md','docs/TESTING.md','docs/ROADMAP.md','docs/DEPENDENCY_LICENSES.md','docs/PHASE2_MEMORY_CAMPAIGN_REPORT.md')}
paths|={p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
source_hash=hashlib.sha256(''.join(p.relative_to(root).as_posix()+'|'+digest(p)+'\n' for p in sorted(paths)).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/memory-campaign-{fp}-{source_hash[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
 for p in sorted(paths):z.writestr(p.relative_to(root).as_posix(),p.read_bytes())
files=[record(p) for folder in (initial,v,inventory) for p in sorted(folder.rglob('*')) if p.is_file()]
evidence={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'SDK_source_fingerprint':fp,'summary':record(report/'summary.json'),'source_archive':record(archive),'source_files':[record(p) for p in sorted(paths)],'files':files,
 'artifacts':[record(root/p) for p in ('out/core/Release/GraphicsSDKCore.lib','out/core/Release/gfx_memory_stress.exe','out/memory-validation-probe/Release/memory_validation_probe.exe','out/memory-validation-probe/Release/sdk_lifetime_probe.exe')],
 'SDK_packages':[old['SDK_package']]+old['previous_SDK_packages'],'previous_summary':record(previous/'summary.json'),'previous_evidence':record(previous/'evidence.json')}
output(report/'evidence.json',evidence)
print(f'PASS exact-ID stress tooling, 25 CTests/139 Python, 0/2/0 continuation, failed gates retained; {len(files)} evidence files; seven old SDK ZIPs unchanged')
