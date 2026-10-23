"""Freeze and validate a rejected SDK experiment separately from production sources."""
import hashlib
import json
import pathlib
import zipfile
import audit_licenses as a

ROOT=pathlib.Path(__file__).resolve().parents[1]
FOLDER=ROOT/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory/cached-candidate'
EXPECTED='f0cc198c5c4aca3130c3705f6b1e6d1857652c27fd64aa48a9d872d5fd7767af'
def record(p):return a.record_file(ROOT,p)
def freeze():
    assert a.sdk_fingerprint(ROOT)==EXPECTED
    FOLDER.mkdir(exist_ok=False)
    suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl','.spvasm'}
    paths={p for f in ('include','src','cmake','tests','examples','tools','shaders') for p in (ROOT/f).rglob('*') if p.is_file() and p.suffix in suffixes}
    paths|={ROOT/n for n in ('CMakeLists.txt','CMakePresets.json','README.md','docs/VULKAN.md','LICENSE','licenses/vulkan-upload-memory-policy.json')}
    records=[record(p) for p in sorted(paths)];archive=FOLDER/'sources.zip'
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
        for r in records:z.writestr(r['path'],(ROOT/r['path']).read_bytes())
    receipt=dict(SDK_source_fingerprint=EXPECTED,adopted=False,archive=record(archive),files=records,SDK_inventory=record(ROOT/'licenses/generated/windows-sdk-vulkan-upload-memory-release/inventory.json'))
    (FOLDER/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS: complete cached-memory candidate source frozen for independent audit; not production acceptance')

def candidate():
    receipt=json.loads((FOLDER/'receipt.json').read_bytes());r=receipt['archive'];assert record(ROOT/r['path'])==r
    assert receipt['SDK_source_fingerprint']==EXPECTED and receipt['adopted'] is False
    assert record(ROOT/receipt['SDK_inventory']['path'])==receipt['SDK_inventory']
    with zipfile.ZipFile(ROOT/r['path']) as z:
        assert len(z.namelist())==len(receipt['files']) and set(z.namelist())=={r['path'] for r in receipt['files']}
        data={r['path']:z.read(r['path']) for r in receipt['files']}
    for r in receipt['files']:assert hashlib.sha256(data[r['path']]).hexdigest()==r['sha256'] and len(data[r['path']])==r['bytes']
    sdk=[p for p in data if p=='CMakeLists.txt' or (p.startswith(('include/','src/')) and pathlib.Path(p).suffix in ('.hpp','.cpp'))]
    fp=hashlib.sha256(''.join(p+'|'+hashlib.sha256(data[p]).hexdigest()+'\n' for p in sorted(sdk,key=pathlib.Path)).encode()).hexdigest()
    assert fp==EXPECTED
    return receipt,data

if __name__=='__main__':freeze()
