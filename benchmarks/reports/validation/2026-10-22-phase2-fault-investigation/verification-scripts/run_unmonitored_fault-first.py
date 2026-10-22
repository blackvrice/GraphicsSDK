import pathlib,json,subprocess,time,sys
root=pathlib.Path.cwd();output=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation'
deadline=time.monotonic()+600;status=root/'out/gbv-origin-heap-status-v2.json'
while not status.exists():
    assert time.monotonic()<deadline,'Heap predecessor did not finish'
    time.sleep(2)
data=json.loads(status.read_text(encoding='utf-8-sig'));assert data['completed'] and all(p['session_stopped'] for p in data['processes'])
command=[sys.executable,'tools/run_memory_stress.py','--mode','validation','--adapter','106766','--output',str(output/'intel-unmonitored')]
print('Started normal Intel full GBV without a debugger (300s + 30s warmup)',flush=True)
with (output/'unmonitored-runner.log').open('xb') as log:p=subprocess.run(command,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=480)
manifest=json.loads((output/'intel-unmonitored/campaign.json').read_text());assert manifest['inputs_unchanged']
(output/'unmonitored-result.json').write_text(json.dumps(dict(command=command,runner_exit=p.returncode,campaign=manifest),indent=2)+'\n')
print('Unmonitored native exits: '+str([(r['mode'],r['returncode']) for r in manifest['processes']]),flush=True)
