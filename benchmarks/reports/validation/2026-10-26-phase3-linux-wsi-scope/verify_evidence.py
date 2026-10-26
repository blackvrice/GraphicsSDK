"""Verify preserved WSI scope observations offline; does not execute a display."""
import hashlib
import importlib.util
import json
import pathlib
import sys
import tempfile
import zipfile
sys.dont_write_bytecode=True
F=pathlib.Path(__file__).resolve().parent;ROOT=F.parents[3]


def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def checked(p,r):
    b=p.read_bytes();assert len(b)==r['bytes'] and sha(b)==r['sha256'],str(p)


def main():
    for item in read(F/'inventory.json')['files']:checked(ROOT/item['path'],item)
    artifacts=read(F/'ci/artifacts.json')
    for key,name in [('first','first-artifact.zip'),('accepted','accepted-artifact.zip'),('python_linux','python-linux-artifact.zip'),('python_windows','python-windows-artifact.zip')]:
        assert sha((F/'ci'/name).read_bytes())==artifacts[key]['sha256']
    for kind,folder in [('first','first-rgba-rejection'),('accepted','accepted-native')]:
        m=read(F/f'source-manifest-{kind}.json');checked(ROOT/m['archive']['path'],m['archive'])
        receipt=read(F/folder/'receipt.json');assert receipt['source_commit']==m['source_commit']
        assert receipt['inputs_unchanged'] and receipt['SDK_source_fingerprint']==m['SDK_source_fingerprint']
        with tempfile.TemporaryDirectory(prefix='gfx-wsi-scope-') as directory:
            restored=pathlib.Path(directory)
            with zipfile.ZipFile(ROOT/m['archive']['path']) as z:
                assert set(z.namelist())=={i['path'] for i in m['files']}|{'source-receipt.json'}
                assert json.loads(z.read('source-receipt.json'))=={k:v for k,v in m.items() if k!='archive'}
                for n in z.namelist():
                    p=pathlib.PurePosixPath(n);assert not p.is_absolute() and '..' not in p.parts and '__pycache__' not in p.parts
                    assert p.suffix.lower() not in ('.pyc','.exe','.dll','.lib','.a','.so','.pdb','.dmp')
                z.extractall(restored)
            for item in m['files']:checked(restored/item['path'],item)
            sys.path.insert(0,str(restored/'tools'));sys.modules.pop('audit_licenses',None)
            spec=importlib.util.spec_from_file_location('wsi_archived_'+kind,restored/'tools/run_linux_wsi_scope.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            assert module.sdk_fingerprint(restored)==m['SDK_source_fingerprint']
            frozen=receipt['frozen_inputs']
            for name in ('tools/run_linux_wsi_scope.py','tests/linux_wsi_scope/Probe.cpp','tests/linux_wsi_scope/CMakeLists.txt'):
                h=sha((restored/name).read_bytes());assert any(n.endswith('/'+name) and v==h for n,v in frozen.items())
            rows=[json.loads(l) for l in (F/folder/'xcb.stdout').read_text().splitlines()]
            result=module.validate_rows(rows,m['SDK_source_fingerprint']);assert result==receipt['cases'][0]['result']
            assert receipt['cases'][0]['returncode']==0
            if kind=='first':
                assert receipt['status']=='failed' and receipt['error']=='CI requires observed ready software candidates'
                assert result['policy_ready_adapters']==0 and len(receipt['cases'])==1
                assert all(a['rgba8'] is False for a in rows if a.get('event')=='adapter')
            else:
                assert receipt['status']=='passed' and result['adapters']==result['software_adapters']==result['policy_ready_adapters']==1
                assert len(receipt['cases'])==2 and receipt['cases'][1]['returncode']==2
                negative=[json.loads(l) for l in (F/folder/'no-display.stdout').read_text().splitlines()]
                assert len(negative)==1 and negative[0]['event']=='display_unavailable' and negative[0]['xcb_error']!=0
                adapter=next(a for a in rows if a.get('event')=='adapter');assert adapter['bgra8'] is True and adapter['rgba8'] is False
                assert {(a['format'],a['color_space']) for a in rows if a.get('event')=='surface_format'}=={(44,0),(50,0)}
                assert len(receipt['runtime_files'])==3 and all(r['redistributed'] is False and len(r['sha256'])==64 for r in receipt['runtime_files'])
            sys.path.pop(0)
    for directory in ('python-local','python-ci-linux','python-ci-windows'):
        p=read(F/directory/'receipt.json');assert p['successful'] and p['inputs_unchanged'] and p['tests']==len(p['test_ids'])==682
        assert not any(p[k] for k in ('errors','failures','skipped','unexpected_successes'))
        checked(F/directory/'unittest.log',p['log'])
    runs=read(F/'ci/runs.json')
    for name in ('Linux WSI scope (XCB prerequisites)','Build and correctness','Linux Vulkan compile','Linux installed SDK input and payload audit'):
        assert any(r['name']==name and r['conclusion']=='success' and r['head_sha']=='f666c01a2b83268c5885f5996235479294753780' for r in runs)
    assert not list(F.rglob('*.pyc')) and not list(F.rglob('__pycache__'))
    print('PASS WSI-01: exact first/accepted source ZIPs, preserved RGBA rejection, explicit BGRA prerequisites, no-display control; Python 682; no SDK presentation or physical GPU acceptance')


if __name__=='__main__':main()
