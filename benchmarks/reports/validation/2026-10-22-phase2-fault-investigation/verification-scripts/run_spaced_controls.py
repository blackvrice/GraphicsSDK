import pathlib,json,subprocess,time,hashlib,shutil
root=pathlib.Path.cwd();base=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/spaced-controls';base.mkdir(exist_ok=False)
exe=root/'out/gbv-spaced-control/build/Release/gbv_spaced_control.exe'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
paths=[exe,exe.with_suffix('.pdb'),root/'out/gbv-spaced-control/main.cpp',root/'out/gbv-spaced-control/CMakeLists.txt',root/'tools/memory_validation_probe/Lifetime.cpp',root/'out/make_spaced_control.py']
frozen={str(p):sha(p) for p in paths};(base/'inputs-before.json').write_text(json.dumps(frozen,indent=2)+'\n')
for p in paths:
    if p.suffix in ('.cpp','.py') or p.name=='CMakeLists.txt':
        target=base/'source'/p.relative_to(root);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
results=[]
for index in (0,1):
    path=base/f'adapter-{index}.jsonl';command=[str(exe),'full',str(index),'2','256',str(path)]
    print('Started unmonitored spaced native control adapter '+str(index),flush=True);start=time.monotonic()
    with (base/f'adapter-{index}.log').open('xb') as log:process=subprocess.run(command,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=100)
    results.append(dict(adapter_index=index,command=command,returncode=process.returncode,wall_seconds=time.monotonic()-start));(base/'cases.json').write_text(json.dumps(results,indent=2)+'\n')
    assert process.returncode==0
    data=[json.loads(s) for s in path.read_text(encoding='utf-8').splitlines()];assert data[-1]['passed']
    samples=[r for r in data if r['type']=='sample'];quiet=[r for r in samples if r['phase']=='between_batch_quiet']
    assert len(quiet)==40 and quiet[19]['batches']==1 and quiet[39]['batches']==2
    assert samples[-1]['resources_created']==samples[-1]['resources_destroyed']==1536 and samples[-1]['devices_destroyed']==1 and samples[-1]['validation_errors']==0
    print('PASS spaced adapter '+str(index)+' private after quiet batches='+str([quiet[19]['private_bytes'],quiet[39]['private_bytes']]),flush=True)
assert all(sha(pathlib.Path(p))==h for p,h in frozen.items())
(base/'inputs-after.json').write_text(json.dumps(frozen,indent=2)+'\n')
