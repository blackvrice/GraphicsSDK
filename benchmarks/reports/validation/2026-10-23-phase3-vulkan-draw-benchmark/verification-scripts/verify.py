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
import run_vulkan_draw as upload
import report_vulkan_draw as report_tool

v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw-benchmark'
base=root/'benchmarks/reports/2026-10-23-phase3-vulkan-draw-benchmark'
results=root/'benchmarks/results/2026-10-23-vulkan-draw'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(data):return hashlib.sha256(data).hexdigest()
e=load(base/'evidence.json')
records=e['files']+e['source_files']+e['preserved_inputs']+e['unchanged_production_sources']+e['historical_packages']+e['local_artifacts']+[e[k] for k in ('summary','metrics','source_archive','previous_evidence')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert audit.sdk_fingerprint(root)==e['SDK_source_fingerprint']
assert upload.fingerprint(root)==e['benchmark_source_fingerprint']
x=report_tool.review(results)
assert len(x['runs'])==48 and x['verified_images']==1308 and x['verified_bytes']==12582960
assert x['repetitions']==3 and x['warmup']==5 and x['samples_per_process']==30
commands=load(v/'commands.json');assert len(commands)==6 and all(c['returncode']==0 for c in commands)
xml=ET.parse(v/'ctest.xml').getroot();assert len(xml.findall('.//testcase'))==3 and not xml.findall('.//failure') and not xml.findall('.//skipped')
assert 'Ran 295 tests' in (v/'python-tests.log').read_text(errors='replace') and '\nOK\n' in (v/'python-tests.log').read_text(errors='replace')
assert '648 conservative inputs, 21 components; two new compiler/linker traces' in (v/'benchmark-audit.log').read_text()
assert len(e['historical_packages'])==23
for pref in ('high','low'):
    text=(results/f'validation-{pref}-sdl-draw1.stderr').read_text()
    assert text.count('Inserted device layer "VK_LAYER_KHRONOS_validation"')>=2
    assert 'VkLayer_khronos_validation.dll' in text and 'forced disabled' in text
    for sdk in ('graphicssdk','sdl'):
        for scenario in upload.SHAPES:
            text=(results/f'validation-{pref}-{sdk}-{scenario}.stderr').read_text()
            assert 'Validation Error' not in text and 'SYNC-HAZARD' not in text
for sdk in ('graphicssdk','sdl'):
    assert 'VK_LAYER_KHRONOS_validation' in (results/f'missing-layer-{sdk}.stderr').read_text()
    assert not (results/f'missing-layer-{sdk}.stdout').read_bytes()
    assert not (results/f'wrong-gpu-{sdk}.stdout').read_bytes()
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
assert summary['submitted_draw_calls']==sum(r['result']['submitted_draw_calls'] for r in x['runs'])==436872
for row in summary['CPU_interval_statistics']:
    rr=[r['result'] for r in x['runs'] if r['kind']=='measurement' and all(r['result'][k]==row[k] for k in ('preference','scenario','sdk'))]
    for i,name in enumerate(('record_ms','submit_ms','wait_ms','total_ms')):
        assert row['statistics'][name]==upload.summarize([dict(samples_ms=[s[i] for s in r['CPU_intervals_ms']]) for r in rr])
assert 'draw_reference_assertions=12 covered_pixels=1705 clear_pixels=700 GPU_calls=0' in (v/'native-tests.log').read_text(errors='replace')
assert 'seven altered report controls rejected' in (v/'report-controls.log').read_text()
initial=(v/'initial-spirv13/results/validation-high-sdl-draw1.stdout').read_bytes()
assert initial.count(b'VUID-VkShaderModuleCreateInfo-pCode-08737')>=3 and b'Invalid SPIR-V binary version 1.3' in initial
assert b'Initial Vulkan 1.1 shader path requires SPIR-V 1.3' in (v/'initial-spirv10/results/validation-high-graphicssdk-draw1.stderr').read_bytes()
tool=load(v/'shader-tools/receipt.json');reconstruction=load(v/'shader-tools/source-reconstruction.json')
assert tool['source']['sha256']==reconstruction['snapshot']['sha256'] and tool['header']['sha256']==sha((v/'initial-spirv10/DrawShaders.hpp').read_bytes())
for fixture in tool['fixtures']:
    assert (root/fixture['binary']['path']).read_bytes()[:8]==bytes.fromhex('0302230700000100')
old=load(v/'reused-shader-receipt.json')
for fixture in old['fixtures']:
    if fixture['name'] in ('fullscreen','red','green'):
        import re,struct
        header=(root/old['header']['path']).read_text()
        body=re.search(r' '+fixture['name']+r'\{(.*?)\};',header,re.S).group(1)
        words=[int(x,16) for x in re.findall(r'0x([0-9a-f]+)u',body)]
        data=struct.pack('<'+'I'*len(words),*words)
        assert sha(data)==fixture['binary']['sha256'] and data[:8]==bytes.fromhex('0302230700030100')
print(f'PASS: {len(records)} hashes; 48 GPU processes / 1308 full images / 1080 primary samples; three CTests, 295 Python tests, seven report controls and source ZIP; unchanged SDK and 23 packages preserved')
