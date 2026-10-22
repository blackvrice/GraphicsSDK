import pathlib,subprocess,json,sys
v=pathlib.Path('benchmarks/reports/validation/2026-10-22-phase2-copy-atomicity');commands=[]
for name,cmd in [('pilot-build',['cmake','--build','out/core','--config','Release','--parallel','1','--target','gfx_copy_tests']),('pilot-gpu',['out/core/Release/gfx_copy_tests.exe','--gpu']),('pilot-validation',['out/core/Release/gfx_copy_tests.exe','--validation'])]:
    print('Running '+name,flush=True)
    p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    (v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,command=cmd,returncode=p.returncode))
    (v/'pilot-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    if p.returncode:print(p.stdout.decode(errors='replace'));sys.exit(p.returncode)
    print('PASS '+name,flush=True)
