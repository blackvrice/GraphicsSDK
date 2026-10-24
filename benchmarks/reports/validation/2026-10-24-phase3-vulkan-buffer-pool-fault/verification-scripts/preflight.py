import pathlib
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
def run(name,args):
    p=subprocess.run([sys.executable,*args],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);print(p.stdout.decode('utf-8',errors='replace')[-2100:],flush=True);p.check_returncode()
old=v/'python-tests.log';old.rename(v/'python-tests.initial.log')
run('python-tests',['-m','unittest','discover','-s','tests','-p','test_*.py','-v'])
run('report-controls',[str(v/'verification-scripts/report-controls.py')])
run('audit-check',['tools/audit_vulkan_buffer_pool_fault.py','--check'])
run('write-document',[str(v/'verification-scripts/write-document.py')])
run('update-links',[str(v/'verification-scripts/update-links.py')])
