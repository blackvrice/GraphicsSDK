import pathlib,json,hashlib,subprocess,sys,zipfile,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-copy-atomicity';report=root/'benchmarks/reports/2026-10-22-phase2-copy-atomicity'
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint,audit_package
from run_presentation_validation import validate_events,validate_pacing
from run_adapter_validation import validate_list,validate_selection
from analyze_memory_stress import analyze,read_records
from run_memory_stress import validate_native
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def txt(p):return p.read_text(encoding='utf-8',errors='replace')
def record(p):return dict(path=p.relative_to(root).as_posix(),sha256=digest(p),bytes=p.stat().st_size)
def write(p,value):p.write_bytes((json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode())
fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
before=load(v/'inputs-before.json');assert before==load(v/'inputs-after.json') and before['SDK_source_fingerprint']==fp
for path,h in before['inputs'].items():assert digest(root/path)==h,path
commands=load(v/'commands.json');assert len(commands)==27 and all(r['returncode']==0 and r['attempt']==1 for r in commands)
for name,count in [('core',34),('no-backend',6),('benchmark',72),('presentation',7)]:
    text=txt(v/(name+'-ctest-1.log'));assert f'100% tests passed, 0 tests failed out of {count}' in text and 'Skipped' not in text
assert 'Ran 159 tests' in txt(v/'python-tests-1.log') and '\nOK' in txt(v/'python-tests-1.log')
old_repro=load(v/'before-repro.log');new_repro=load(v/'installed-copy-repro-1.log')
assert old_repro['rejected_copy_poisoned_submit']==1 and old_repro['retained_resources_after_drop']==3
assert new_repro['rejected_copy_poisoned_submit']==0 and new_repro['retained_resources_after_drop']==1 and new_repro['sdk']==fp
old=load(v/'historical-before.json')
rebuilt={'out/core/Release/GraphicsSDKCore.lib','out/install/lib/GraphicsSDKCore.lib','out/copy-rejection-repro/build/Release/repro.exe'}
for path,h in old['inputs'].items():
    if path not in rebuilt:assert digest(root/path)==h,path
assert len(old['SDK_packages'])==10 and old['before_repro_exit']==2
assert all(r['returncode']==0 for r in load(v/'pilot-commands.json'))
events=[json.loads(line) for line in txt(v/'core-native.log').splitlines() if line.startswith('{"event":"copy_')]
controls=[e for e in events if e['event']=='copy_rejection_case'];results=[e for e in events if e['event']=='copy_atomicity_result']
assert len(controls)==48 and len(results)==2 and {e['validation'] for e in results}=={False,True}
assert all(e['adapters']==3 and e['cases']==24 and e['full_images']==6 and e['verified_pixels']==2646 and e['verified_bytes']==28164 and e['rejections']==1557 and e['passed'] and e['sdk_sha256']==fp for e in results)
assert all(e['case'] in range(8) and e['repetitions']==64 and e['resources_after_drop']==1 and e['submit_passed'] for e in controls)
assert all(r['returncode']==0 for r in load(v/'audit-commands.json'))
audits=[];artifacts={}
for profile,folder,count,components,links in [('sdk','windows-sdk-copy-atomicity-release',20,1,None),('benchmarks-phase1','windows-benchmarks-copy-atomicity-release',978,22,5),('sdk-copy','windows-sdk-copy-atomicity-validation-release',43,1,9),('sdk-stress','windows-sdk-copy-atomicity-stress-release',26,1,5),('sdk-crash','windows-sdk-copy-atomicity-crash-release',28,1,8)]:
    p=root/'licenses/generated'/folder/'inventory.json';data=load(p)
    assert data['sdk_build_source_sha256']==fp and len(data['files'])==count and len(data['components'])==components
    if links is not None:assert len(data['linker_read_evidence'])==links,(profile,len(data['linker_read_evidence']))
    for r in data['files']+data['artifacts']+data['compiler_read_evidence']+data.get('linker_read_evidence',[]):assert digest(root/r['path'])==r['sha256'],r['path']
    for r in data['artifacts']:artifacts[r['path']]=r
    for r in data['compiler_read_evidence']+data.get('linker_read_evidence',[])+list(data['links'].values()):
        source=root/r['path'];dest=v/'build-traces'/source.relative_to(root/'out');dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.exists():assert dest.read_bytes()==source.read_bytes()
        else:shutil.copyfile(source,dest)
    audits.append(dict(profile=profile,compiler_inputs=count,components=components,inventory=record(p)))
default=load(v/'presentation-campaign/summary.json');explicit=load(v/'adapter-campaign/summary.json')
assert default['sdk_build_source_sha256']==explicit['sdk_build_source_sha256']==fp
adapters=validate_list([json.loads(s) for s in txt(v/'adapter-campaign/adapters.jsonl').splitlines()],fp)
assert len(adapters)==3 and {a['adapter_id'] for a in adapters}=={e['adapter_id'] for e in controls}
for folder in ('presentation-campaign','adapter-campaign'):
    a=load(v/f'{folder}/inputs-before.json');assert a==load(v/f'{folder}/inputs-after.json')
    for r in a:assert digest(root/r['path'])==r['sha256'],r['path']
for case in explicit['cases']:
    parsed=[json.loads(s) for s in txt(root/case['evidence']['path']).splitlines()];assert validate_selection(parsed,case['adapter'])==case['result']
for case in default['cases']:
    parsed=[json.loads(s) for s in txt(root/case['evidence']['path']).splitlines()];assert validate_events(parsed,case['case'].split('-')[0],case['case'].endswith('-vsync'))==case['result']
for case in default['pacing_cases']:
    fps=case['result']['target_fps'];parsed=[json.loads(s) for s in txt(v/f'presentation-campaign/pacing-{fps}.jsonl').splitlines()];assert validate_pacing(parsed,fps,fps+1)==case['result']
images=sum(c['result']['full_images'] for c in default['cases']+explicit['cases']);pixels=sum(c['result']['verified_pixels'] for c in default['cases']+explicit['cases']);assert (images,pixels)==(441,67393539)
for mode in ('gpu','validation','warp'):validate_native(read_records(v/f'core-memory-{mode}.jsonl'),fp,mode,None,0)
assert digest(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
h=load(root/'benchmarks/reports/2026-10-22-phase2-memory-stress/summary.json')
try:analyze(read_records(root/'benchmarks/reports/validation/2026-10-22-phase2-memory-stress/campaign/validation.jsonl'),h['observed_SDK_source_fingerprint'])
except ValueError as e:assert str(e)=='Memory plateau gate failed: private_bytes'
else:raise AssertionError('Original failed memory gate changed')
package=root/'out/GraphicsSDK-0.0.1-dev-windows-x64-copy-atomicity.zip';pkg=audit_package(root,package)
assert (root/'out/core/Release/GraphicsSDKCore.lib').read_bytes()==(root/'out/install/lib/GraphicsSDKCore.lib').read_bytes()
scripts=v/'verification-scripts';scripts.mkdir(exist_ok=True)
for name in ('preserve_copy_repro.py','pilot_copy_atomicity.py','verify_copy_atomicity.py','package_copy_atomicity.py','make_copy_finalizer.py','finish_copy_atomicity.py'):shutil.copyfile(root/'out'/name,scripts/name)
fsck=subprocess.run(['git','fsck','--full','--strict'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(v/'git-prestage-fsck.log').write_bytes(fsck.stdout);assert fsck.returncode==0
report.mkdir(exist_ok=False)
summary=dict(SDK_source_fingerprint=fp,production_SDK_changed=True,public_virtual_ABI_changed=False,phase2_complete=False,memory_stress_accepted=False,Intel_AV_cause_resolved=False,
    before_repro_exit=2,after_repro_exit=0,before_repro_rejected_copy_resources=3,after_repro_rejected_copy_resources=1,
    validation_rejection_atomicity=True,accepted_shadow_state_and_lifetimes_preserved=True,accepted_use_stale_rejection_preserved=True,
    adapters=adapters,rejection_cases=controls,repeated_validation_rejections=3072,total_rejections=3114,reference_images=12,reference_pixels=5292,verified_payload_bytes=56328,
    core_ctest=34,no_backend_ctest=6,common_correctness_ctest=72,presentation_ctest=7,python_tests=159,compiler_retries=0,
    window_campaign_full_images=images,window_campaign_verified_pixels=pixels,audits=audits,SDK_package=pkg,previous_SDK_packages=old['SDK_packages'],
    old_live_out_artifacts_rebuilt=True,historical_evidence_preserved=True,original_policy_unchanged=True,original_failed_memory_gate_preserved=True,
    new_long_soak_claim=False,continuous_peak_claim=False,performance_claim=False,git_precommit_fsck_exit=0)
write(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|={p for p in (root/'docs').glob('*.md')}|{p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
sources=[record(p) for p in sorted(paths)];source_digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/copy-atomicity-{fp}-{source_digest[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=fp,source_manifest_sha256=source_digest,files=sources),indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
    assert len(z.namelist())==len(sources)+1 and not any(pathlib.Path(n).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.nupkg') for n in z.namelist())
    for r in sources:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
files=[record(p) for p in sorted(v.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.nupkg') for r in files)
prior=root/'benchmarks/reports/2026-10-22-phase2-swap-guards/evidence.json'
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),SDK_source_fingerprint=fp,summary=record(report/'summary.json'),
    source_archive=record(archive),source_files=sources,files=files,artifacts=list(artifacts.values()),SDK_packages=old['SDK_packages']+[record(package)],previous_evidence=record(prior),previous_summary=load(prior)['summary']))
print(f'PASS final evidence: {len(files)} raw/audit files; {len(sources)} source files; 12 full images; 56,328 payload bytes; 3,114 rejections; eleven SDK ZIPs',flush=True)
