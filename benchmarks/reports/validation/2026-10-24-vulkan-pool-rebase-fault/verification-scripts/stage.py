import json
import pathlib
import subprocess
root=pathlib.Path(__file__).resolve().parents[5];e=json.loads((root/'benchmarks/reports/2026-10-24-vulkan-pool-rebase-fault/evidence.json').read_text(encoding='utf-8'))
paths={r['path'] for r in e['source_files']+e['files'] if not r['path'].startswith('out/')}|{e[k]['path'] for k in ('summary','cases','source_archive')}|{'benchmarks/reports/2026-10-24-vulkan-pool-rebase-fault/evidence.json'}
paths.discard('tests/VulkanAllocationBenchmarkTests.cpp')
cmd=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false','add','-f','--pathspec-from-file=-','--pathspec-file-nul']
p=subprocess.run(cmd,cwd=root,input=('\0'.join(sorted(paths))+'\0').encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
print(f'PASS batch staged {len(paths)} exact own source/evidence paths; unrelated changes excluded')
