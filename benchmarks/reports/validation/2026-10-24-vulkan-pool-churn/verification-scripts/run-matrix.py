import json
import pathlib
import subprocess
import sys
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
args=[sys.executable,'tools/run_vulkan_pool_churn.py','--output','benchmarks/results/2026-10-24-vulkan-pool-churn'];start=time.monotonic()
p=subprocess.run(args,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=900)
(v/'matrix.log').write_bytes(p.stdout);(v/'matrix-command.json').write_text(json.dumps(dict(argv=args,returncode=p.returncode,seconds=time.monotonic()-start),indent=2)+'\n');print(p.stdout.decode(errors='replace'),flush=True)
if p.returncode:raise RuntimeError('Churn matrix failed')
