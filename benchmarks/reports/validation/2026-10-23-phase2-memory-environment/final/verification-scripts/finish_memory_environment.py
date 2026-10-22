import pathlib,json,hashlib,subprocess,sys,zipfile,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-23-phase2-memory-environment';final=v/'final';report=root/'benchmarks/reports/2026-10-23-phase2-memory-environment'
sys.path.insert(0,str(root/'tools'))
import memory_environment as env
from audit_licenses import sdk_fingerprint,audit_package
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return dict(path=p.relative_to(root).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
before=load(final/'inputs-before.json');assert before==load(final/'inputs-after.json') and all(sha(pathlib.Path(p))==h for p,h in before.items())
initial=load(v/'inputs-before.json');assert sha(v/'initial-tool-source.py')==initial[str(root/'tools/memory_environment.py')]
assert sha(v/'initial-verification-source.py')==initial[str(root/'out/verify_memory_environment.py')]
assert not (v/'baseline').exists() and 'Get-FileHash' in (v/'baseline.log').read_text()
commands=load(final/'commands.json');assert len(commands)==10 and all(c['returncode']==c['expected_exit'] for c in commands)
log=(final/'python-tests.log').read_text();assert 'Ran 196 tests' in log and 'OK' in log and 'skipped' not in log
cases=[env.validated_case(final/name/'case.json')[0] for name in ('baseline','same-environment')]
assert sum(c['references']['frames'] for c in cases)==472 and sum(c['references']['full_images'] for c in cases)==1416
assert sum(c['references']['verified_buffer_bytes'] for c in cases)==123731968
assert env.compare_cases(final/'baseline/case.json',final/'same-environment/case.json','none')==load(final/'comparison.json')
assert not load(final/'missing-driver-change.json')['recorded_controls_match']
previous=root/'benchmarks/reports/2026-10-23-phase2-virtual-ranges/evidence.json';prior=load(previous)
preserved=prior['files']+prior['preserved_inputs']+[prior[k] for k in ('summary','source_archive','previous_evidence')]+[rec(previous)]
assert all(sha(root/r['path'])==r['sha256'] for r in preserved)
assert len(prior['SDK_packages'])==11 and all(sha(root/r['path'])==r['sha256'] for r in prior['SDK_packages'])
package=audit_package(root,root/'out/GraphicsSDK-0.0.1-dev-windows-x64-copy-atomicity.zip')
for name in ('.gitattributes','README.md','docs/ROADMAP.md','docs/TESTING.md','docs/PHASE2_MEMORY_ENVIRONMENT_REPORT.md'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
shutil.copyfile(root/'out/finish_memory_environment.py',final/'verification-scripts/finish_memory_environment.py')
report.mkdir(exist_ok=False)
write(report/'summary.json',dict(SDK_source_fingerprint=fp,production_SDK_changed=False,phase2_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False,
    new_contract_tests=14,python_tests=196,new_native_compilation=False,native_regression_rerun=False,new_long_soak=False,actual_driver_change=False,
    normal_Intel_runs=2,measured_seconds_each=3,warmup_seconds_each=1,frames=472,full_images=1416,verified_buffer_bytes=123731968,
    cases=[dict(outcome=c['outcome'],native_exit_code=c['native_exit_code'],references=c['references']) for c in cases],
    recorded_environment_comparison=load(final/'comparison.json'),same_driver_change_request_rejected=True,overwrite_controls=2,missing_adapter_rejected=True,
    initial_preflight_rejected=True,initial_failure_before_workload=True,original_policy_unchanged=True,eleven_SDK_packages_preserved=True,SDK_repackaged=False,
    earlier_evidence_preserved=True,hardware_stability_certified=False,loaded_module_identity_claim=False,SDK_package=package,commands=commands))
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|={p for p in (root/'docs').glob('*.md')}|{p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
paths|={root/'out'/p.name for p in (final/'verification-scripts').iterdir()}|{v/'initial-tool-source.py',v/'initial-verification-source.py'}
sources=[rec(p) for p in sorted(paths)];manifest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/memory-environment-{fp}-{manifest[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=fp,source_manifest_sha256=manifest,files=sources),indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
    assert len(z.namelist())==len(sources)+1
    for r in sources:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
files=[rec(p) for p in sorted(v.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.pdb','.lib','.dmp','.etl','.csv','.nupkg') for r in files+sources)
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),SDK_source_fingerprint=fp,
    summary=rec(report/'summary.json'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=preserved,
    SDK_packages=prior['SDK_packages'],previous_evidence=rec(previous)))
print(f'PASS: {len(files)} verification files, {len(sources)} verified source archive members; two real serial Intel runs; original SDK/policy/packages/evidence preserved',flush=True)
