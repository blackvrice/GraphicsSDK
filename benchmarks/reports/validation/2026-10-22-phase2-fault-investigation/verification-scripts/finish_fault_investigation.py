import pathlib,json,hashlib,subprocess,sys,zipfile,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation';report=root/'benchmarks/reports/2026-10-23-phase2-fault-investigation'
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint,audit_package
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return dict(path=p.relative_to(root).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
assert sha(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
previous=root/'benchmarks/reports/2026-10-22-phase2-thread-snapshot/evidence.json';prior=load(previous)
preserved=prior['files']+[prior[k] for k in ('summary','source_archive','previous_evidence')]+prior['artifacts']+prior['SDK_packages']+[rec(previous)]
for r in preserved:assert sha(root/r['path'])==r['sha256'],r['path']
assert len(prior['SDK_packages'])==11
package=audit_package(root,root/'out/GraphicsSDK-0.0.1-dev-windows-x64-copy-atomicity.zip')
inventory=root/'licenses/generated/windows-sdk-thread-snapshot-crash-release/inventory.json';inv=load(inventory)
assert inv['sdk_build_source_sha256']==fp
for r in inv['files']+inv['artifacts']+inv['compiler_read_evidence']+inv['linker_read_evidence']+list(inv['links'].values()):assert sha(root/r['path'])==r['sha256'],r['path']
observations=load(v/'verified-observations.json');assert len(observations['controls'])==12 and len(observations['long_Intel_runs'])==5
assert observations['total_frames']==53936 and observations['total_full_images']==161808
paths=load(v/'quantified-gbv-path.json');assert [r.get('add_resource_info_allocate_cb_commit_call_bytes',0) for r in paths]==[320028672,0,111587328,0]
for folder in ('allocation-origin','retention-controls','spaced-controls','extended-intel',''):
    base=v/folder;before=load(base/'inputs-before.json');assert before==load(base/'inputs-after.json')
    for name,digest in before.items():assert sha(pathlib.Path(name))==digest,name
# Preserve helper/compiler artifact identities without redistributing artifacts.
helper=load(v/'helper-build/inventory.json');assert len(helper['own_input_union'])==5
for project in helper['projects']:
    for r in project['own_compiler_inputs']+project['artifacts_local_only']+project['evidence']:assert sha(root/r['path'])==r['sha256']
for name in ('.gitattributes','README.md','docs/ROADMAP.md','docs/PHASE2_FAULT_INVESTIGATION_REPORT.md'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
shutil.copyfile(root/'out/finish_fault_investigation.py',v/'verification-scripts/finish_fault_investigation.py')
commands=[]
for args in ([sys.executable,'tools/audit_licenses.py','--profile','sdk-crash','--output','licenses/generated/windows-sdk-thread-snapshot-crash-release','--check'],['git','fsck','--full','--strict']):
    p=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);assert p.returncode==0,p.stdout
    name='audit-recheck.log' if args[0]==sys.executable else 'git-prestage-fsck.log';(v/name).write_bytes(p.stdout);commands.append(dict(command=args,returncode=p.returncode,log=rec(v/name)))
report.mkdir(exist_ok=False)
summary=dict(SDK_source_fingerprint=fp,production_SDK_changed=False,phase2_complete=False,memory_stress_accepted=False,Intel_AV_cause_resolved=False,
    native_lifetime_controls=12,native_resource_destruction_callbacks=12*1536,native_device_destruction_callbacks=12,virtual_trace_controls=4,heap_trace_controls=6,
    zero_event_loss=True,virtual_event_scope='target child only',commit_byte_sums_are_not_net_unique_pages=True,
    observed_allocation_path=['NDebug::CGBVDataController::AddResourceInfo','NDXGI::CDevice::AllocateCB_0022'],commit_call_paths=paths,
    spaced_live_device_private_byte_pairs={'NVIDIA':[328519680,491929600],'Intel':[143175680,219828224]},
    post_device_reclamation_observed_by_seconds=15,unbounded_leak_proven=False,internal_Microsoft_source_defect_proven=False,
    long_Intel_runs=observations['long_Intel_runs'],total_frames=53936,total_full_images=161808,total_verified_buffer_bytes=14138998784,
    Intel_second_chance_faults=0,unmonitored_normal_exit=0,unmonitored_runner_exit=1,extended_seconds=600,extended_diagnostic_guard_mib=1024,
    original_policy_unchanged=True,earlier_evidence_and_eleven_SDK_packages_preserved=True,SDK_repackaged=False,
    complete_regression_rerun=False,performance_claim=False,continuous_peak_claim=False,
    helper_own_compiler_inputs=5,helper_native_projects=4,existing_sdk_crash_compiler_inputs=28,existing_inventory=rec(inventory),SDK_package=package,checks=commands)
write(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|={p for p in (root/'docs').glob('*.md')}|{p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
paths|={root/'out'/p/name for p in ('gbv-allocation-origin','gbv-retention-control','gbv-spaced-control','private-virtual-controller') for name in ('main.cpp','CMakeLists.txt')}
paths|={root/'out'/p.name for p in (v/'verification-scripts').iterdir()}
sources=[rec(p) for p in sorted(paths)];manifest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/fault-investigation-{fp}-{manifest[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=fp,source_manifest_sha256=manifest,files=sources),indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
    assert len(z.namelist())==len(sources)+1
    assert not any(pathlib.Path(n).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.nupkg','.etl','.csv') for n in z.namelist())
    for r in sources:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
files=[rec(p) for p in sorted(v.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.nupkg','.etl','.csv') for r in files)
assert 'S-1-5-21-' not in (v/'target-application-errors.json').read_text() and '<Event' not in (v/'target-application-errors.json').read_text()
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),SDK_source_fingerprint=fp,
    summary=rec(report/'summary.json'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=preserved,
    SDK_packages=prior['SDK_packages'],previous_evidence=rec(previous),helper_build_inventory=rec(v/'helper-build/inventory.json')))
print(f'PASS: {len(files)} raw files; {len(sources)} own sources; original policy/SDK/eleven SDK packages preserved; source archive independently verified',flush=True)
