import hashlib
import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import run_vulkan_buffer_pool as trial
import report_vulkan_buffer_pool as report_tool
v=root/'benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool';base=root/'benchmarks/reports/2026-10-24-phase3-vulkan-buffer-pool';results=root/'benchmarks/results/2026-10-24-vulkan-buffer-pool'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(b):return hashlib.sha256(b).hexdigest()
e=load(base/'evidence.json');records=e['files']+e['source_files']+e['preserved_inputs']+e['unchanged_production_sources']+e['historical_packages']+e['local_artifacts']+[e[k] for k in ('summary','metrics','source_archive','previous_evidence')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert audit.sdk_fingerprint(root)==e['SDK_source_fingerprint'] and trial.source_ids()['candidate']==e['candidate_SDK_source_fingerprint']
assert trial.b.fingerprint()==e['benchmark_source_fingerprint'] and trial.t.fingerprint()==e['trace_source_fingerprint']
x=report_tool.review(results);assert len(x['runs'])==48 and len(x['traces'])==8 and x['verified_buffers']==654654 and x['verified_bytes']==2681462784
assert sum(r['result']['verified_buffers'] for r in x['traces'])==16016 and sum(r['result']['verified_bytes'] for r in x['traces'])==65601536
commands=load(v/'build-commands.json');assert len(commands)==8 and all(c['returncode']==0 for c in commands)
test=next(c for c in commands if c['name']=='candidate-tests');assert test['argv'][test['argv'].index('-E')+1]=='^memory_reclamation_'
xml=ET.parse(v/'candidate-tests.xml').getroot();assert len(xml.findall('.//testcase'))==57 and not xml.findall('.//failure') and not xml.findall('.//skipped')
assert not any('memory_reclamation_' in n.attrib.get('name','') for n in xml.findall('.//testcase'))
assert load(v/'matrix-command.json')['returncode']==0 and len(load(results/'commands.json'))==70
assert 'Ran 444 tests' in (v/'python-tests.log').read_text(errors='replace') and '\nOK\n' in (v/'python-tests.log').read_text(errors='replace')
native=(v/'native-tests.log').read_text(errors='replace');assert 'pool_range_assertions=34810 GPU_calls=0' in native and 'pool_gpu_assertions=57 devices=2 verified_bytes=164742' in native and 'pool_gpu_assertions=59 devices=2 verified_bytes=164742' in native
assert 'eight altered candidate reports rejected' in (v/'report-controls.log').read_text()
assert len(e['historical_packages'])==23 and sha((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
summary=load(base/'summary.json');assert summary['measurement_iterations']==1080 and summary['CPU_interval_observations']==2160 and summary['production_adopted'] is False
assert summary['requested_bytes_1_candidate']==16*summary['requested_bytes_1_baseline'] and summary['requested_bytes_1000_candidate']-summary['requested_bytes_1000_baseline']==32768
for row in x['groups']:
    if row['scenario']=='buffer1000':assert all(row['candidate_over_baseline_p50_ratio'][c]<1 for c in trial.b.COLUMNS)
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    manifest=json.loads(z.read('source-manifest.json'));assert manifest['files']==e['source_files'] and sha(json.dumps(manifest['files'],sort_keys=True,separators=(',',':')).encode())==manifest['source_manifest_sha256']
if '--staged' in sys.argv:
    command=['git','-c','gc.auto=0','-c','maintenance.auto=false'];rows=subprocess.check_output([*command,'ls-files','--stage','-z'],cwd=root).decode().split('\0');ids={s.split('\t',1)[1]:s.split()[1] for s in rows if s}
    paths={r['path'] for r in e['source_files']+e['files']}|{e[k]['path'] for k in ('summary','metrics','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'};pairs=[(p,ids[p]) for p in sorted(paths)]
    data=subprocess.run([*command,'cat-file','--batch'],cwd=root,input=('\n'.join(oid for _,oid in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout;position=0
    for path,oid in pairs:
        end=data.index(b'\n',position);header=data[position:end].split();assert header[:2]==[oid.encode(),b'blob'];size=int(header[2]);blob=data[end+1:end+1+size];position=end+2+size;raw=(root/path).read_bytes();assert blob==raw or blob==raw.replace(b'\r\n',b'\n'),path
print(f'PASS {len(records)} hashes / 57 CTests / 444 Python tests / 48 performance-correctness + 8 trace processes; source ZIP, prior evidence and 23 packages; production unchanged')
