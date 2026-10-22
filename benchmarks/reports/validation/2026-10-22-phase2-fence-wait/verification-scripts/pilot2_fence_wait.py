import pathlib,subprocess,json,sys
v=pathlib.Path('benchmarks/reports/validation/2026-10-22-phase2-fence-wait');commands=[]
for name,cmd in [('pilot2-build',['cmake','--build','out/core','--config','Release','--parallel','1','--target','gfx_fence_wait_tests']),('pilot2-gpu',['out/core/Release/gfx_fence_wait_tests.exe','--gpu']),('pilot2-validation',['out/core/Release/gfx_fence_wait_tests.exe','--validation'])]:
    print('Running '+name,flush=True)
    p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    (v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,command=cmd,returncode=p.returncode))
    (v/'pilot2-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    if p.returncode:print(p.stdout.decode(errors='replace'));sys.exit(p.returncode)
    print('PASS '+name,flush=True)
