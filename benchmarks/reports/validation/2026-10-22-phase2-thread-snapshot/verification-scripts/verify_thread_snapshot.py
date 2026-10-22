import pathlib, hashlib, json, subprocess, sys, time
root = pathlib.Path.cwd()
v = root/'benchmarks/reports/validation/2026-10-22-phase2-thread-snapshot'
v.mkdir(exist_ok=False)
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def record(p): return dict(path=p.relative_to(root).as_posix(), sha256=digest(p), bytes=p.stat().st_size)
def write(p,x): p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
changed = [root/p for p in ('tools/crash_capture/Capture.cpp','tools/crash_capture/Fixture.cpp','tools/crash_capture/evidence.py','tools/crash_capture/validate.py','tools/capture_memory_failure.py','tests/test_crash_capture.py')]
for p in changed: p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint
fp = sdk_fingerprint(root)
assert fp == '50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
prior = root/'benchmarks/reports/2026-10-22-phase2-copy-atomicity/evidence.json'
old = json.loads(prior.read_text())
preserved = old['files']+old['SDK_packages']+[old['source_archive'],old['summary'],record(prior)]
preserved += [record(root/p) for p in ('out/core/Release/GraphicsSDKCore.lib','out/install/lib/GraphicsSDKCore.lib','out/bench/Release/gfx_bench.exe','tools/memory_stress_policy.json')]
write(v/'preserved-before.json',preserved)
inputs = [record(p) for folder in ('include','src','tools','tests') for p in sorted((root/folder).rglob('*')) if p.is_file() and p.suffix in ('.cpp','.hpp','.py','.json')]
inputs += [record(root/p) for p in ('CMakeLists.txt','tools/crash_capture/CMakeLists.txt')]
write(v/'inputs-before.json', inputs)
commands=[]
def run(label,args,expected=0):
    print('Running '+label,flush=True)
    start=time.monotonic()
    with (v/(label+'.log')).open('xb') as out:
        p=subprocess.run(args,cwd=root,stdout=out,stderr=subprocess.STDOUT)
    commands.append(dict(label=label,command=args,returncode=p.returncode,expected_exit=expected,seconds=time.monotonic()-start))
    write(v/'commands.json',commands)
    assert p.returncode==expected,(label,p.returncode)
    print('PASS '+label,flush=True)
run('build',['cmake','--build','out/crash-capture','--config','Release','--parallel','1'])
run('native-controls',[sys.executable,'tools/crash_capture/validate.py','--output',str(v/'controls')])
run('python-tests',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py'])
run('core-tests',['ctest','--test-dir','out/core','-C','Release','--output-on-failure'])
# Fresh IDs from an unchanged installed consumer, no stale index assumptions.
adapters = subprocess.check_output([str(root/'out/presentation/Release/gfx_presentation.exe'),'--list-adapters'])
(v/'adapters.jsonl').write_bytes(adapters)
rows=[json.loads(s) for s in adapters.decode().splitlines() if s.startswith('{')]
targets=[r for r in rows if r.get('event')=='adapter']
assert len(targets)==3, targets
for i,a in enumerate(targets):
    run('memory-'+str(i),[sys.executable,'tools/capture_memory_failure.py','--adapter',str(a['adapter_id']),'--mode','warp' if a['software'] else 'validation','--diagnostic-seconds','5','--output',str(v/f'memory-{i}')])
folder='licenses/generated/windows-sdk-thread-snapshot-crash-release'
for check in (False,True):
    run('audit-check' if check else 'audit-create',[sys.executable,'tools/audit_licenses.py','--profile','sdk-crash','--output',folder]+(['--check'] if check else []))
assert sdk_fingerprint(root)==fp
assert all(digest(root/r['path'])==r['sha256'] for r in preserved)
assert all(digest(root/r['path'])==r['sha256'] for r in inputs)
write(v/'inputs-after.json',inputs)
print('PASS serial controls/current consumers/regression/frozen SDK and eleven packages',flush=True)
