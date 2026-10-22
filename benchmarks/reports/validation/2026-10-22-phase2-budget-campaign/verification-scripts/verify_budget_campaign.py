import pathlib, subprocess, sys, json, hashlib, shutil

root=pathlib.Path.cwd()
v=root/'benchmarks/reports/validation/2026-10-22-phase2-budget-campaign'
v.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint
from run_adapter_validation import validate_list
from analyze_memory_stress import read_records, analyze
from run_memory_stress import validate_native

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,value):p.write_bytes((json.dumps(value,indent=2,ensure_ascii=False)+'\n').encode())
fp=sdk_fingerprint(root)
paths={p for folder in ('include','src','tests','tools') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in ('.cpp','.hpp','.py','.json')}
paths.add(root/'CMakeLists.txt')
frozen={p.relative_to(root).as_posix():digest(p) for p in sorted(paths)}
prior=json.loads((root/'benchmarks/reports/2026-10-22-phase2-memory-budget/evidence.json').read_text())
old={r['path']:r['sha256'] for r in prior['files']+prior['SDK_packages']+[prior['summary'],prior['source_archive']]}
old.update({p.relative_to(root).as_posix():digest(p) for p in (root/'licenses/generated').rglob('*') if p.is_file()})
assert len(prior['SDK_packages'])==8
for path,sha in old.items():assert digest(root/path)==sha,path
library=digest(root/'out/core/Release/GraphicsSDKCore.lib')
write(v/'inputs-before.json',dict(SDK_source_fingerprint=fp,inputs=frozen,historical=old,library_sha256=library))
commands=[]
def run(name,cmd,expected=0,timeout=240):
    print('Running '+name,flush=True)
    p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
    (v/(name+'.log')).write_bytes(p.stdout)
    commands.append(dict(name=name,command=cmd,returncode=p.returncode,log=name+'.log'))
    write(v/'commands.json',commands)
    if p.returncode!=expected:
        print(p.stdout.decode(errors='replace'),flush=True)
        raise RuntimeError(name+': unexpected exit '+str(p.returncode))
    print('PASS '+name,flush=True)
    return p.stdout

run('core-build',['cmake','--build','out/core','--config','Release','--parallel','1','--target','gfx_memory_stress'])
run('core-ctest',['ctest','--test-dir','out/core','-C','Release','--output-on-failure'])
shutil.copyfile(root/'out/core/Testing/Temporary/LastTest.log',v/'core-native.log')
for mode in ('gpu','validation','warp'):shutil.copyfile(root/f'out/core/memory-{mode}.jsonl',v/f'core-memory-{mode}.jsonl')
run('python-tests',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py','-v'])
run('diagnostic-build',['cmake','--build','out/memory-validation-probe','--config','Release','--parallel','1'])
run('symbol-build',['cmake','--build','out/crash-capture','--config','Release','--parallel','1','--target','gfx_memory_stress_symbols'])
raw=run('adapters',['out/presentation/Release/gfx_presentation.exe','--list-adapters'])
entries=validate_list([json.loads(s) for s in raw.decode().splitlines()],fp)
write(v/'adapters.json',entries)
cases=[]
for i,entry in enumerate(entries):
    for mode in (('warp',) if entry['software'] else ('gpu','validation')):
        name=f'adapter{i}-{mode}'
        run(name,[sys.executable,'tools/run_memory_stress.py','--mode',mode,'--adapter',str(entry['adapter_id']),'--diagnostic-seconds','3','--output',str(v/name)])
        records=read_records(v/name/(mode+'.jsonl'))
        result=validate_native(records,fp,mode,entry['adapter_id'],0)
        assert records[0]['schema']==3 and not result['accepted']
        try:analyze(records,fp)
        except ValueError as e:assert str(e)=='Short smoke is not soak acceptance',str(e)
        else:raise AssertionError('Short execution accepted as soak')
        cases.append(dict(case=name,result=result))
hardware=next(e for e in entries if not e['software'])
run('guard-control',[sys.executable,'tools/run_memory_stress.py','--mode','validation','--adapter',str(hardware['adapter_id']),'--diagnostic-seconds','3','--max-private-growth-mib','1','--output',str(v/'guard-control')],expected=1)
r=read_records(v/'guard-control/validation.jsonl')
guard=validate_native(r,fp,'validation',hardware['adapter_id'],2)
assert guard['status']=='aborted' and guard['public_memory_budget']['recorded']
run('native-observer',['out/memory-validation-probe/Release/memory_validation_probe.exe','native-cpu-texture','1',str(v/'native-observer.jsonl')])
run('sdk-lifetime',['out/memory-validation-probe/Release/sdk_lifetime_probe.exe','hardware','2','2',str(v/'sdk-lifetime.jsonl')])
run('symbol-capture',[sys.executable,'tools/capture_memory_failure.py','--mode','gpu','--adapter',str(hardware['adapter_id']),'--diagnostic-seconds','2','--output',str(v/'symbol-capture')])
for profile,folder in [('sdk-stress','windows-sdk-budget-campaign-release'),('sdk-crash','windows-sdk-budget-campaign-crash-release')]:
    for check in (False,True):
        cmd=[sys.executable,'tools/audit_licenses.py','--profile',profile,'--output','licenses/generated/'+folder]
        if check:cmd+=['--check']
        run(profile+('-check' if check else '-create'),cmd)
for profile,folder in [('sdk','windows-sdk-memory-budget-release'),('sdk-budget','windows-sdk-memory-budget-validation-release'),('benchmarks-phase1','windows-benchmarks-memory-budget-release')]:
    cmd=[sys.executable,'tools/audit_licenses.py','--profile',profile,'--output','licenses/generated/'+folder,'--check']
    if profile!='benchmarks-phase1':cmd+=['--package',str(root/'out/GraphicsSDK-0.0.1-dev-windows-x64-memory-budget.zip')]
    run(profile+'-unchanged',cmd)
for path,sha in frozen.items():assert digest(root/path)==sha,path
for path,sha in old.items():assert digest(root/path)==sha,path
assert sdk_fingerprint(root)==fp and digest(root/'out/core/Release/GraphicsSDKCore.lib')==library
shutil.copyfile(v/'inputs-before.json',v/'inputs-after.json')
write(v/'results.json',dict(cases=cases,guard=guard,SDK_source_fingerprint=fp,library_unchanged=True,historical_evidence_unchanged=True,previous_SDK_packages_preserved=8,memory_acceptance=False))
print('PASS serial native/schema3 regression, frozen sources/library/historical records/eight SDK packages',flush=True)
