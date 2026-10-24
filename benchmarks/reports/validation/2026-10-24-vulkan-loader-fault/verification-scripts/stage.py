import json
import pathlib
import subprocess
root=pathlib.Path(__file__).resolve().parents[5];e=json.loads((root/'benchmarks/reports/2026-10-24-vulkan-loader-fault/evidence.json').read_text(encoding='utf-8'))
cmd=['git','-c','gc.auto=0','-c','maintenance.auto=false']
paths={r['path'] for r in e['source_files']+e['files'] if not r['path'].startswith('out/')}|{e['summary']['path'],'benchmarks/reports/2026-10-24-vulkan-loader-fault/evidence.json'}
paths.discard('tests/VulkanAllocationBenchmarkTests.cpp')
for p in sorted(paths):subprocess.run([*cmd,'add','--',p],cwd=root,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
subprocess.run([*cmd,'add','-f','--',e['source_archive']['path']],cwd=root,check=True)
print('PASS staged exact evidence/source paths; preexisting unrelated files excluded')
