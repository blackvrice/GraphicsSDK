import json
import pathlib
import subprocess
root=pathlib.Path(__file__).resolve().parents[5];base=root/'benchmarks/reports/2026-10-24-vulkan-creation-ownership'
e=json.loads((base/'evidence.json').read_text(encoding='utf-8'));command=['git','-c','gc.auto=0','-c','maintenance.auto=false']
assert subprocess.check_output([*command,'branch','--show-current'],cwd=root,text=True).strip()=='main'
assert not subprocess.check_output([*command,'diff','--cached','--name-only'],cwd=root).strip()
paths={r['path'] for r in e['files']+e['source_files'] if not r['path'].startswith('out/')}|{e[k]['path'] for k in ('summary','cases','source_archive')}|{(base/'evidence.json').relative_to(root).as_posix()}
paths.discard('tests/VulkanAllocationBenchmarkTests.cpp');archive=e['source_archive']['path'];paths.discard(archive);paths=sorted(paths)
for i in range(0,len(paths),60):subprocess.run([*command,'add','--',*paths[i:i+60]],cwd=root,check=True)
subprocess.run([*command,'add','-f','--',archive],cwd=root,check=True);subprocess.run([*command,'diff','--cached','--check'],cwd=root,check=True)
staged=subprocess.check_output([*command,'diff','--cached','--name-only'],cwd=root,text=True).splitlines();assert 'tests/VulkanAllocationBenchmarkTests.cpp' not in staged and '.idea/editor.xml' not in staged
print('PASS staged',len(staged),'product-fix files; unrelated local changes preserved')
