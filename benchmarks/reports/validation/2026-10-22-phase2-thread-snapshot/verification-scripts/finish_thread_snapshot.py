import pathlib, hashlib, json, subprocess, sys, shutil, zipfile
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-thread-snapshot';report=root/'benchmarks/reports/2026-10-22-phase2-thread-snapshot'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def record(p):return dict(path=p.relative_to(root).as_posix(),sha256=digest(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint,audit_package
from crash_capture.evidence import validate,read_records
from run_memory_stress import validate_native
from analyze_memory_stress import analyze
fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
inputs=load(v/'inputs-before.json');assert inputs==load(v/'inputs-after.json')
assert all(digest(root/r['path'])==r['sha256'] for r in inputs)
preserved=load(v/'preserved-before.json');assert all(digest(root/r['path'])==r['sha256'] for r in preserved)
commands=load(v/'commands.json');assert len(commands)==9 and all(c['returncode']==c['expected_exit']==0 for c in commands)
assert 'Ran 169 tests' in (v/'python-tests.log').read_text() and 'OK' in (v/'python-tests.log').read_text() and 'skipped' not in (v/'python-tests.log').read_text()
assert '100% tests passed, 0 tests failed out of 34' in (v/'core-tests.log').read_text()
controls=load(v/'controls/summary.json');assert len(controls['cases'])==9 and controls['overwrite_refused']
for name,sha in controls['frozen_inputs'].items():assert digest(pathlib.Path(name))==sha,name
snapshots=[]
for case in controls['cases']:
    records=read_records(v/'controls'/f"{case['case']}.jsonl")
    assert validate(records,case['returncode'],case['returncode'])=={k:value for k,value in case['result'].items() if k!='local_minidump_sha256'}
    snapshots.extend(dict(case=case['case'],**s) for s in case['result']['thread_snapshots'])
assert len(snapshots)==4
assert next(s for s in snapshots if s['case']=='worker-crash')['all_threads_captured']
assert not next(s for s in snapshots if s['case']=='thread-limit')['all_threads_captured']
# Keep the real dump local; only its location/hash receipt is redistributed.
dump=v/'controls/null-call.dmp';destination=root/'out/thread-snapshot-null-call.dmp'
assert dump.read_bytes().startswith(b'MDMP') and not destination.exists()
dump_receipt=dict(path=destination.relative_to(root).as_posix(),sha256=digest(dump),bytes=dump.stat().st_size,redistributed=False)
shutil.move(str(dump),str(destination));assert digest(destination)==dump_receipt['sha256']
write(v/'local-dump.json',dump_receipt)
# Replay original schema 1 controls read-only with the new parser.
historical=root/'out/crash-capture-controls-final-v2';legacy=[]
for mode,code in [('handled',0),('arguments',0),('exit-seven',7),('null-call',0xc0000005),('breakpoint',0x80000003),('sleep',124),('debug-spam',0)]:
    p=historical/(mode+'.jsonl');data=read_records(p);assert data[0]['schema']==1
    result=validate(data,code,code);assert not result['memory_acceptance']
    legacy.append(dict(case=mode,source=record(p),result=result))
write(v/'legacy-schema1-replay.json',legacy)
memory=[]
for i in range(3):
    folder=v/f'memory-{i}';data=load(folder/'summary.json');assert data['inputs_unchanged'] and data['status']=='completed' and not data['memory_acceptance']
    for p,sha in data['frozen_inputs'].items():assert digest(pathlib.Path(p))==sha,p
    records=read_records(folder/'capture.jsonl');assert records[0]['schema']==2
    assert validate(records,0,0)==data['capture'] and not data['capture']['thread_snapshots']
    workload=read_records(folder/'memory.jsonl');h=workload[0]
    assert h['seconds']==5 and h['warmup']==1 and h['sdk_sha256']==fp
    assert validate_native(workload,fp,h['mode'],h['adapter_id'],0)==data['references']
    memory.append(dict(adapter=h['adapter'],**data['references']))
assert {r['mode'] for r in memory}=={'validation','warp'}
assert sum(r['frames'] for r in memory)==1200 and sum(r['full_images'] for r in memory)==3600
assert sum(r['verified_buffer_bytes'] for r in memory)==314572800
inventory=root/'licenses/generated/windows-sdk-thread-snapshot-crash-release/inventory.json';audit=load(inventory)
assert audit['sdk_build_source_sha256']==fp and len(audit['files'])==28 and len(audit['components'])==1 and len(audit['linker_read_evidence'])==8
for r in audit['files']+audit['artifacts']+audit['compiler_read_evidence']+audit['linker_read_evidence']+list(audit['links'].values()):assert digest(root/r['path'])==r['sha256'],r['path']
for r in audit['compiler_read_evidence']+audit['linker_read_evidence']+list(audit['links'].values()):
    p=root/r['path'];dest=v/'build-traces'/p.relative_to(root/'out');dest.parent.mkdir(parents=True,exist_ok=True)
    if dest.exists():assert dest.read_bytes()==p.read_bytes()
    else:shutil.copyfile(p,dest)
assert digest(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
original=load(root/'benchmarks/reports/2026-10-22-phase2-memory-stress/summary.json')
try:analyze(read_records(root/'benchmarks/reports/validation/2026-10-22-phase2-memory-stress/campaign/validation.jsonl'),original['observed_SDK_source_fingerprint'])
except ValueError as e:assert str(e)=='Memory plateau gate failed: private_bytes'
else:raise AssertionError('Original failed memory gate changed')
package=audit_package(root,root/'out/GraphicsSDK-0.0.1-dev-windows-x64-copy-atomicity.zip')
for p in [root/'.gitattributes',root/'README.md',root/'tools/crash_capture/README.md',*list((root/'docs').glob('*.md'))]:
    # Only normalize the changed documentation, preserving all unrelated sources.
    if p.as_posix() in {str(root/n).replace('\\','/') for n in ('.gitattributes','README.md','tools/crash_capture/README.md','docs/TESTING.md','docs/DEPENDENCY_LICENSES.md','docs/ROADMAP.md','docs/PHASE2_THREAD_SNAPSHOT_REPORT.md')}:
        p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
scripts=v/'verification-scripts';scripts.mkdir()
for name in ('verify_thread_snapshot.py','finish_thread_snapshot.py'):shutil.copyfile(root/'out'/name,scripts/name)
p=subprocess.run(['git','fsck','--full','--strict'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(v/'git-prestage-fsck.log').write_bytes(p.stdout);assert p.returncode==0
report.mkdir(exist_ok=False)
summary=dict(SDK_source_fingerprint=fp,production_SDK_changed=False,phase2_complete=False,memory_stress_accepted=False,Intel_AV_cause_resolved=False,
    native_controls=9,overwrite_refused=True,schema1_replay_cases=7,thread_limit=16,frame_limit=64,snapshots=snapshots,core_ctest=34,python_tests=169,
    short_diagnostics=memory,full_images=3600,verified_buffer_bytes=314572800,new_long_soak_claim=False,continuous_peak_claim=False,performance_claim=False,
    original_policy_unchanged=True,original_failed_memory_gate_preserved=True,old_evidence_and_eleven_SDK_packages_preserved=True,
    audit=dict(profile='sdk-crash',compiler_inputs=28,components=1,linker_closures=8,inventory=record(inventory)),SDK_package=package,local_dump=dump_receipt)
write(report/'summary.json',summary)
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|={p for p in (root/'docs').glob('*.md')}|{p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
sources=[record(p) for p in sorted(paths)];source_digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/thread-snapshot-{fp}-{source_digest[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=fp,source_manifest_sha256=source_digest,files=sources),indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
    assert len(z.namelist())==len(sources)+1
    assert not any(pathlib.Path(n).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.nupkg') for n in z.namelist())
    for r in sources:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
files=[record(p) for p in sorted(v.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.lib','.pdb','.dmp','.nupkg') for r in files)
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),SDK_source_fingerprint=fp,
    summary=record(report/'summary.json'),source_archive=record(archive),source_files=sources,files=files,artifacts=audit['artifacts'],preserved_inputs=preserved,
    SDK_packages=[r for r in preserved if r['path'].startswith('out/GraphicsSDK-')],previous_evidence=record(root/'benchmarks/reports/2026-10-22-phase2-copy-atomicity/evidence.json')))
print(f'PASS evidence: {len(files)} raw files, {len(sources)} own sources, nine native controls, seven legacy replays, four bounded snapshots, eleven preserved SDK ZIPs',flush=True)
