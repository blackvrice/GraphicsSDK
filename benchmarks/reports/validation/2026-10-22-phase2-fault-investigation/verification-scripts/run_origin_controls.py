import pathlib,json,subprocess,sys,time,hashlib,shutil
root=pathlib.Path.cwd();out=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation';campaign=out/'campaign.json'
deadline=time.monotonic()+1800
while True:
    if json.loads(campaign.read_text()).get('completed_utc'):break
    if time.monotonic()>deadline:raise RuntimeError('Serial predecessor did not complete')
    time.sleep(2)
exe=root/'out/gbv-allocation-origin/build/Release/gbv_allocation_origin.exe';capture=root/'out/crash-capture/Release/gfx_crash_capture.exe'
control=out/'allocation-origin';control.mkdir()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
paths=[exe,exe.with_suffix('.pdb'),capture,root/'out/gbv-allocation-origin/main.cpp',root/'out/gbv-allocation-origin/CMakeLists.txt',root/'tools/memory_validation_probe/Lifetime.cpp']
frozen={str(p):sha(p) for p in paths};(control/'inputs-before.json').write_text(json.dumps(frozen,indent=2)+'\n')
for p in paths:
    if p.suffix=='.cpp' or p.name=='CMakeLists.txt':
        target=control/'source'/p.relative_to(root);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
sys.path.insert(0,str(root/'tools'))
from crash_capture.evidence import read_records,validate
cases=[]
for index in (0,1):
    for mode in ('cpu','full','no-tracking'):
        name=f'adapter-{index}-{mode}';folder=control/name;folder.mkdir()
        cmd=[str(capture),'--output',str(folder/'capture.jsonl'),'--timeout-ms','90000','--dump',str(root/'out'/f'origin-{name}.dmp'),'--',str(exe),mode,str(index),'2','256',str(folder/'memory.jsonl')]
        print('Started native causal control '+name,flush=True)
        start=time.monotonic()
        with (folder/'native.log').open('xb') as log:p=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=120)
        capture_data=read_records(folder/'capture.jsonl');result=validate(capture_data,capture_data[-1]['child_exit_code'],p.returncode)
        cases.append(dict(case=name,command=cmd,returncode=p.returncode,seconds=time.monotonic()-start,capture=result))
        (control/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
        assert p.returncode==0,name
        rows=read_records(folder/'memory.jsonl');assert rows[-1]['passed']
        print('PASS '+name,flush=True)
assert all(sha(pathlib.Path(p))==h for p,h in frozen.items())
(control/'inputs-after.json').write_text(json.dumps(frozen,indent=2)+'\n')
print('Completed six SDK-free allocation origin controls',flush=True)
