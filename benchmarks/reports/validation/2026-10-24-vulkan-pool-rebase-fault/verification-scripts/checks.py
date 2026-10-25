import json
import pathlib
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent;commands=[]
for name,args in (
    ('audit-check',['tools/audit_vulkan_pool_rebase_fault.py','--check']),
    ('python-tests',['-m','unittest','discover','-s','tests','-p','test_*.py']),
    ('report',['tools/report_vulkan_pool_rebase_fault.py','benchmarks/results/2026-10-24-vulkan-pool-rebase-fault']),
):
    p=subprocess.run([sys.executable,*args],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,argv=[sys.executable,*args],returncode=p.returncode));(v/'check-commands.json').write_text(json.dumps(commands,indent=2)+'\n');print(name,p.returncode,p.stdout.decode(errors='replace')[-600:],flush=True)
    if p.returncode:raise RuntimeError(name+' failed')
