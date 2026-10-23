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
base=root/'benchmarks/reports/2026-10-23-phase3-vulkan-color-pass'
v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-color-pass'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(data):return hashlib.sha256(data).hexdigest()
e=load(base/'evidence.json')
records=e['files']+e['source_files']+e['preserved_inputs']+e['historical_packages']+e['local_artifacts']
records += [e[k] for k in ('summary','source_archive','previous_evidence','SDK_package')]
for r in records:
    data=(root/r['path']).read_bytes();assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert audit.sdk_fingerprint(root)==e['SDK_source_fingerprint']
commands=load(v/'commands.json');assert len(commands)==25 and all(c['returncode']==c['expected'] for c in commands)
for name,count in (('ctest.xml',38),('only-ctest.xml',13),('portable-ctest.xml',6)):
    xml=ET.parse(v/name).getroot()
    assert len(xml.findall('.//testcase'))==count and not xml.findall('.//failure') and not xml.findall('.//skipped')
color_pass='PASS assertions=593 passes=128 images=138 verified_texel_bytes=747063512 padded_images=114 adapters=2'
buffer='PASS assertions=717 transfers=48 verified_bytes=57042546 adapters=2'
texture='PASS assertions=835 roundtrips=74 verified_texel_bytes=499371832 padded_roundtrips=40 adapters=2'
probe='PASS assertions=274 adapters=2 native_core_errors=64 native_sync_hazards=2 sync_disabled_controls=2 synthetic_errors=2050 invalid_GPU_submissions=0'
for name in ('native-tests.log','only-native-tests.log'):
    text=(v/name).read_text(errors='replace');assert text.count(buffer)==2 and text.count(texture)==2 and text.count(color_pass)==2 and probe in text
    assert text.count('source='+e['SDK_source_fingerprint']+' validation=1')==3
    assert text.count('loaded_layer=')==6 and 'Unexpected validation:' not in text
for name,suite in (('consumer-native.log',buffer),('consumer-texture.log',texture),('consumer-render.log',color_pass)):
    text=(v/name).read_text();assert suite in text and 'source='+e['SDK_source_fingerprint']+' validation=1' in text
text=(v/'controls-loader-trace.log').read_text();assert probe in text and text.count('loaded_layer=')==6 and 'Unexpected validation:' not in text
for name in ('unknown-option.log','extra-option.log','texture-unknown.log','texture-extra.log','render-unknown.log','render-extra.log'):
    text=(v/name).read_text();assert 'Usage:' in text and 'source=' not in text
for name in ('missing-layer.log','texture-missing-layer.log','render-missing-layer.log'):
    assert 'VK_LAYER_KHRONOS_validation is not installed' in (v/name).read_text()
text=(v/'python-tests.log').read_text();assert 'Ran 208 tests' in text and '\nOK\n' in text
receipt=load(v/'runtime-receipt.json');assert sha((root/'tools/vulkan-validation.lock.json').read_bytes())==receipt['lock_sha256']
for r in receipt['artifacts']:assert sha((root/r['path']).read_bytes())==r['sha256']
first=load(v/'first-attempt/receipt.json');assert first['CTest_passed']==40 and first['CTest_total']==41 and first['failed_test']=='memory_reclamation_validation'
xml=ET.parse(v/'first-attempt/ctest.xml').getroot();assert len(xml.findall('.//testcase'))==41
failed=[t for t in xml.findall('.//testcase') if t.findall('failure')];assert len(failed)==1 and failed[0].get('name')=='memory_reclamation_validation'
assert '-E' in next(c for c in commands if c['name']=='ctest')['argv']
assert '^memory_reclamation_' in next(c for c in commands if c['name']=='ctest')['argv']
assert b'77e68ea2b9225ec12f8b0778ec8aefc29f3e9e12fb5517162b88439f02a8ee4d' in (root/'out/vulkan-color-pass-first-attempt/Release/GraphicsSDKCore.lib').read_bytes()
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    manifest=json.loads(z.read('source-manifest.json'));assert manifest['files']==e['source_files']
    assert sha(json.dumps(manifest['files'],sort_keys=True,separators=(',',':')).encode())==manifest['source_manifest_sha256']
if '--staged' in sys.argv:
    rows=subprocess.check_output(['git','ls-files','--stage','-z'],cwd=root).decode().split('\0')
    ids={line.split('\t',1)[1]:line.split()[1] for line in rows if line}
    paths={r['path'] for r in e['source_files']+e['files']}|{e[k]['path'] for k in ('summary','source_archive')}|{base.relative_to(root).as_posix()+'/evidence.json'}
    pairs=[(p,ids[p]) for p in sorted(paths)]
    result=subprocess.run(['git','cat-file','--batch'],cwd=root,input=('\n'.join(i for _,i in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout
    position=0
    for path,oid in pairs:
        end=result.index(b'\n',position);header=result[position:end].split();assert header[:2]==[oid.encode(),b'blob']
        size=int(header[2]);blob=result[end+1:end+1+size];position=end+2+size;data=(root/path).read_bytes()
        assert blob==data or blob==data.replace(b'\r\n',b'\n'),path
print(f'PASS: {len(records)} hashes, 57 selected CTests (3 deferred memory tests excluded), 208 Python contracts, texture/counter controls and source ZIP verified')
