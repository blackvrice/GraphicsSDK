import pathlib,json,subprocess,sys,time,hashlib,shutil
root=pathlib.Path.cwd();out=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation';deadline=time.monotonic()+600
while not (out/'unmonitored-result-v2.json').exists():
    assert time.monotonic()<deadline,'Serial normal predecessor did not finish'
    time.sleep(2)
folder=out/'retention-controls';folder.mkdir()
exe=root/'out/gbv-retention-control/build/Release/gbv_retention_control.exe';capture=root/'out/crash-capture/Release/gfx_crash_capture.exe'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
paths=[exe,exe.with_suffix('.pdb'),capture,root/'out/gbv-retention-control/main.cpp',root/'out/gbv-retention-control/CMakeLists.txt',root/'tools/memory_validation_probe/Lifetime.cpp',root/'out/make_retention_control.py']
frozen={str(p):sha(p) for p in paths};(folder/'inputs-before.json').write_text(json.dumps(frozen,indent=2)+'\n')
for p in paths:
    if p.suffix in ('.cpp','.py') or p.name=='CMakeLists.txt':
        target=folder/'source'/p.relative_to(root);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
sys.path.insert(0,str(root/'tools'))
from crash_capture.evidence import read_records,validate
results=[]
for index in (0,1):
    for mode in ('full','full-drain'):
        name=f'adapter-{index}-{mode}';case=folder/name;case.mkdir()
        cmd=[str(capture),'--output',str(case/'capture.jsonl'),'--timeout-ms','120000','--dump',str(root/'out'/f'retention-{name}.dmp'),'--',str(exe),mode,str(index),'2','256',str(case/'memory.jsonl')]
        print('Started no-trim retention control '+name,flush=True);start=time.monotonic()
        with (case/'native.log').open('xb') as log:process=subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=150)
        data=read_records(case/'capture.jsonl');receipt=validate(data,data[-1]['child_exit_code'],process.returncode)
        results.append(dict(case=name,command=cmd,returncode=process.returncode,wall_seconds=time.monotonic()-start,capture=receipt));(folder/'cases.json').write_text(json.dumps(results,indent=2)+'\n')
        assert process.returncode==0,name
        native=read_records(case/'memory.jsonl');assert native[-1]['passed']
        print('PASS '+name,flush=True)
assert all(sha(pathlib.Path(p))==h for p,h in frozen.items())
(folder/'inputs-after.json').write_text(json.dumps(frozen,indent=2)+'\n')
print('Completed four no-trim alive-idle/empty-queue-drain controls',flush=True)
