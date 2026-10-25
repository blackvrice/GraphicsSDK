import hashlib
import json
import pathlib
import subprocess
import sys
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import report_vulkan_pool_rebase_fault as report
import run_vulkan_pool_rebase_fault as native
import vulkan_creation_provenance as history
v=pathlib.Path(__file__).resolve().parent.parent;base=root/'benchmarks/reports/2026-10-24-vulkan-pool-rebase-fault';results=root/'benchmarks/results/2026-10-24-vulkan-pool-rebase-fault'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(b):return hashlib.sha256(b).hexdigest()
e=load(base/'evidence.json');records=e['files']+e['source_files']+e['preserved_inputs']+e['historical_packages']+e['local_artifacts']+[e[k] for k in ('summary','cases','source_archive','previous_evidence','memory_policy')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert e['production_adopted'] is False and audit.sdk_fingerprint(root)==native.SDK==e['SDK_source_fingerprint'] and audit.sdk_fingerprint(native.SOURCE)==native.CANDIDATE==e['candidate_SDK_source_fingerprint']
assert audit.sdk_fingerprint(root/history.SNAPSHOT)==history.OLD
s=load(base/'summary.json');actual=report.review(results);assert all(native.same(s[k],value) for k,value in actual.items())
expected=dict(clean_processes=88,regression_processes=60,mixed_context_processes=28,total_commands=103,cycles=264,injections=324,allocation_oom=228,unknown_errors=24,bind_errors=72,verified_buffers=1200,verified_bytes=4975104,assertions=4104,buffers_created=2844,buffers_destroyed=2844,allocation_attempts=2916,allocation_successes=2664,native_frees=2664,allocated_bytes=11768832,freed_bytes=11768832,Python_tests=614,CPU_plan_assertions=914)
assert all(type(s[k]) is int and s[k]==value for k,value in expected.items())
assert s['SDK_CTests_rerun'] is False and s['prior_candidate_selected_CTests']==55 and s['production_adopted'] is False and s['performance_remeasured'] is False and s['physical_memory_reclamation_verified'] is False
build=load(v/'build-commands.json');assert len(build)==5 and all(c['returncode']==0 for c in build)
assert 'fault_plan_assertions=914 GPU_calls=0' in (v/'fault-plan-native.log').read_text(errors='replace')
text=(v/'python-tests-final.log').read_text(errors='replace');assert 'Ran 614 tests' in text and '\nOK\n' in text and 'skipped=' not in text
assert all(c['returncode']==0 for c in load(v/'check-commands.json')) and load(v/'python-command-final.json')['returncode']==0 and load(v/'matrix-command.json')['returncode']==0
assert '12 altered rebased-fault matrices/raw reports rejected' in (v/'report-controls.log').read_text() and 'every valid relocated baseline accepted' in (v/'report-controls.log').read_text()
receipt=load(native.SOURCE/'candidate-receipt.json');assert receipt['product_SDK_source_fingerprint']==native.SDK and receipt['candidate_SDK_source_fingerprint']==native.CANDIDATE
assert len(e['historical_packages'])==23 and e['memory_policy']['sha256']=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
prior=load(root/e['previous_evidence']['path']);assert prior['candidate_SDK_source_fingerprint']==e['candidate_SDK_source_fingerprint'] and prior['SDK_source_fingerprint']==e['SDK_source_fingerprint']
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1 and len(set(z.namelist()))==len(z.namelist())
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    m=json.loads(z.read('source-manifest.json'));assert m['files']==e['source_files'] and sha(json.dumps(m['files'],sort_keys=True,separators=(',',':')).encode())==m['source_manifest_sha256']
    assert not any(n.startswith('.deps/') or pathlib.Path(n).suffix.lower() in ('.exe','.dll','.lib','.pdb') for n in z.namelist())
if '--staged' in sys.argv:
    cmd=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false'];rows=subprocess.check_output([*cmd,'ls-files','--stage','-z'],cwd=root).decode().split('\0');ids={s.split('\t',1)[1]:s.split()[1] for s in rows if s}
    paths={r['path'] for r in e['source_files']+e['files'] if not r['path'].startswith('out/')}|{e[k]['path'] for k in ('summary','cases','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'};paths.discard('tests/VulkanAllocationBenchmarkTests.cpp')
    pairs=[(p,ids[p]) for p in sorted(paths)];data=subprocess.run([*cmd,'cat-file','--batch'],cwd=root,input=('\n'.join(oid for _,oid in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout;position=0
    for path,oid in pairs:
        end=data.index(b'\n',position);header=data[position:end].split();assert header[:2]==[oid.encode(),b'blob'];size=int(header[2]);blob=data[end+1:end+1+size];position=end+2+size;raw=(root/path).read_bytes();assert blob==raw or blob==raw.replace(b'\r\n',b'\n'),path
    changed=subprocess.check_output([*cmd,'diff','--cached','--name-only','-z'],cwd=root).decode().split('\0');assert not any(p.startswith(('.idea/','out/','.deps/')) or p=='tests/VulkanAllocationBenchmarkTests.cpp' for p in changed if p)
print(f'PASS {len(records)} hashes / 614 Python / 914 CPU plan / 88 clean + 14 controls; 324 faults, 1200 readbacks, zero native remainder/invalid cleanup; source archive and history verified')
