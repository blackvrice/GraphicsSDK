import argparse, datetime, hashlib, json, pathlib, subprocess, sys, time
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import run_memory_stress as normal
import capture_memory_failure as capture
import memory_environment as env
from audit_licenses import sdk_fingerprint

OUT=ROOT/'out/p2-intel-crash-resumed-2026-10-25'
ENUM=ROOT/'out/memory-resume-presentation/Release/gfx_presentation.exe'
BIN=ROOT/'out/intel-crash-resume-capture/Release'
LIB=ROOT/'out/intel-crash-resume-symbol-sdk/Release/GraphicsSDKCore.lib'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
OUT.mkdir(exist_ok=False)
raw=subprocess.check_output([str(ENUM),'--list-adapters'],cwd=ROOT);(OUT/'adapters.jsonl').write_bytes(raw)
rows=[json.loads(l) for l in raw.decode().splitlines()];fp=sdk_fingerprint(ROOT)
assert rows[-1]['sdk_fingerprint']==fp
intel=[r for r in rows if r.get('event')=='adapter' and r['vendor_id']==0x8086 and not r['software']]
assert len(intel)==1;adapter=intel[0]['adapter_id']
paths=[pathlib.Path(__file__),ENUM,normal.POLICY,ROOT/'tools/MemoryStress.cpp',pathlib.Path(normal.__file__),pathlib.Path(capture.__file__),pathlib.Path(env.__file__),ROOT/'tools/analyze_memory_stress.py',ROOT/'tools/audit_licenses.py',ROOT/'tools/crash_capture/CMakeLists.txt',ROOT/'tools/crash_capture/evidence.py',*sorted((ROOT/'tools/crash_capture').glob('*.cpp')),ROOT/'out/core/Release/GraphicsSDKCore.lib',ROOT/'out/core/Release/gfx_memory_stress.exe',LIB,LIB.with_suffix('.pdb'),*[BIN/n for n in ('gfx_crash_capture.exe','gfx_crash_capture.pdb','gfx_memory_stress_symbols.exe','gfx_memory_stress_symbols.pdb')]]
frozen={str(p):sha(p) for p in paths}
state=dict(schema=1,task='P2-ACC-02',source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
 started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),SDK_source_fingerprint=fp,adapter=intel[0],
 diagnostic_only=True,memory_acceptance=False,crash_cause_resolved=False,serialized=True,status='running',cases=[],frozen_inputs=frozen)
save(OUT/'resumption.json',state)
base_roles=dict(env.ROLES);env.ENUMERATOR=ENUM
def unchanged():return sdk_fingerprint(ROOT)==fp and all(sha(pathlib.Path(p))==h for p,h in frozen.items())
def run_case(name,kind,guard):
 assert unchanged()
 stress=ROOT/'out/core/Release/gfx_memory_stress.exe' if kind=='normal' else BIN/'gfx_memory_stress_symbols.exe'
 lib=ROOT/'out/core/Release/GraphicsSDKCore.lib' if kind=='normal' else LIB
 env.STRESS=stress;env.ROLES={**base_roles,'stress':stress,'core_library':lib,'enumerator':ENUM}
 before=env.collect_snapshot(adapter);save(OUT/(name+'-environment-before.json'),before)
 start=time.monotonic();print('BEGIN '+name+' exact_adapter='+str(adapter),flush=True)
 output=OUT/name
 if kind=='normal':
  code=normal.run(argparse.Namespace(output=output,executable=stress,mode='validation',adapter=adapter,max_private_growth_mib=guard,diagnostic_seconds=None))
  receipt=json.loads((output/'campaign.json').read_text(encoding='utf-8'));p=receipt['processes'][0];native=p['returncode']
  status='crashed' if native & 0xffffffff==0xc0000005 else 'guard_aborted' if native==2 else 'completed' if native==0 else 'native_failed'
 else:
  code=capture.run(argparse.Namespace(output=output,binary=BIN,core_library=LIB,mode='validation',adapter=adapter,max_private_growth_mib=guard,diagnostic_seconds=None,dump=OUT/(name+'.dmp')))
  receipt=json.loads((output/'summary.json').read_text(encoding='utf-8'));status=receipt['status'];native=receipt.get('collector_exit_code')
 after=env.collect_snapshot(adapter);save(OUT/(name+'-environment-after.json'),after)
 comparison=env.compare_snapshots(before,after,within_run=True);save(OUT/(name+'-environment-comparison.json'),comparison)
 row=dict(name=name,kind=kind,guard_MiB=guard,runner_exit=code,native_exit=native,status=status,wall_seconds=time.monotonic()-start,
  requested_measurement_seconds=300,warmup_seconds=30,recorded_environment_controls_match=comparison['recorded_controls_match'],inputs_unchanged=receipt['inputs_unchanged'])
 state['cases'].append(row);save(OUT/'resumption.json',state);print('END '+name+' '+status,flush=True)
 return status
try:
 for name,kind in [('normal-512','normal'),('captured-512','captured')]:
  status=run_case(name,kind,512)
  if status in ('crashed','capture_failed','timed_out','native_failed'):
   state['status']='requires_fault_analysis';break
 else:state['status']='bounded_pair_complete_without_av'
except Exception as e:state.update(status='investigation_failed',error=type(e).__name__+': '+str(e))
finally:
 state['inputs_unchanged']=unchanged();state['completed_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();save(OUT/'resumption.json',state)
 print('FINAL '+state['status'],flush=True)
sys.exit(0 if state['status']=='bounded_pair_complete_without_av' and state['inputs_unchanged'] else 1)
