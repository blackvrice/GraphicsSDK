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
base=root/'benchmarks/reports/2026-10-23-phase3-vulkan-validation'
v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-validation'
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(data):return hashlib.sha256(data).hexdigest()
e=load(base/'evidence.json')
records=e['files']+e['source_files']+e['preserved_inputs']+e['historical_packages']+e['local_artifacts']
records += [e[k] for k in ('summary','source_archive','previous_evidence','SDK_package')]
for r in records:
    data=(root/r['path']).read_bytes()
    assert len(data)==r['bytes'] and sha(data)==r['sha256'],r['path']
assert audit.sdk_fingerprint(root)==e['SDK_source_fingerprint']
commands=load(v/'commands.json');assert len(commands)==17 and all(c['returncode']==c['expected'] for c in commands)
for name,count in (('ctest.xml',37),('only-ctest.xml',9),('portable-ctest.xml',6)):
    xml=ET.parse(v/name).getroot()
    assert len(xml.findall('.//testcase'))==count and not xml.findall('.//failure') and not xml.findall('.//skipped')
suite='PASS assertions=717 transfers=48 verified_bytes=57042546 adapters=2'
probe='PASS assertions=274 adapters=2 native_core_errors=64 native_sync_hazards=2 sync_disabled_controls=2 synthetic_errors=2050 invalid_GPU_submissions=0'
for name,native_count in (('native-tests.log',2),('only-native-tests.log',2),('consumer-native.log',1)):
    text=(v/name).read_text(errors='replace');assert text.count(suite)==native_count
    assert text.count('source='+e['SDK_source_fingerprint']+' validation=1')==1
for name in ('native-tests.log','only-native-tests.log','controls-loader-trace.log','final-probe.log'):
    text=(v/name).read_text(errors='replace');assert probe in text and 'Unexpected validation:' not in text
    assert text.count('loaded_layer=')==6
for name in ('unknown-option.log','extra-option.log'):
    text=(v/name).read_text();assert 'Usage:' in text and 'source=' not in text
assert 'VK_LAYER_KHRONOS_validation is not installed' in (v/'missing-layer.log').read_text()
text=(v/'python-tests.log').read_text();assert 'Ran 208 tests' in text and '\nOK\n' in text
receipt=load(v/'runtime-receipt.json')
assert sha((root/'tools/vulkan-validation.lock.json').read_bytes())==receipt['lock_sha256']
for r in receipt['artifacts']:assert sha((root/r['path']).read_bytes())==r['sha256']
with zipfile.ZipFile(root/e['source_archive']['path']) as z:
    assert len(z.namelist())==len(e['source_files'])+1
    for r in e['source_files']:assert sha(z.read(r['path']))==r['sha256']
    manifest=json.loads(z.read('source-manifest.json'));assert manifest['files']==e['source_files']
    assert sha(json.dumps(manifest['files'],sort_keys=True,separators=(',',':')).encode())==manifest['source_manifest_sha256']
if '--staged' in sys.argv:
    rows=subprocess.check_output(['git','ls-files','--stage','-z'],cwd=root).decode().split('\0')
    ids={line.split('\t',1)[1]:line.split()[1] for line in rows if line}
    paths={r['path'] for r in e['source_files']+e['files']}|{e[k]['path'] for k in ('summary','source_archive')}
    pairs=[(p,ids[p]) for p in sorted(paths)]
    result=subprocess.run(['git','cat-file','--batch'],cwd=root,input=('\n'.join(i for _,i in pairs)+'\n').encode(),stdout=subprocess.PIPE,check=True).stdout
    position=0
    for path,oid in pairs:
        end=result.index(b'\n',position);header=result[position:end].split();assert header[:2]==[oid.encode(),b'blob']
        size=int(header[2]);blob=result[end+1:end+1+size];position=end+2+size;data=(root/path).read_bytes()
        assert blob==data or blob==data.replace(b'\r\n',b'\n'),path
print(f'PASS: {len(records)} hashes, 52 CTests, 208 Python contracts, native layer controls and source ZIP verified')
