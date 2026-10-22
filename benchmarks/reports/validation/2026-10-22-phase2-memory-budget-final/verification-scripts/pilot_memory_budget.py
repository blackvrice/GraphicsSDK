import pathlib,subprocess,sys,json
v=pathlib.Path('benchmarks/reports/validation/2026-10-22-phase2-memory-budget-pilot');v.mkdir(parents=True,exist_ok=False);rs=[]
tasks=[('build',['cmake','--build','out/core','--config','Release','--parallel','1','--target','gfx_budget_tests']),('native-gpu',['out/core/Release/gfx_budget_tests.exe','--gpu']),('native-validation',['out/core/Release/gfx_budget_tests.exe','--validation'])]
for name,cmd in tasks:
 for attempt in range(1,4):
  print('Running '+name+str(attempt),flush=True);p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
  log=f'{name}-{attempt}.log';(v/log).write_bytes(p.stdout);rs.append({'name':name,'attempt':attempt,'command':cmd,'returncode':p.returncode,'log':log});(v/'commands.json').write_text(json.dumps(rs,indent=2)+'\n')
  if p.returncode==0:break
  if name!='build' or b'MSB6006' not in p.stdout or b'-1073741819' not in p.stdout or attempt==3:
   print(p.stdout.decode(errors='replace'));sys.exit(p.returncode)
  print('Preserved compiler AV, retrying unchanged source',flush=True)
 print('PASS '+name,flush=True)
