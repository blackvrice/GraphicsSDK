import json
import pathlib
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent;commands=[]
def run(name,args):
    p=subprocess.run([sys.executable,*args],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,argv=[sys.executable,*args],returncode=p.returncode));(v/'check-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(name,p.returncode,p.stdout.decode(errors='replace')[-1800:],flush=True)
    if p.returncode:raise RuntimeError(name+' failed')
run('audit-create',['tools/audit_vulkan_loader_fault.py','--create'])
run('audit-check',['tools/audit_vulkan_loader_fault.py','--check'])
run('python-tests',['-m','unittest','discover','-s','tests','-p','test_*.py'])
run('report',['tools/report_vulkan_loader_fault.py','benchmarks/results/2026-10-24-vulkan-loader-fault-v2','--output','benchmarks/reports/2026-10-24-vulkan-loader-fault/summary.json'])
