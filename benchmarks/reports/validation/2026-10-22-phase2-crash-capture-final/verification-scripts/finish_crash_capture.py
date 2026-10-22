import pathlib,json,hashlib,subprocess,sys,shutil,zipfile
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-crash-capture-final';initial=root/'benchmarks/reports/validation/2026-10-22-phase2-crash-capture';report=root/'benchmarks/reports/2026-10-22-phase2-crash-capture'
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint,record_file,audit_package
from crash_capture.evidence import validate,read_records
from analyze_memory_stress import analyze
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def record(p):return record_file(root,p)
def write(p,value):p.write_bytes((json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode())
fp=sdk_fingerprint(root);assert fp=='85190ccf45a1c6c57473c25c8bef5c69e5387a5631a742d5561aef8b0e659970'
assert '100% tests passed, 0 tests failed out of 25' in (v/'core-ctest.log').read_text(encoding='utf-8',errors='replace')
assert 'Ran 147 tests' in (v/'python-tests-final.log').read_text(encoding='utf-8',errors='replace')
assert '\nOK' in (v/'python-tests-final.log').read_text(encoding='utf-8',errors='replace')
controls=load(v/'native-controls/summary.json');assert len(controls['cases'])==7 and controls['overwrite_refused']
for case in controls['cases']:
 records=read_records(v/'native-controls'/(case['case']+'.jsonl'));result=validate(records,case['returncode'],case['returncode'])
 assert not result['memory_acceptance']
 if case['case']=='null-call':assert result['second_chance_count']==1 and result['context_available']
for path,h in controls['frozen_inputs'].items():assert digest(pathlib.Path(path))==h,path
intel=load(v/'intel-full-gbv/summary.json');assert intel['status']=='completed' and intel['capture_complete'] and intel['inputs_unchanged'] and not intel['memory_acceptance']
for path,h in intel['frozen_inputs'].items():assert digest(pathlib.Path(path))==h,path
capture=read_records(v/'intel-full-gbv/capture.jsonl');validate(capture,0,0)
assert capture[-1]['second_chance_count']==0 and capture[-1]['debug_message_count']==53103 and capture[-1]['debug_text_bytes']==65374
r=read_records(v/'intel-full-gbv/memory.jsonl');memory_footer=r[-1];assert r[-1]['frames']==17696 and r[-1]['full_images']==53088 and r[-1]['validation_errors']==0
assert r[0]['gpu_validation'] and r[0]['gpu_state_tracking'] and r[0]['private_growth_limit_bytes']==512*1024**2 and r[-1]['measured_seconds']>=300
observation=load(v/'intel-diagnostic-policy-observation.json');assert not observation['memory_acceptance'] and observation['error']=='Memory plateau gate failed: private_bytes'
short=[]
for folder in sorted(v.glob('diagnostic-*')):
 if not folder.is_dir():continue
 s=load(folder/'summary.json');assert s['status']=='completed' and s['capture_complete'] and s['inputs_unchanged'] and not s['memory_acceptance'];short.append(s['references'])
 for path,h in s['frozen_inputs'].items():assert digest(pathlib.Path(path))==h,path
 assert not validate(read_records(folder/'capture.jsonl'),0,0)['memory_acceptance']
assert len(short)==3
commands=load(v/'commands.json');assert all(x['returncode']==0 for x in commands)
assert load(v/'verification-expectation.json')['original_harness_expected_runner_exit']==1
for name in ('previous-audit-commands.json','final-audit-commands.json'):assert all(x['returncode']==0 for x in load(v/name))
inventory=root/'licenses/generated/windows-sdk-crash-capture-final-release';data=load(inventory/'inventory.json')
assert len(data['files'])==27 and len(data['components'])==1 and len(data['linker_read_evidence'])==8
for r in data['files']+data['artifacts']+data['compiler_read_evidence']+data['linker_read_evidence']:assert digest(root/r['path'])==r['sha256'],r['path']
for r in data['compiler_read_evidence']+data['linker_read_evidence']+list(data['links'].values()):
 p=root/r['path'];target=v/'build-traces'/p.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
previous=root/'benchmarks/reports/2026-10-22-phase2-memory-campaign';old=load(previous/'evidence.json')
for r in old['files']+old['SDK_packages']+old['artifacts']+[old['summary'],old['source_archive'],old['previous_summary'],old['previous_evidence']]:assert digest(root/r['path'])==r['sha256'],r['path']
with zipfile.ZipFile(root/old['source_archive']['path']) as z:
 for r in old['source_files']:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
package=audit_package(root,root/old['SDK_packages'][0]['path'])
assert digest(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
fsck=subprocess.run(['git','fsck','--no-reflogs','--no-dangling'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(v/'git-fsck.log').write_bytes(fsck.stdout);assert fsck.returncode==0
index=root/'out/index-recovery-20261022-170810';idx=load(index/'metadata.json');comparison=load(index/'index-tree-comparison.json');assert idx['trailer_sha1_valid'] and comparison['entries']==comparison['tree_entries']==3509 and not comparison['differences'] and not comparison['bad_modes']
write(v/'repository-read-observation.json',{'initial_error':'unknown index entry format 0x62650000','original_index_sha256':idx['sha256'],'index_trailer_valid':True,'HEAD_matching_entries':3509,'index_replaced':False,'working_files_replaced':False,'read_only_fsck_exit':0,'cause_resolved':False})
write(initial/'quarantined-controls.json',{'originals_local_only':'out/crash-capture-controls-initial-raw','reason':'pre-fix optional debug-text byte-length reads','excluded_from_publication_evidence':True})
for script in ('verify_crash_capture.py','verify_crash_capture_final.py','resume_crash_capture.py','finish_crash_capture.py'):
 target=v/'verification-scripts'/script;target.parent.mkdir(exist_ok=True);shutil.copyfile(root/'out'/script,target)
local_dumps=[]
for path in (root/'out/crash-capture-controls-final-v2').glob('*.dmp'):local_dumps.append({'path':str(path),'sha256':digest(path),'bytes':path.stat().st_size,'redistributed':False})
assert len(local_dumps)==1 and not (root/'out/intel-gbv-crash-capture-final.dmp').exists()
for folder in (v,initial):assert not any(p.suffix.lower() in ('.dmp','.pdb','.exe','.dll','.lib') for p in folder.rglob('*') if p.is_file())
report.mkdir(exist_ok=False)
summary={'SDK_source_fingerprint':fp,'production_SDK_changed':False,'phase2_complete':False,'memory_stress_accepted':False,'Intel_crash_cause_resolved':False,'Intel_crash_reproduced_this_run':False,
 'core_ctest':25,'python_tests':147,'added_python_tests':8,'native_control_cases':7,'native_overwrite_refusal':True,
 'Intel_instrumented':{'status':'completed','warmup_seconds':30,'measured_seconds':memory_footer['measured_seconds'],'frames':17696,'full_images':53088,'verified_buffer_bytes':4638900224,'validation_errors':0,'second_chance_exceptions':0,'debug_messages':53103,'debug_text_bytes':65374,'strict_policy_observation':observation,'memory_acceptance':False},
 'short_instrumented_exact_adapter_checks':short,'local_dump_receipts':local_dumps,'dump_redistribution':False,
 'compiler_access_violations_observed':1,'unchanged_source_retry_passed':True,'compiler_failure_cause_resolved':False,'transient_git_read_cause_resolved':False,
 'initial_debug_text_length_defect_repaired':True,'initial_rejected_capture_quarantined':True,'initial_harness_failure_expectation_corrected':True,
 'audits':{'compiler_inputs':27,'components':1,'actual_linker_closures':8,'inventory':record(inventory/'inventory.json')},
 'SDK_packages_preserved':7,'previous_evidence_preserved':True,'original_policy_unchanged':True,'new_SDK_package':False,'new_performance_claim':False,'remaining':['original_long_GBV_memory_gate','original_Intel_AV_cause','manual_display_DPI_TDR_hot_unplug','Vulkan_Linux_Metal_macOS']}
write(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE','docs/BUILD.md','docs/ARCHITECTURE.md','docs/PUBLIC_API.md','docs/TESTING.md','docs/ROADMAP.md','docs/DEPENDENCY_LICENSES.md','docs/PHASE2_CRASH_CAPTURE_REPORT.md')}
paths|={p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
h=hashlib.sha256(''.join(p.relative_to(root).as_posix()+'|'+digest(p)+'\n' for p in sorted(paths)).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/crash-capture-{fp}-{h[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
 for p in sorted(paths):z.writestr(p.relative_to(root).as_posix(),p.read_bytes())
folders=(v,initial,inventory,root/'licenses/generated/windows-sdk-crash-capture-release')
files=[record(p) for folder in folders for p in sorted(folder.rglob('*')) if p.is_file()]
evidence={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'SDK_source_fingerprint':fp,'summary':record(report/'summary.json'),'source_archive':record(archive),'source_files':[record(p) for p in sorted(paths)],'files':files,'SDK_packages':old['SDK_packages'],'previous_summary':record(previous/'summary.json'),'previous_evidence':record(previous/'evidence.json'),
 'artifacts':[record(root/p) for p in ('out/core/Release/GraphicsSDKCore.lib','out/crash-capture/Release/gfx_crash_capture.exe','out/crash-capture/Release/gfx_crash_capture.pdb','out/crash-capture/Release/gfx_crash_fixture.exe','out/crash-capture/Release/gfx_crash_fixture.pdb','out/crash-capture/Release/gfx_memory_stress_symbols.exe','out/crash-capture/Release/gfx_memory_stress_symbols.pdb')], 'local_dumps_not_redistributed':local_dumps}
write(report/'evidence.json',evidence)
print(f'PASS 25 CTests/147 Python, seven native controls/overwrite, full Intel instrumented references and three adapter checks; {len(files)} evidence files; previous failed gates retained; dumps local only')
