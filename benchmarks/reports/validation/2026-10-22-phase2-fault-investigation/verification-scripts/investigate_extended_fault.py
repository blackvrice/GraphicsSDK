import time,pathlib
spaced_done=pathlib.Path.cwd()/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/spaced-controls/inputs-after.json'
spaced_deadline=time.monotonic()+300
while not spaced_done.exists():
    assert time.monotonic()<spaced_deadline,'Serial native predecessor did not finish'
    time.sleep(2)
import pathlib,sys,json,hashlib,subprocess,time,datetime
root=pathlib.Path.cwd();out=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/extended-intel';out.mkdir(exist_ok=False)
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint
from crash_capture.evidence import read_records,validate
from run_memory_stress import validate_native
from analyze_memory_stress import analyze
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
enumeration=subprocess.check_output([str(root/'out/presentation/Release/gfx_presentation.exe'),'--list-adapters']);(out/'adapters.jsonl').write_bytes(enumeration)
adapters=[json.loads(s) for s in enumeration.decode().splitlines() if s.startswith('{"event":"adapter",')]
intel=next(a for a in adapters if a['vendor_id']==32902 and not a['software'])
capture=root/'out/crash-capture/Release/gfx_crash_capture.exe';normal=root/'out/core/Release/gfx_memory_stress.exe';symbols=root/'out/crash-capture/Release/gfx_memory_stress_symbols.exe'
fp=sdk_fingerprint(root)
frozen={str(p):sha(p) for p in [capture,normal,symbols,capture.with_suffix('.pdb'),symbols.with_suffix('.pdb'),root/'out/core/Release/GraphicsSDKCore.lib',root/'tools/MemoryStress.cpp',root/'tools/memory_stress_policy.json',root/'tools/crash_capture/Capture.cpp',pathlib.Path(__file__)]}
assert all(fp.encode() in p.read_bytes() for p in [normal,symbols])
save(out/'inputs-before.json',frozen)
manifest=dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),SDK_source_fingerprint=fp,adapter=intel,serial=True,memory_acceptance=False,crash_cause_resolved=False,cases=[])
save(out/'campaign.json',manifest)
for label,exe in [('intel-normal-extended',normal)]:
    assert all(sha(pathlib.Path(p))==h for p,h in frozen.items())
    folder=out/label;folder.mkdir();dump=root/'out'/f'{label}-fault-investigation.dmp';assert not dump.exists()
    command=[str(capture),'--output',str(folder/'capture.jsonl'),'--timeout-ms','720000','--dump',str(dump),'--',str(exe),'--mode','validation','--adapter',str(intel['adapter_id']),'--seconds','600','--warmup','30','--interval','5','--max-private-growth-mib','1024','--output',str(folder/'memory.jsonl')]
    case=dict(case=label,command=command,status='running');manifest['cases'].append(case);save(out/'campaign.json',manifest)
    print('Started '+label,flush=True);start=time.monotonic()
    with (folder/'native.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=root,stdout=log,stderr=subprocess.STDOUT);case['collector_pid']=process.pid;save(out/'campaign.json',manifest)
        try:code=process.wait(timeout=780)
        except subprocess.TimeoutExpired:process.kill();process.wait();case.update(status='outer_timeout',returncode=process.returncode);save(out/'campaign.json',manifest);raise
    case.update(returncode=code,wall_seconds=time.monotonic()-start)
    data=read_records(folder/'capture.jsonl');receipt=validate(data,data[-1]['child_exit_code'],code);case['capture']=receipt
    case['status']='crashed' if receipt['second_chance_count'] else 'timed_out' if receipt['timed_out'] else 'guard_aborted' if code==2 else 'completed' if code==0 else 'failed'
    if code in (0,2) and not receipt['second_chance_count'] and not receipt['timed_out']:
        records=read_records(folder/'memory.jsonl');case['references']=validate_native(records,fp,'validation',intel['adapter_id'],code)
        try:case['memory_analysis']=analyze(records,fp)
        except ValueError as e:case['memory_analysis']=dict(accepted=False,error=str(e))
    if dump.exists():case['local_dump']=dict(path=str(dump),sha256=sha(dump),bytes=dump.stat().st_size,redistributed=False)
    case['module_files']=[dict(path=p,sha256=sha(pathlib.Path(p))) for p in sorted({r['path'] for r in data if r.get('event')=='module' and r.get('path')}) if pathlib.Path(p).is_file()]
    save(out/'campaign.json',manifest);print(label+': '+case['status']+' exit='+str(code),flush=True)
    if case['status']=='crashed':break
assert sdk_fingerprint(root)==fp and all(sha(pathlib.Path(p))==h for p,h in frozen.items())
save(out/'inputs-after.json',frozen);manifest['inputs_unchanged']=True;manifest['completed_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save(out/'campaign.json',manifest)
print('Completed frozen serial crash reproduction campaign',flush=True)
