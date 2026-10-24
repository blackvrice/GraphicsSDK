import hashlib
import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import report_vulkan_buffer_pool_fault as report_tool
import run_vulkan_buffer_pool_fault as trial
v=pathlib.Path(__file__).resolve().parent.parent;base=root/'benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool-fault';results=root/'benchmarks/results/2026-10-24-vulkan-buffer-pool-fault-v2'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(b):return hashlib.sha256(b).hexdigest()
e=load(base/'evidence.json');records=e['files']+e['source_files']+e['preserved_inputs']+e['unchanged_production_sources']+e['historical_packages']+e['local_artifacts']+[e[k] for k in ('summary','cases','source_archive','previous_evidence')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert audit.sdk_fingerprint(root)==e['SDK_source_fingerprint'] and audit.sdk_fingerprint(trial.SOURCE)==e['candidate_SDK_source_fingerprint'] and trial.fingerprint()==e['fault_source_fingerprint']
x=report_tool.review(results);assert len(x['runs'])==68
commands=load(v/'build-commands.json');assert len(commands)==10 and all(c['returncode']==0 for c in commands)
test=next(c for c in commands if c['name']=='candidate-tests');assert test['argv'][test['argv'].index('-E')+1]=='^memory_reclamation_'
for name,count in (('candidate',57),('prior',1),('fixed',1)):
    xml=ET.parse(v/(name+'-tests.xml')).getroot();assert len(xml.findall('.//testcase'))==count and not xml.findall('.//failure') and not xml.findall('.//skipped')
    assert not any('memory_reclamation_' in n.attrib.get('name','') for n in xml.findall('.//testcase'))
assert load(v/'matrix-command.json')['returncode']==0 and load(v/'matrix-command.initial.json')['returncode']!=0
assert len(load(results/'commands.json'))==77
assert 'Ran 506 tests' in (v/'python-tests.log').read_text(errors='replace') and '\nOK\n' in (v/'python-tests.log').read_text(errors='replace')
native=(v/'vulkan-buffer-pool-fault-sdk-tests.log').read_text(errors='replace');assert 'pool_range_assertions=41246 GPU_calls=0' in native and 'pool_gpu_assertions=103 devices=2 verified_bytes=164742' in native and 'pool_gpu_assertions=105 devices=2 verified_bytes=164742' in native
for variant in ('prior','fixed'):assert 'fault_plan_assertions=914 GPU_calls=0' in (v/('vulkan-buffer-pool-fault-'+variant+'-tests.log')).read_text(errors='replace')
assert 'ten altered fault reports rejected' in (v/'report-controls.log').read_text()
assert len(e['historical_packages'])==23 and sha((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
summary=load(base/'summary.json');expected=dict(positive_processes=60,guarded_regression_processes=8,fail_closed_processes=8,total_commands=77,positive_iterations=180,guarded_control_iterations=24,positive_verified_buffers=540,positive_verified_bytes=2260992,positive_injected_failures=216,guarded_invalid_frees=24,positive_invalid_frees=0,production_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False)
assert all(summary[k]==v and type(summary[k]) is type(v) for k,v in expected.items())
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    manifest=json.loads(z.read('source-manifest.json'));assert manifest['files']==e['source_files'] and sha(json.dumps(manifest['files'],sort_keys=True,separators=(',',':')).encode())==manifest['source_manifest_sha256']
if '--staged' in sys.argv:
    command=['git','-c','gc.auto=0','-c','maintenance.auto=false'];rows=subprocess.check_output([*command,'ls-files','--stage','-z'],cwd=root).decode().split('\0');ids={s.split('\t',1)[1]:s.split()[1] for s in rows if s}
    paths={r['path'] for r in e['source_files']+e['files']}|{e[k]['path'] for k in ('summary','cases','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'};pairs=[(p,ids[p]) for p in sorted(paths)]
    data=subprocess.run([*command,'cat-file','--batch'],cwd=root,input=('\n'.join(oid for _,oid in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout;position=0
    for path,oid in pairs:
        end=data.index(b'\n',position);header=data[position:end].split();assert header[:2]==[oid.encode(),b'blob'];size=int(header[2]);blob=data[end+1:end+1+size];position=end+2+size;raw=(root/path).read_bytes();assert blob==raw or blob==raw.replace(b'\r\n',b'\n'),path
print(f'PASS {len(records)} hashes / SDK 57 + consumer 2 CTests / 506 Python / 60 clean + 8 guarded + 8 fail-closed processes; ZIP/prior evidence/packages; product unchanged')
