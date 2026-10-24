"""Seal this snapshot only after the sequential GPU campaign exits successfully."""
import json
import pathlib
import shutil
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
def run(name,args):
    p=subprocess.run([sys.executable,*args],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);print(p.stdout.decode('utf-8',errors='replace'),flush=True);p.check_returncode()
assert json.loads((v/'matrix-command.json').read_text())['returncode']==0
run('audit-check',['tools/audit_vulkan_adaptive_buffer_pool.py','--check'])
run('report-create',['tools/report_vulkan_adaptive_buffer_pool.py','--input','benchmarks/results/2026-10-24-vulkan-adaptive-buffer-pool','--output','benchmarks/reports/2026-10-24-phase3-vulkan-adaptive-buffer-pool'])
run('report-controls',[str(v/'verification-scripts/report-controls.py')])
shutil.copyfile(root/'out/vulkan-adaptive-buffer-pool-sdk/Testing/Temporary/LastTest.log',v/'native-tests.log')
run('write-document',[str(v/'verification-scripts/write-document.py')])
# finalize captures the logs and scripts; its own stdout and later verification are
# observations about the sealed snapshot, not inputs to a self-referential hash.
subprocess.run([sys.executable,str(v/'verification-scripts/finalize.py')],cwd=root,check=True)
subprocess.run([sys.executable,str(v/'verification-scripts/verify.py')],cwd=root,check=True)
