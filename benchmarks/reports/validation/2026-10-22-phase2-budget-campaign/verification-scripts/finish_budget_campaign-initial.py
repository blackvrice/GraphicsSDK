import pathlib,json,hashlib,zipfile,shutil,subprocess,sys
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-budget-campaign';report=root/'benchmarks/reports/2026-10-22-phase2-budget-campaign'
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint
from analyze_memory_stress import read_records,analyze
from run_memory_stress import validate_native
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def record(p):return dict(path=p.relative_to(root).as_posix(),sha256=digest(p),bytes=p.stat().st_size)
def write(p,value):p.write_bytes((json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode())
fp=sdk_fingerprint(root);before=load(v/'inputs-before.json');results=load(v/'results.json')
assert before==load(v/'inputs-after.json') and fp==before['SDK_source_fingerprint']==results['SDK_source_fingerprint']
for path,sha in {**before['inputs'],**before['historical']}.items():assert digest(root/path)==sha,path
assert digest(root/'out/core/Release/GraphicsSDKCore.lib')==before['library_sha256']
commands=load(v/'commands.json');assert len(commands)==23
assert all(r['returncode']==(1 if r['name']=='guard-control' else 0) for r in commands)
assert '100% tests passed, 0 tests failed out of 28' in (v/'core-ctest.log').read_text()
assert 'Ran 158 tests' in (v/'python-tests.log').read_text() and '\nOK' in (v/'python-tests.log').read_text()
snapshots=images=frames=buffer_bytes=0
for case in results['cases']:
    r=case['result'];mode=r['mode'];records=read_records(v/case['case']/(mode+'.jsonl'))
    assert validate_native(records,fp,mode,r['adapter_id'],0)==r
    assert not r['accepted'] and r['status']=='passed'
    for c in r['public_memory_budget']['segments'].values():
        assert c['available']==c['stable_native_matches']==c['snapshots'] and c['unsupported']==c['unstable_native_brackets']==0
        snapshots+=c['snapshots']
    images+=r['full_images'];frames+=r['frames'];buffer_bytes+=r['verified_buffer_bytes']
assert (snapshots,images,frames,buffer_bytes)==(146,7608,2536,664797184)
guard=results['guard'];assert validate_native(read_records(v/'guard-control/validation.jsonl'),fp,'validation',guard['adapter_id'],2)==guard
assert guard['frames']==8 and guard['full_images']==24 and all(c['snapshots']==7 for c in guard['public_memory_budget']['segments'].values())
capture=load(v/'symbol-capture/summary.json');assert capture['status']=='completed' and capture['inputs_unchanged'] and capture['capture']['second_chance_count']==0 and not capture['memory_acceptance']
assert capture['references']['frames']==312 and capture['references']['full_images']==936
for mode in ('gpu','validation','warp'):
    records=read_records(v/f'core-memory-{mode}.jsonl')
    validate_native(records,fp,mode,None,0)
assert read_records(v/'native-observer.jsonl')[-1]['isolation_passed']
lifetime=read_records(v/'sdk-lifetime.jsonl')[-1];assert lifetime['lifetime_contracts_passed'] and lifetime['device_destructions']==2 and lifetime['full_images']==96
audits=[];artifacts={}
for profile,folder,count,links in [('sdk-stress','windows-sdk-budget-campaign-release',25,5),('sdk-crash','windows-sdk-budget-campaign-crash-release',27,8)]:
    path=root/'licenses/generated'/folder/'inventory.json';data=load(path)
    assert data['sdk_build_source_sha256']==fp and len(data['files'])==count and len(data['components'])==1 and len(data['linker_read_evidence'])==links
    for r in data['files']+data['artifacts']+data['compiler_read_evidence']+data['linker_read_evidence']:
        assert digest(root/r['path'])==r['sha256'],r['path']
    for r in data['artifacts']:artifacts[r['path']]=r
    for r in data['compiler_read_evidence']+data['linker_read_evidence']+list(data['links'].values()):
        p=root/r['path'];dest=v/'build-traces'/p.relative_to(root/'out');dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.exists():assert dest.read_bytes()==p.read_bytes()
        else:shutil.copyfile(p,dest)
    audits.append(dict(profile=profile,compiler_inputs=count,components=1,linker_closures=links,inventory=record(path)))
prior=load(root/'benchmarks/reports/2026-10-22-phase2-memory-budget/evidence.json')
for r in prior['SDK_packages']:assert digest(root/r['path'])==r['sha256']
historical=root/'benchmarks/reports/2026-10-22-phase2-memory-stress'
h=load(historical/'summary.json');assert not h['memory_stress_accepted']
try:analyze(read_records(root/'benchmarks/reports/validation/2026-10-22-phase2-memory-stress/campaign/validation.jsonl'),h['observed_SDK_source_fingerprint'])
except ValueError as e:assert str(e)=='Memory plateau gate failed: private_bytes'
else:raise AssertionError('Original failed gate changed')
assert digest(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
scripts=v/'verification-scripts';scripts.mkdir(exist_ok=True)
for name in ('verify_budget_campaign.py','finish_budget_campaign.py'):shutil.copyfile(root/'out'/name,scripts/name)
report.mkdir(exist_ok=False)
summary=dict(SDK_source_fingerprint=fp,production_SDK_changed=False,phase2_complete=False,memory_stress_accepted=False,Intel_AV_cause_resolved=False,
    schema=3,explicit_adapter_cases=results['cases'],public_snapshot_observations=snapshots,reference_frames=frames,full_images=images,verified_buffer_bytes=buffer_bytes,
    guard=guard,symbol_capture=capture['references'],symbol_capture_second_chance_count=0,core_ctest=28,python_tests=158,new_python_tests=11,compiler_retries=0,
    native_observer_smoke_passed=True,sdk_lifetime_generations=2,sdk_lifetime_full_images=96,library_unchanged=True,SDK_packages_preserved=8,
    original_policy_unchanged=True,original_failed_memory_gate_preserved=True,historical_evidence_preserved=True,
    no_new_SDK_binary_package=True,new_long_soak_claim=False,continuous_peak_claim=False,performance_claim=False,
    actual_unsupported_query_seen=False,actual_unstable_bracket_seen=False,actual_over_budget_forced=False,nonzero_reservation_forced=False,
    common_and_presentation_ctests_rerun=False,existing_sdk_common_budget_inventories_checked=True,audits=audits)
write(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|={p for p in (root/'docs').glob('*.md')}|{p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
source_files=[record(p) for p in sorted(paths)]
manifest=json.dumps(source_files,sort_keys=True,separators=(',',':')).encode()
source_digest=hashlib.sha256(manifest).hexdigest()
archive=root/f'benchmarks/reports/sources/budget-campaign-{fp}-{source_digest[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in source_files:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=fp,source_manifest_sha256=source_digest,files=source_files),indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
    assert len(z.namelist())==len(source_files)+1 and not any(pathlib.Path(n).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.nupkg') for n in z.namelist())
    for r in source_files:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
files=[record(p) for p in sorted(v.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.nupkg') for r in files)
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),SDK_source_fingerprint=fp,
    summary=record(report/'summary.json'),source_archive=record(archive),source_files=source_files,files=files,artifacts=list(artifacts.values()),
    SDK_packages=prior['SDK_packages'],previous_summary=prior['summary'],previous_evidence=record(root/'benchmarks/reports/2026-10-22-phase2-memory-budget/evidence.json')))
print(f'PASS final evidence: {len(files)} raw/trace files; {len(source_files)} source files; {snapshots} public snapshots; eight preserved SDK ZIPs',flush=True)
