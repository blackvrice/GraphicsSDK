import pathlib,subprocess,sys,json,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-crash-capture-final'
receipts=json.loads((v/'commands.json').read_text())
def run(name,command,expected=0):
 print('Running '+name,flush=True);p=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=300)
 (v/(name+'.log')).write_bytes(p.stdout);receipts.append({'name':name,'command':command,'returncode':p.returncode,'expected':expected})
 (v/'commands.json').write_text(json.dumps(receipts,indent=2)+'\n');assert p.returncode==expected,(name,p.stdout.decode(errors='replace'));print('PASS '+name,flush=True)
s=json.loads((v/'intel-full-gbv/summary.json').read_text());assert s['capture_complete'] and s['inputs_unchanged'] and s['memory_acceptance'] is False,s
# The initial verification harness expected a rejected run. A completed diagnostic
# remains valid capture evidence; it is not upgraded to normal memory acceptance.
(v/'verification-expectation.json').write_text(json.dumps({'observed_Intel_status':s['status'],'original_harness_expected_runner_exit':1,'actual_runner_exit':receipts[-1]['returncode'],'capture_complete':True,'memory_acceptance':False},indent=2)+'\n')
for entry in json.loads((v/'adapters.json').read_text()):
 mode='warp' if entry['software'] else 'gpu'
 run('diagnostic-'+str(entry['adapter_id']),[sys.executable,'tools/capture_memory_failure.py','--mode',mode,'--adapter',str(entry['adapter_id']),'--diagnostic-seconds','2','--output',str(v/('diagnostic-'+str(entry['adapter_id'])))])
run('core-ctest',['ctest','--test-dir','out/core','-C','Release','--output-on-failure'])
shutil.copyfile(root/'out/core/Testing/Temporary/LastTest.log',v/'core-native.log')
for mode in ('gpu','validation','warp'):shutil.copyfile(root/f'out/core/memory-{mode}.jsonl',v/f'core-memory-{mode}.jsonl')
run('python-tests-final',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py','-v'])
print('PASS final capture evidence, three exact-adapter instrumented checks, core regressions and Python',flush=True)
