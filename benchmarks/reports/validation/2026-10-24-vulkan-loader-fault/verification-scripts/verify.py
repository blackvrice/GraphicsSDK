import hashlib
import json
import pathlib
import subprocess
import sys
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import report_vulkan_loader_fault as report
import vulkan_creation_provenance as history
v=pathlib.Path(__file__).resolve().parent.parent;base=root/'benchmarks/reports/2026-10-24-vulkan-loader-fault';results=root/'benchmarks/results/2026-10-24-vulkan-loader-fault-v2'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(b):return hashlib.sha256(b).hexdigest()
e=load(base/'evidence.json');records=e['files']+e['source_files']+e['preserved_inputs']+e['historical_packages']+e['local_artifacts']+[e[k] for k in ('summary','source_archive','previous_evidence','memory_policy')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert e['product_changed'] is False and audit.sdk_fingerprint(root)==e['SDK_source_fingerprint'] and audit.sdk_fingerprint(root/history.SNAPSHOT)==history.OLD
s=load(base/'summary.json');actual=report.review(results);assert all(report.same(s[k],value) for k,value in actual.items())
assert s['clean_processes']==32 and s['regression_processes']==8 and s['total_processes']==51 and s['injections']==96
assert s['invalid_cleanup']==0 and s['prior_invalid_cleanup']==24 and s['verified_buffers']==360 and s['presented_frames']==24 and s['verified_bytes']==7545312
assert s['native_objects_created']==s['native_objects_destroyed']==360 and s['final_live_objects']==0
assert s['product_CTests_rerun'] is False and s['prior_product_selected_CTests']==55
assert 'Ran 550 tests' in (v/'python-tests-final.log').read_text(errors='replace') and '\nOK\n' in (v/'python-tests-final.log').read_text(errors='replace')
assert 'skipped=' not in (v/'python-tests-final.log').read_text(errors='replace')
assert '12 altered loader matrices/raw reports rejected' in (v/'report-controls-v3.log').read_text() and 'every relocated baseline accepted' in (v/'report-controls-v3.log').read_text()
assert len(load(v/'build-commands-v4.json'))==6 and all(c['returncode']==0 for c in load(v/'build-commands-v4.json'))
assert all(c['returncode']==0 for c in load(v/'check-commands.json')+load(v/'final-check-commands.json')) and load(v/'matrix-command.json')['returncode']==0
for variant in ('before','after'):assert 'loader plan assertions=216 GPU_calls=0' in (v/(variant+'-plan-native.log')).read_text(errors='replace')
prior=load(root/e['previous_evidence']['path']);assert e['SDK_source_fingerprint']==prior['SDK_source_fingerprint']
assert len(e['historical_packages'])==23 and e['memory_policy']['sha256']=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1 and len(set(z.namelist()))==len(z.namelist())
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    m=json.loads(z.read('source-manifest.json'));assert m['files']==e['source_files'] and sha(json.dumps(m['files'],sort_keys=True,separators=(',',':')).encode())==m['source_manifest_sha256']
    assert not any(n.startswith('.deps/') or pathlib.Path(n).suffix.lower() in ('.exe','.dll','.lib','.pdb') for n in z.namelist())
if '--staged' in sys.argv:
    cmd=['git','-c','gc.auto=0','-c','maintenance.auto=false'];rows=subprocess.check_output([*cmd,'ls-files','--stage','-z'],cwd=root).decode().split('\0');ids={s.split('\t',1)[1]:s.split()[1] for s in rows if s}
    paths={r['path'] for r in e['source_files']+e['files'] if not r['path'].startswith('out/')}|{e[k]['path'] for k in ('summary','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'}
    paths.discard('tests/VulkanAllocationBenchmarkTests.cpp')
    pairs=[(p,ids[p]) for p in sorted(paths)];data=subprocess.run([*cmd,'cat-file','--batch'],cwd=root,input=('\n'.join(oid for _,oid in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout;position=0
    for path,oid in pairs:
        end=data.index(b'\n',position);header=data[position:end].split();assert header[:2]==[oid.encode(),b'blob'];size=int(header[2]);blob=data[end+1:end+1+size];position=end+2+size;raw=(root/path).read_bytes();assert blob==raw or blob==raw.replace(b'\r\n',b'\n'),path
    changed=subprocess.check_output([*cmd,'diff','--cached','--name-only','-z'],cwd=root).decode().split('\0');assert not any(p.startswith(('.idea/','out/','.deps/')) or p=='tests/VulkanAllocationBenchmarkTests.cpp' for p in changed if p)
print(f'PASS {len(records)} hashes / 550 Python / 216 CPU plan / 32 clean + 8 guarded + 10 controls; 96 injected errors, zero invalid cleanup/live objects; source ZIP and history verified')
