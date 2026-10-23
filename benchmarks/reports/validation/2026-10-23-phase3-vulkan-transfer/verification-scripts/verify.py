"""Verify the frozen final evidence and exact source archive, optionally staged blobs."""
import hashlib
import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

root = pathlib.Path(__file__).resolve().parents[5]
sys.path.insert(0, str(root / 'tools'))
import audit_licenses as audit

def sha(data): return hashlib.sha256(data).hexdigest()
def load(path): return json.loads(path.read_text(encoding='utf-8'))
evidence = load(root / 'benchmarks/reports/2026-10-23-phase3-vulkan-transfer/evidence.json')
validation = root / 'benchmarks/reports/validation/2026-10-23-phase3-vulkan-transfer'
records = evidence['files'] + evidence['preserved_inputs'] + evidence['historical_packages'] + evidence['source_files']
records += [evidence[k] for k in ('summary', 'source_archive', 'previous_evidence', 'SDK_package')]
for record in records:
    data = (root / record['path']).read_bytes()
    assert len(data) == record['bytes'] and sha(data) == record['sha256'], record['path']
assert audit.sdk_fingerprint(root) == evidence['SDK_source_fingerprint']
commands = load(validation / 'commands.json')
assert len(commands) == 15 and all(c['returncode'] == c['expected'] for c in commands)
for name, count in [('ctest.xml', 35), ('only-ctest.xml', 7), ('portable-ctest.xml', 6)]:
    xml = ET.parse(validation / name).getroot()
    cases = xml.findall('.//testcase')
    assert len(cases) == count and not xml.findall('.//failure') and not xml.findall('.//skipped')
for name in ('native-tests.log', 'only-native-tests.log', 'consumer-native.log'):
    text = (validation / name).read_text(encoding='utf-8', errors='replace')
    assert 'PASS assertions=717 transfers=48 verified_bytes=57042546 adapters=2' in text
    assert evidence['SDK_source_fingerprint'] in text
text = (validation / 'python-tests.log').read_text(encoding='utf-8')
assert 'Ran 208 tests' in text and '\nOK\n' in text
assert 'VK_LAYER_KHRONOS_validation is not installed' in (validation / 'validation-unavailable.log').read_text()
with zipfile.ZipFile(root / evidence['source_archive']['path']) as archive:
    assert len(archive.namelist()) == len(evidence['source_files']) + 1
    for record in evidence['source_files']:
        assert sha(archive.read(record['path'])) == record['sha256']
    manifest = json.loads(archive.read('source-manifest.json'))
    assert manifest['files'] == evidence['source_files']
    assert sha(json.dumps(manifest['files'], sort_keys=True, separators=(',', ':')).encode()) == manifest['source_manifest_sha256']
if '--staged' in sys.argv:
    staged = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=root).decode().split('\0')
    ids = {line.split('\t', 1)[1]: line.split()[1] for line in staged if line}
    paths = {r['path'] for r in evidence['source_files'] + evidence['files']} | {
        evidence[k]['path'] for k in ('summary', 'source_archive')}
    pairs = [(p, ids[p]) for p in sorted(paths)]
    result = subprocess.run(['git', 'cat-file', '--batch'], cwd=root, input=('\n'.join(i for _, i in pairs) + '\n').encode(),
        stdout=subprocess.PIPE, check=True).stdout
    position = 0
    for path, oid in pairs:
        end = result.index(b'\n', position)
        header = result[position:end].split(); assert header[:2] == [oid.encode(), b'blob']
        size = int(header[2]); blob = result[end+1:end+1+size]; position = end+2+size
        current = (root / path).read_bytes()
        assert blob == current or blob == current.replace(b'\r\n', b'\n'), path
print(f'PASS: {len(records)} hashed records, 48 CTests, 208 Python tests, 144 native transfers and source archive verified')
