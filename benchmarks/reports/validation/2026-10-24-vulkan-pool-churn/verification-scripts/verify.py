import hashlib
import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import report_vulkan_pool_churn as report
import run_vulkan_pool_churn as native
import vulkan_creation_provenance as history
v=pathlib.Path(__file__).resolve().parent.parent;base=root/'benchmarks/reports/2026-10-24-vulkan-pool-churn';results=root/'benchmarks/results/2026-10-24-vulkan-pool-churn'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(b):return hashlib.sha256(b).hexdigest()
e=load(base/'evidence.json');records=e['files']+e['source_files']+e['preserved_inputs']+e['historical_packages']+e['local_artifacts']+[e[k] for k in ('summary','observations','source_archive','previous_evidence','memory_policy')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert e['production_adopted'] is False and audit.sdk_fingerprint(root)==native.SDK==e['SDK_source_fingerprint'] and audit.sdk_fingerprint(native.SOURCE)==native.CANDIDATE==e['candidate_SDK_source_fingerprint']
assert audit.sdk_fingerprint(root/history.SNAPSHOT)==history.OLD
s=load(base/'summary.json');actual=report.review(results);assert all(native.same(s[k],value) for k,value in actual.items())
expected=dict(clean_processes=24,fail_closed_processes=8,total_processes=33,injected_bind_errors=224,verified_buffers=35392,verified_bytes=107417152,native_buffers_created=75840,native_buffers_destroyed=75840,native_allocations=68476,native_frees=68476,native_allocated_bytes=249273600,native_freed_bytes=249273600,checkpoint_count=1080,model_checks=22968,assertions=118960,Python_tests=581,candidate_selected_CTests=55)
assert all(type(s[k]) is int and s[k]==value for k,value in expected.items())
assert s['performance_remeasured'] is False and s['physical_memory_reclamation_verified'] is False and s['product_CTests_rerun'] is False
xml=ET.parse(v/'candidate-tests.xml').getroot();cases=xml.findall('.//testcase');assert len(cases)==55 and not xml.findall('.//failure') and not xml.findall('.//skipped') and not any('memory_reclamation_' in c.attrib.get('name','') for c in cases)
build=load(v/'sdk-build-commands.json');assert len(build)==4 and all(c['returncode']==0 for c in build);test=next(c for c in build if c['name']=='candidate-tests');assert test['argv'][test['argv'].index('-E')+1]=='^memory_reclamation_'
consumers=load(v/'consumer-build-commands-v2.json');assert len(consumers)==6 and all(c['returncode']==0 for c in consumers)
for variant in ('product','candidate'):assert 'churn model assertions=172 GPU_calls=0' in (v/(f'vulkan-pool-churn-{variant}-v2-native-tests.log')).read_text(errors='replace')
text=(v/'python-tests.log').read_text(errors='replace');assert 'Ran 581 tests' in text and '\nOK\n' in text and 'skipped=' not in text
assert all(c['returncode']==0 for c in load(v/'check-commands.json')) and load(v/'matrix-command.json')['returncode']==0
assert '12 altered churn matrices/raw reports rejected' in (v/'report-controls.log').read_text() and 'all unmodified relocated baselines passed' in (v/'report-controls.log').read_text()
receipt=load(native.SOURCE/'candidate-receipt.json');assert receipt['product_SDK_source_fingerprint']==native.SDK and receipt['prior_candidate_SDK_source_fingerprint']==native.prepare.PRIOR
assert len(e['historical_packages'])==23 and e['memory_policy']['sha256']=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
for name in ('BufferPool.hpp','BufferPoolRange.hpp'):assert (native.SOURCE/'src/backends/vulkan'/name).read_bytes()==(root/'out/vulkan-buffer-pool-fault-source/src/backends/vulkan'/name).read_bytes()
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1 and len(set(z.namelist()))==len(z.namelist())
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    m=json.loads(z.read('source-manifest.json'));assert m['files']==e['source_files'] and sha(json.dumps(m['files'],sort_keys=True,separators=(',',':')).encode())==m['source_manifest_sha256']
    assert not any(n.startswith('.deps/') or pathlib.Path(n).suffix.lower() in ('.exe','.dll','.lib','.pdb') for n in z.namelist())
if '--staged' in sys.argv:
    cmd=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false'];rows=subprocess.check_output([*cmd,'ls-files','--stage','-z'],cwd=root).decode().split('\0');ids={s.split('\t',1)[1]:s.split()[1] for s in rows if s}
    paths={r['path'] for r in e['source_files']+e['files'] if not r['path'].startswith('out/')}|{e[k]['path'] for k in ('summary','observations','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'};paths.discard('tests/VulkanAllocationBenchmarkTests.cpp')
    pairs=[(p,ids[p]) for p in sorted(paths)];data=subprocess.run([*cmd,'cat-file','--batch'],cwd=root,input=('\n'.join(oid for _,oid in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout;position=0
    for path,oid in pairs:
        end=data.index(b'\n',position);header=data[position:end].split();assert header[:2]==[oid.encode(),b'blob'];size=int(header[2]);blob=data[end+1:end+1+size];position=end+2+size;raw=(root/path).read_bytes();assert blob==raw or blob==raw.replace(b'\r\n',b'\n'),path
    changed=subprocess.check_output([*cmd,'diff','--cached','--name-only','-z'],cwd=root).decode().split('\0');assert not any(p.startswith(('.idea/','out/','.deps/')) or p=='tests/VulkanAllocationBenchmarkTests.cpp' for p in changed if p)
print(f'PASS {len(records)} hashes / candidate 55 CTests / 581 Python / 172 CPU model / 24 clean + 8 controls; 224 bind errors, 35392 readbacks, conserved ownership and immutable history')
