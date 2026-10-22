import pathlib,json,hashlib,subprocess,sys,time,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-23-phase2-fixed-work';v.mkdir(exist_ok=False)
sys.path.insert(0,str(root/'tools'))
import run_fixed_memory as fixed
from audit_licenses import sdk_fingerprint,parse_tlog
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
for name in ('tools/run_fixed_memory.py','tests/test_fixed_memory.py','tools/memory_fixed_work/FixedWork.cpp','tools/memory_fixed_work/CMakeLists.txt'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
commands=[]
def run(name,args,expected=0,timeout=180):
    started=time.monotonic()
    with (v/(name+'.log')).open('xb') as log:p=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    commands.append(dict(name=name,command=args,returncode=p.returncode,expected_exit=expected,seconds=time.monotonic()-started));save(v/'commands.json',commands)
    assert p.returncode==expected,name
    print('PASS '+name,flush=True)
run('configure',['cmake','-S','tools/memory_fixed_work','-B','out/memory-fixed-work','-G','Visual Studio 17 2022','-A','x64'])
run('build',['cmake','--build','out/memory-fixed-work','--config','Release','--parallel','1'])
frozen=fixed.inputs();frozen[str(root/'tests/test_fixed_memory.py')]=sha(root/'tests/test_fixed_memory.py');frozen[str(root/'out/verify_fixed_memory.py')]=sha(root/'out/verify_fixed_memory.py')
save(v/'inputs-before.json',frozen);fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
run('python-tests',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py'])
assert 'Ran 208 tests' in (v/'python-tests.log').read_text() and 'skipped' not in (v/'python-tests.log').read_text()
p=subprocess.run([str(fixed.environment.ENUMERATOR),'--list-adapters'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30);assert p.returncode==0;(v/'adapters.jsonl').write_bytes(p.stdout)
adapters=[json.loads(s) for s in p.stdout.splitlines()]
intel=next(a['adapter_id'] for a in adapters if a.get('vendor_id')==0x8086 and a.get('software') is False)
nvidia=next(a['adapter_id'] for a in adapters if a.get('vendor_id')==0x10de and a.get('software') is False)
warp=next(a['adapter_id'] for a in adapters if a.get('software') is True)
results=[]
for name in ('intel-a','intel-b'):
    run(name,[sys.executable,'tools/run_fixed_memory.py','run','--adapter',str(intel),'--rounds','16','--warmup-rounds','2','--checkpoint-rounds','4','--max-seconds','60','--output',str(v/name)])
    data,_,_=fixed.load_case(v/name/'case.json');results.append(dict(case=name,references=data['references']))
for name,adapter,mode,rounds,guard,deadline,expected in (
    ('nvidia-gpu',nvidia,'gpu',16,512,60,0),('nvidia-gbv',nvidia,'validation',16,512,60,0),('warp',warp,'warp',16,512,60,0),
    ('intel-guard',intel,'validation',1000,1,60,2),('intel-deadline',intel,'validation',100000,4096,1,3)):
    path=v/(name+'.jsonl');cmd=[str(fixed.BINARY),'--adapter',str(adapter),'--mode',mode,'--rounds',str(rounds),'--warmup-rounds','2' if expected==0 else '0','--checkpoint-rounds','4','--max-seconds',str(deadline),'--max-private-growth-mib',str(guard),'--output',str(path)]
    run(name,cmd,expected);reference=fixed.validate(fixed.read_records(path),fp,expected);results.append(dict(case=name,references=reference));save(v/'cases.json',results)
run('comparison',[sys.executable,'tools/run_fixed_memory.py','compare','--before',str(v/'intel-a/case.json'),'--after',str(v/'intel-b/case.json'),'--change','none','--output',str(v/'comparison.json')])
assert load(v/'comparison.json')['recorded_controls_match'] and [p['measured_round'] for p in load(v/'comparison.json')['matched_checkpoints']]==[4,8,12,16]
run('no-driver-change',[sys.executable,'tools/run_fixed_memory.py','compare','--before',str(v/'intel-a/case.json'),'--after',str(v/'intel-b/case.json'),'--change','selected-driver','--output',str(v/'no-driver-change.json')],1)
before=sha(v/'comparison.json')
run('comparison-overwrite',[sys.executable,'tools/run_fixed_memory.py','compare','--before',str(v/'intel-a/case.json'),'--after',str(v/'intel-b/case.json'),'--change','none','--output',str(v/'comparison.json')],1);assert sha(v/'comparison.json')==before
before=sha(v/'nvidia-gbv.jsonl')
run('native-overwrite',[str(fixed.BINARY),'--adapter',str(nvidia),'--output',str(v/'nvidia-gbv.jsonl')],1);assert sha(v/'nvidia-gbv.jsonl')==before
for name,options in (('zero-rounds',['--rounds','0']),('duplicate',['--rounds','1','--rounds','2']),('warmup-bound',['--warmup-rounds','10001']),('checkpoint-bound',['--rounds','1','--checkpoint-rounds','2'])):
    path=v/(name+'.jsonl');run(name,[str(fixed.BINARY),'--adapter',str(intel),'--output',str(path),*options],1);assert not path.exists()
run('audit-recheck',[sys.executable,'tools/audit_licenses.py','--profile','sdk-crash','--output','licenses/generated/windows-sdk-thread-snapshot-crash-release','--check'])
run('git-prestage-fsck',['git','-c','gc.auto=0','fsck','--full','--strict'])
assert all(sha(pathlib.Path(p))==h for p,h in frozen.items()) and sdk_fingerprint(root)==fp;save(v/'inputs-after.json',frozen)
build=v/'build-traces';build.mkdir();logs=[p for p in (root/'out/memory-fixed-work').rglob('CL.read.1.tlog') if 'Release' in p.parts];assert len(logs)==1
own=[];system=[]
for name in parse_tlog(logs[0].read_bytes()):
    path=pathlib.Path(name)
    if path.is_relative_to(root):
        rel=path.relative_to(root).as_posix();assert rel.lower().startswith(('include/graphics/','tools/memory_fixed_work/','tools/memorystress.cpp')),rel
        own.append(dict(path=rel,sha256=sha(path)))
    else:
        low=name.lower().replace('\\','/');assert low.startswith(('c:/program files/microsoft visual studio/','c:/program files (x86)/windows kits/')) or low in ('c:/windows/system32/tzres.dll','c:/windows/globalization/sorting/sortdefault.nls'),name
        system.append(name)
trace_files=logs+[p for p in (root/'out/memory-fixed-work').rglob('link.read.1.tlog') if 'Release' in p.parts]+list((root/'out/memory-fixed-work').glob('*.vcxproj'))
for path in trace_files:
    dest=build/path.relative_to(root/'out/memory-fixed-work');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest)
save(build/'inventory.json',dict(scope='Conservative fixed-work helper compiler/linker-read closure; own MIT sources plus system Windows SDK/MSVC, no competitor dependencies or redistributed native binaries',own_compiler_inputs=own,system_input_count=len(system),system_input_paths_sha256=hashlib.sha256('\n'.join(sorted(system)).encode()).hexdigest(),core_library_sha256=sha(root/'out/core/Release/GraphicsSDKCore.lib')))
scripts=v/'verification-scripts';scripts.mkdir();shutil.copyfile(root/'out/verify_fixed_memory.py',scripts/'verify_fixed_memory.py')
print('PASS: five completed fixed-work controls, guard/deadline prefixes, aligned comparison, original SDK preserved',flush=True)
