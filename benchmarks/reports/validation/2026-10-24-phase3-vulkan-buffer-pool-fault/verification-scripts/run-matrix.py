import json
import pathlib
import subprocess
import sys
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
args=[sys.executable,'tools/run_vulkan_buffer_pool_fault.py','--output','benchmarks/results/2026-10-24-vulkan-buffer-pool-fault-v2']
start=time.monotonic()
with (v/'matrix.log').open('wb') as f:
    p=subprocess.Popen(args,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    for line in p.stdout:f.write(line);f.flush();print(line.decode('utf-8',errors='replace').rstrip(),flush=True)
    status=p.wait()
(v/'matrix-command.json').write_text(json.dumps(dict(argv=args,returncode=status,seconds=time.monotonic()-start),indent=2)+'\n')
if status:raise RuntimeError('Fault matrix failed; preserve raw evidence')
