import json
import pathlib
import subprocess
import sys
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent;args=[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py']
p=subprocess.run(args,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(v/'python-tests-final.log').write_bytes(p.stdout);(v/'python-command-final.json').write_text(json.dumps(dict(argv=args,returncode=p.returncode),indent=2)+'\n');print(p.stdout.decode(errors='replace')[-500:]);assert p.returncode==0
