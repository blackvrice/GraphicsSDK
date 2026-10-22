import pathlib,subprocess,sys,json,shutil,hashlib
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-crash-capture'
receipts=[]
def run(name,command,expected=0,timeout=600):
 print('Running '+name,flush=True);p=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
 (v/(name+'.log')).write_bytes(p.stdout);receipts.append({'name':name,'command':command,'returncode':p.returncode,'expected':expected})
 (v/'commands.json').write_text(json.dumps(receipts,indent=2)+'\n')
 if p.returncode!=expected:print(p.stdout.decode(errors='replace'));raise RuntimeError(name)
 print('PASS '+name,flush=True);return p
run('final-build',['cmake','--build','out/crash-capture','--config','Release','--parallel','1'])
run('native-controls',[sys.executable,'tools/crash_capture/validate.py','--output',str(root/'out/crash-capture-controls-final')])
controls=v/'native-controls';controls.mkdir()
for p in (root/'out/crash-capture-controls-final').iterdir():
 if p.is_file() and p.suffix in ('.jsonl','.json','.log'):shutil.copyfile(p,controls/p.name)
run('python-tests',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py','-v'])
listed=run('enumeration',[str(root/'out/presentation/Release/gfx_presentation.exe'),'--list-adapters'])
entries=[json.loads(s) for s in listed.stdout.decode().splitlines() if json.loads(s)['event']=='adapter']
(v/'adapters.json').write_text(json.dumps(entries,indent=2)+'\n')
intel=next(e for e in entries if e['vendor_id']==0x8086 and not e['software'])
# Current fingerprint and symbols are frozen by the wrapper. No validation reduction.
run('intel-full-gbv',[sys.executable,'tools/capture_memory_failure.py','--adapter',str(intel['adapter_id']),'--output',str(v/'intel-full-gbv'),'--dump',str(root/'out/intel-gbv-crash-capture.dmp')],1,600)
s=json.loads((v/'intel-full-gbv/summary.json').read_text());assert s['capture_complete'] and s['inputs_unchanged'],s
print('Intel diagnostic: '+s['status']+'; complete debugger receipt; memory acceptance remains false',flush=True)
for entry in entries:
 mode='warp' if entry['software'] else 'gpu'
 run('diagnostic-'+str(entry['adapter_id']),[sys.executable,'tools/capture_memory_failure.py','--mode',mode,'--adapter',str(entry['adapter_id']),'--diagnostic-seconds','2','--output',str(v/('diagnostic-'+str(entry['adapter_id'])))])
print('PASS crash capture native controls and actual exact-adapter instrumented workload checks',flush=True)
