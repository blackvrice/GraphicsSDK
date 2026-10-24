import json
import pathlib
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent;commands=[]
for name,args in (
    ('python-tests-final',['-m','unittest','discover','-s','tests','-p','test_*.py']),
    ('report-final',['tools/report_vulkan_loader_fault.py','benchmarks/results/2026-10-24-vulkan-loader-fault-v2']),
):
    p=subprocess.run([sys.executable,*args],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(v/(name+'.log')).write_bytes(p.stdout)
    commands.append(dict(name=name,argv=[sys.executable,*args],returncode=p.returncode));(v/'final-check-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(name,p.returncode,p.stdout.decode(errors='replace')[-500:],flush=True)
    if p.returncode:raise RuntimeError(name+' failed')
