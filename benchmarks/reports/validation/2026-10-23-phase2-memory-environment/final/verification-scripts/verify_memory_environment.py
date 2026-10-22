import pathlib,json,hashlib,subprocess,sys,time,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-23-phase2-memory-environment/final';v.mkdir(exist_ok=False)
sys.path.insert(0,str(root/'tools'))
import memory_environment as env
from audit_licenses import sdk_fingerprint
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
for name in ('tools/memory_environment.py','tests/test_memory_environment.py'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
paths=list(env.ROLES.values())+[root/'tests/test_memory_environment.py',root/'out/verify_memory_environment.py']
frozen={str(p):sha(p) for p in paths};save(v/'inputs-before.json',frozen)
enumeration=subprocess.run([str(env.ENUMERATOR),'--list-adapters'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30);assert enumeration.returncode==0
(v/'adapters.jsonl').write_bytes(enumeration.stdout);adapters=[json.loads(s) for s in enumeration.stdout.splitlines()]
intel=[a for a in adapters if a.get('vendor_id')==0x8086 and a.get('software') is False];assert len(intel)==1;adapter=intel[0]['adapter_id']
commands=[]
def run(name,args,expected=0,timeout=180):
    started=time.monotonic()
    with (v/(name+'.log')).open('xb') as log:p=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    commands.append(dict(name=name,command=args,returncode=p.returncode,expected_exit=expected,seconds=time.monotonic()-started));save(v/'commands.json',commands)
    assert p.returncode==expected,name
    print('PASS '+name,flush=True)
python=sys.executable
run('python-tests',[python,'-m','unittest','discover','-s','tests','-p','test_*.py'])
assert 'Ran 196 tests' in (v/'python-tests.log').read_text() and 'skipped' not in (v/'python-tests.log').read_text()
for case in ('baseline','same-environment'):
    run(case,[python,'tools/memory_environment.py','record','--adapter',str(adapter),'--diagnostic-seconds','3','--output',str(v/case)],timeout=180)
    data,_,_=env.validated_case(v/case/'case.json');assert data['outcome']=='completed' and data['native_exit_code']==0 and data['references']['functional_references_passed']
run('comparison',[python,'tools/memory_environment.py','compare','--before',str(v/'baseline/case.json'),'--after',str(v/'same-environment/case.json'),'--change','none','--output',str(v/'comparison.json')])
assert load(v/'comparison.json')['recorded_controls_match']
run('missing-driver-change',[python,'tools/memory_environment.py','compare','--before',str(v/'baseline/case.json'),'--after',str(v/'same-environment/case.json'),'--change','selected-driver','--output',str(v/'missing-driver-change.json')],1)
assert not load(v/'missing-driver-change.json')['recorded_controls_match']
before=sha(v/'comparison.json')
run('comparison-overwrite',[python,'tools/memory_environment.py','compare','--before',str(v/'baseline/case.json'),'--after',str(v/'same-environment/case.json'),'--change','none','--output',str(v/'comparison.json')],1)
assert sha(v/'comparison.json')==before
before=sha(v/'baseline/case.json')
run('record-overwrite',[python,'tools/memory_environment.py','record','--adapter',str(adapter),'--diagnostic-seconds','3','--output',str(v/'baseline')],1)
assert sha(v/'baseline/case.json')==before
run('missing-adapter',[python,'tools/memory_environment.py','record','--adapter',str(2**64-1),'--diagnostic-seconds','3','--output',str(v/'missing-adapter')],1)
assert not (v/'missing-adapter').exists()
run('audit-recheck',[python,'tools/audit_licenses.py','--profile','sdk-crash','--output','licenses/generated/windows-sdk-thread-snapshot-crash-release','--check'])
run('git-prestage-fsck',['git','-c','gc.auto=0','fsck','--full','--strict'])
assert all(sha(pathlib.Path(p))==h for p,h in frozen.items()) and sdk_fingerprint(root)==fp
save(v/'inputs-after.json',frozen)
scripts=v/'verification-scripts';scripts.mkdir();shutil.copyfile(root/'out/verify_memory_environment.py',scripts/'verify_memory_environment.py')
print('PASS frozen inputs, two actual serial Intel diagnostics and comparison/CLI controls',flush=True)
