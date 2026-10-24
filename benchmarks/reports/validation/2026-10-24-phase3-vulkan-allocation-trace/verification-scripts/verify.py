import hashlib
import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

root=pathlib.Path(__file__).resolve().parents[5]
sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import run_vulkan_allocation_trace as upload
import report_vulkan_allocation_trace as report_tool

v=root/'benchmarks/reports/validation/2026-10-24-phase3-vulkan-allocation-trace'
base=root/'benchmarks/reports/2026-10-24-phase3-vulkan-allocation-trace'
results=root/'benchmarks/results/2026-10-24-vulkan-allocation-trace'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(data):return hashlib.sha256(data).hexdigest()
e=load(base/'evidence.json')
records=e['files']+e['source_files']+e['preserved_inputs']+e['unchanged_production_sources']+e['historical_packages']+e['local_artifacts']+[e[k] for k in ('summary','metrics','source_archive','previous_evidence')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert audit.sdk_fingerprint(root)==e['SDK_source_fingerprint']
assert upload.fingerprint(root)==e['benchmark_source_fingerprint']
x=report_tool.review(results)
assert len(x['runs'])==32 and x['verified_images']==32032 and x['verified_buffers']==32032 and x['verified_resources']==64064 and x['verified_bytes']==439350912
assert all(r['result']['warmup']==1 and r['result']['sample_count']==3 for r in x['runs'])
commands=load(v/'commands.json');assert len(commands)==6 and all(c['returncode']==0 for c in commands)
xml=ET.parse(v/'ctest.xml').getroot();assert len(xml.findall('.//testcase'))==2 and not xml.findall('.//failure') and not xml.findall('.//skipped')
assert 'Ran 417 tests' in (v/'python-tests.log').read_text(errors='replace') and '\nOK\n' in (v/'python-tests.log').read_text(errors='replace')
assert '653 conservative inputs, 21 components; four new compiler/linker traces' in (v/'benchmark-audit-final.log').read_text()
assert len(e['historical_packages'])==23
for pref in ('high','low'):
    for sdk in ('graphicssdk','sdl'):
        text=(results/f'trace-1-{pref}-{sdk}-buffer1.stderr').read_text(encoding='utf-8')
        assert 'Inserted device layer "VK_LAYER_GFX_allocation_trace"' in text and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in text
for sdk in ('graphicssdk','sdl'):
    for name in ('missing-trace','missing-vvl','wrong-trace-path','invalid-phase','wrong-gpu'):
        assert not (results/f'{name}-{sdk}.stdout').read_bytes()
for n in range(6):assert not (results/f'invalid-option-{n}.stdout').read_bytes()
receipt=load(results/'runtime-receipt.json')
assert receipt==load(root/'out/vulkan-validation-runtime/receipt.json')
assert sha((root/'tools/vulkan-validation.lock.json').read_bytes())==receipt['lock_sha256']
assert sha((root/'tools/memory_stress_policy.json').read_bytes())=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    manifest=json.loads(z.read('source-manifest.json'));assert manifest['files']==e['source_files']
    assert sha(json.dumps(manifest['files'],sort_keys=True,separators=(',',':')).encode())==manifest['source_manifest_sha256']
if '--staged' in sys.argv:
    rows=subprocess.check_output(['git','ls-files','--stage','-z'],cwd=root).decode().split('\0')
    ids={line.split('\t',1)[1]:line.split()[1] for line in rows if line}
    paths={r['path'] for r in e['source_files']+e['files']}|{e[k]['path'] for k in ('summary','metrics','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'}
    pairs=[(p,ids[p]) for p in sorted(paths)]
    data=subprocess.run(['git','cat-file','--batch'],cwd=root,input=('\n'.join(i for _,i in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout
    position=0
    for path,oid in pairs:
        end=data.index(b'\n',position);header=data[position:end].split();assert header[:2]==[oid.encode(),b'blob']
        size=int(header[2]);blob=data[end+1:end+1+size];position=end+2+size;raw=(root/path).read_bytes()
        assert blob==raw or blob==raw.replace(b'\r\n',b'\n'),path
summary=load(base/'summary.json')
for k in ('created_resources','released_resource_handles','used_resources'):
    assert summary[k]==sum(r['result'][k] for r in x['runs'])==64064
assert summary['scoped_records']==672 and summary['performance_comparison'] is False
assert 'trace_ledger_assertions=17 GPU_calls=0' in (v/'native-tests.log').read_text(errors='replace')
assert 'trace_interface_assertions=13 GPU_calls=0' in (v/'native-tests.log').read_text(errors='replace')
assert 'eight altered report controls rejected' in (v/'report-controls.log').read_text()
print(f'PASS: {len(records)} hashes; 32 profile processes / 64064 resources / 672 scoped records; two CTests, 417 Python tests, eight report controls and source ZIP; unchanged SDK and 23 packages preserved')
