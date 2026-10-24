import hashlib
import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
root=pathlib.Path(__file__).resolve().parents[5];sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import report_vulkan_creation_ownership as report_tool
import vulkan_creation_provenance as history
v=pathlib.Path(__file__).resolve().parent.parent;base=root/'benchmarks/reports/2026-10-24-vulkan-creation-ownership';results=root/'benchmarks/results/2026-10-24-vulkan-creation-ownership'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(b):return hashlib.sha256(b).hexdigest()
e=load(base/'evidence.json');records=e['files']+e['source_files']+e['preserved_inputs']+e['historical_packages']+e['local_artifacts']+[e[k] for k in ('summary','cases','source_archive','previous_evidence','new_product_source')]+[r[k] for r in e['changed_product_sources'] for k in ('before','after')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert audit.sdk_fingerprint(root)==e['SDK_source_fingerprint'] and audit.sdk_fingerprint(root/history.SNAPSHOT)==e['prior_SDK_source_fingerprint']==history.OLD
assert {r['original']['path'] for r in e['historical_relocations']}==history.CHANGED
for r in e['historical_relocations']:assert history.resolve(r['original'],root)==r['snapshot']
x=report_tool.review(results);assert len(x['runs'])==24
commands=load(v/'build-commands.json');assert len(commands)==4 and all(c['returncode']==0 for c in commands)
test=next(c for c in commands if c['name']=='product-tests');assert test['argv'][test['argv'].index('-E')+1]=='^memory_reclamation_'
xml=ET.parse(v/'product-tests.xml').getroot();assert len(xml.findall('.//testcase'))==55 and not xml.findall('.//failure') and not xml.findall('.//skipped')
assert not any('memory_reclamation_' in n.attrib.get('name','') for n in xml.findall('.//testcase'))
consumers=load(v/'consumer-build-commands.json');assert len(consumers)==10 and all(c['returncode']==c['expected'] for c in consumers)
assert load(v/'matrix-command.json')['returncode']==0 and len(load(results/'commands.json'))==33
assert 'Ran 526 tests' in (v/'python-tests.log').read_text(errors='replace') and '\nOK\n' in (v/'python-tests.log').read_text(errors='replace')
assert 'creation_assertions=3640 GPU_calls=0' in (v/'product-native-tests.log').read_text(errors='replace')
assert 'eleven altered ownership reports rejected' in (v/'report-controls.log').read_text()
assert len(load(v/'guarded-call-sites.json'))==29
assert sum((root/p).read_text(encoding='utf-8').count('vulkan::acquireOutput(') for p in ('src/backends/vulkan/Vulkan.cpp','src/backends/vulkan/Presentation.hpp'))==29
assert 'CreateGraphicsPipelines(owner->native->handle, {}, 1, &info, nullptr, &p->pipeline)' in (root/'src/backends/vulkan/Vulkan.cpp').read_text(encoding='utf-8')
assert len(e['historical_packages'])==23 and sha((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
s=load(base/'summary.json');expected=dict(product_fix_adopted=True,buffer_pool_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False,hardware_verified_buffers=132,hardware_verified_bytes=589824,hardware_injected_failures=48,hardware_invalid_free_before=12,hardware_invalid_free_after=0,CPU_invalid_cleanup_before=21,CPU_invalid_cleanup_after=0,public_headers_unchanged=True)
assert all(s[k]==v and type(s[k]) is type(v) for k,v in expected.items())
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    manifest=json.loads(z.read('source-manifest.json'));assert manifest['files']==e['source_files'] and sha(json.dumps(manifest['files'],sort_keys=True,separators=(',',':')).encode())==manifest['source_manifest_sha256']
if '--staged' in sys.argv:
    command=['git','-c','gc.auto=0','-c','maintenance.auto=false'];rows=subprocess.check_output([*command,'ls-files','--stage','-z'],cwd=root).decode().split('\0');ids={s.split('\t',1)[1]:s.split()[1] for s in rows if s}
    # Frozen out/ source bytes ship in the ZIP, not as loose repository files.
    paths={r['path'] for r in e['source_files']+e['files'] if not r['path'].startswith('out/')}|{e[k]['path'] for k in ('summary','cases','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'};pairs=[(p,ids[p]) for p in sorted(paths)]
    data=subprocess.run([*command,'cat-file','--batch'],cwd=root,input=('\n'.join(oid for _,oid in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout;position=0
    for path,oid in pairs:
        end=data.index(b'\n',position);header=data[position:end].split();assert header[:2]==[oid.encode(),b'blob'];size=int(header[2]);blob=data[end+1:end+1+size];position=end+2+size;raw=(root/path).read_bytes();assert blob==raw or blob==raw.replace(b'\r\n',b'\n'),path
print(f'PASS {len(records)} hashes / product 55 CTests / 526 Python / actual CPU 27 before/after / hardware 20 clean + 4 guarded + 8 controls; product fix and immutable history verified')
