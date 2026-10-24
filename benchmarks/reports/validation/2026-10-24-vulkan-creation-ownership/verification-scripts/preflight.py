import pathlib
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
def run(name,args):
    p=subprocess.run([sys.executable,*args],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);print(p.stdout.decode('utf-8',errors='replace')[-2500:],flush=True);p.check_returncode()
run('report-controls',[str(v/'verification-scripts/report-controls.py')])
run('audit-check',['tools/audit_vulkan_creation_ownership.py','--check'])
run('write-document',[str(v/'verification-scripts/write-document.py')])
run('update-links',[str(v/'verification-scripts/update-links.py')])
