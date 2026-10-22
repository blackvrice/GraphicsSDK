import pathlib,sys,subprocess,json,hashlib,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-memory-campaign';v.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
sources=['tools/MemoryStress.cpp','tools/memory_validation_probe/main.cpp','tools/run_memory_stress.py','tools/analyze_memory_stress.py','tools/memory_stress_policy.json','tests/test_memory_campaign.py']
frozen={p:digest(root/p) for p in sources}
(v/'inputs-before.json').write_text(json.dumps({'sdk_fingerprint':sdk_fingerprint(root),'inputs':frozen},indent=2)+'\n')
receipts=[]
def run(name,command,expected=0,timeout=300):
 print('Running '+name,flush=True)
 p=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
 (v/(name+'.log')).write_bytes(p.stdout)
 receipts.append({'name':name,'command':command,'returncode':p.returncode,'expected':expected})
 (v/'commands.json').write_text(json.dumps(receipts,indent=2)+'\n')
 if p.returncode!=expected:
  print(p.stdout.decode(errors='replace'));raise RuntimeError(name)
 print('PASS '+name,flush=True);return p
run('core-build',['cmake','--build','out/core','--config','Release','--parallel','1','--target','gfx_memory_stress'])
run('diagnostic-build',['cmake','--build','out/memory-validation-probe','--config','Release','--parallel','1'])
run('core-ctest',['ctest','--test-dir','out/core','-C','Release','--output-on-failure'])
shutil.copyfile(root/'out/core/Testing/Temporary/LastTest.log',v/'core-native.log')
for mode in ('gpu','validation','warp'):shutil.copyfile(root/f'out/core/memory-{mode}.jsonl',v/f'ctest-{mode}.jsonl')
run('python-tests',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py','-v'])
p=run('enumeration',[str(root/'out/presentation/Release/gfx_presentation.exe'),'--list-adapters'])
entries=[json.loads(line) for line in p.stdout.decode().splitlines() if json.loads(line)['event']=='adapter']
(v/'adapters.json').write_text(json.dumps(entries,indent=2)+'\n')
for i,entry in enumerate(entries):
 mode='warp' if entry['software'] else 'gpu';identity=entry['adapter_id']
 run(f'diagnostic-{i}',[sys.executable,'tools/run_memory_stress.py','--mode',mode,'--adapter',str(identity),'--diagnostic-seconds','3','--output',str(v/f'diagnostic-{i}')])
 if not entry['software']:
  run(f'gbv-guard-{i}',[sys.executable,'tools/run_memory_stress.py','--mode','validation','--adapter',str(identity),'--output',str(v/f'gbv-guard-{i}')],1)
  a=json.loads((v/f'gbv-guard-{i}'/'acceptance.json').read_text());assert not a['accepted'] and a['campaigns'][0]['status']=='aborted'
exe=str(root/'out/core/Release/gfx_memory_stress.exe');ids={e['adapter_id'] for e in entries};missing=2**64-1
while missing in ids:missing-=1
hardware=next(e['adapter_id'] for e in entries if not e['software'])
for name,flags in [('missing',['--adapter',str(missing)]),('wrong-kind',['--mode','warp','--adapter',str(hardware)]),('malformed',['--adapter','1x']),('overflow',['--adapter',str(2**64)]),('duplicate-zero',['--adapter','0','--adapter','1']),('negative',['--seconds','-1']),('trailing',['--seconds','2x']),('wrapping',['--seconds',str(2**32+2)]),('guard-zero',['--max-private-growth-mib','0']),('guard-overflow',['--max-private-growth-mib',str(2**64)])]:
 path=v/(name+'.jsonl');run('negative-'+name,[exe,'--seconds','2','--output',str(path),*flags],1);assert not path.exists()
path=v/'preexisting.jsonl';path.write_bytes(b'preserve original\n')
run('negative-existing',[exe,'--seconds','2','--output',str(path)],1);assert path.read_bytes()==b'preserve original\n'
run('native-cpu-observer',[str(root/'out/memory-validation-probe/Release/memory_validation_probe.exe'),'native-cpu-texture','1',str(v/'native-cpu-observer.jsonl')])
for mode in ('hardware','warp'):
 run('sdk-lifetime-'+mode,[str(root/'out/memory-validation-probe/Release/sdk_lifetime_probe.exe'),mode,'2','1',str(v/('sdk-lifetime-'+mode+'.jsonl'))])
assert frozen=={p:digest(root/p) for p in sources}
(v/'inputs-after.json').write_bytes((v/'inputs-before.json').read_bytes())
print('PASS exact selection, default guard failures, native observer compatibility, original policy unchanged',flush=True)
