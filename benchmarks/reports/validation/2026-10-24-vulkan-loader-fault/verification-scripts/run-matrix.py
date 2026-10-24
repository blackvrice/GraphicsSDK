import json
import pathlib
import subprocess
import sys
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
args=[sys.executable,'tools/run_vulkan_loader_fault.py','--output','benchmarks/results/2026-10-24-vulkan-loader-fault-v2'];start=time.monotonic()
p=subprocess.run(args,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=600)
(v/'matrix.log').write_bytes(p.stdout);(v/'matrix-command.json').write_text(json.dumps(dict(argv=args,returncode=p.returncode,seconds=time.monotonic()-start),indent=2)+'\n')
print(p.stdout.decode(errors='replace'),flush=True)
if p.returncode:raise RuntimeError('Loader matrix failed')
