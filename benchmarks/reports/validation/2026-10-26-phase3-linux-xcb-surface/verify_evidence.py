"""Offline XCB surface evidence replay; retain all observed regression failures."""
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
    for key,name in [('first','first-artifact.zip'),('audit_failed','audit-failed-artifact.zip'),('accepted','accepted-artifact.zip'),('python_linux','python-linux-artifact.zip'),('python_windows','python-windows-artifact.zip')]:
        assert sha((F/'ci'/name).read_bytes())==artifacts[key]['sha256']
    for kind in ('windows','linux-git'):
        m=read(F/f'source-manifest-{kind}.json');checked(ROOT/m['archive']['path'],m['archive'])
        assert len(m['files'])==43 and m['compiled_or_external_binaries'] is False
        with tempfile.TemporaryDirectory(prefix='gfx-xcb-replay-') as directory:
            r=pathlib.Path(directory)
            with zipfile.ZipFile(ROOT/m['archive']['path']) as z:
                assert set(z.namelist())=={i['path'] for i in m['files']}|{'source-receipt.json'}
                assert json.loads(z.read('source-receipt.json'))=={k:v for k,v in m.items() if k!='archive'}
                for name in z.namelist():
                    p=pathlib.PurePosixPath(name);assert not p.is_absolute() and '..' not in p.parts and '__pycache__' not in p.parts
                    assert p.suffix.lower() not in ('.pyc','.exe','.dll','.lib','.pdb','.dmp','.a','.so')
                z.extractall(r)
            for item in m['files']:checked(r/item['path'],item)
            sys.path.insert(0,str(r/'tools'));sys.modules.pop('audit_licenses',None)
            spec=importlib.util.spec_from_file_location('xcb_archived',r/'tools/run_linux_xcb_surface.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            assert module.sdk_fingerprint(r)==m['SDK_source_fingerprint']
            if kind=='windows':
                assert read(F/'native/receipt.json')['SDK_source_fingerprint']==m['SDK_source_fingerprint']
                for name,h in read(F/'native/receipt.json')['frozen_inputs'].items():
                    for local in ('tests/CoreTests.cpp','tests/SwapChainTests.cpp','tests/VulkanPresentationTests.cpp'):
                        if name.replace('\\','/').endswith('/'+local):assert sha((r/local).read_bytes())==h
            else:
                for folder in ('linux-first','linux-audit-failed','linux'):
                    receipt=read(F/folder/'receipt.json');assert receipt['status']=='passed' and receipt['inputs_unchanged']
                    assert receipt['SDK_source_fingerprint']==m['SDK_source_fingerprint']
                    for name in ('tools/run_linux_xcb_surface.py','tests/linux_xcb_surface/Probe.cpp','tests/linux_xcb_surface/Loader.cpp','tests/linux_xcb_surface/Control.hpp','tests/linux_xcb_surface/CMakeLists.txt'):
                        assert any(p.endswith('/'+name) and h==sha((r/name).read_bytes()) for p,h in receipt['frozen_inputs'].items())
                    cases=receipt['cases'];assert [x['name'] for x in cases]==['normal','missing-extension','surface-queue-rejected','visual-queue-rejected','missing-procedure','surface-lost','headless','disabled']
                    for case in cases:
                        assert case['returncode']==0
                        rows=[json.loads(l) for l in (F/folder/(case['name']+'.stdout')).read_text().splitlines()]
                        result=case['result'];assert module.validate_rows(rows,m['SDK_source_fingerprint'],result['mode'],result['disabled'],result['headless'])==result
                audit=read(F/'linux/setup/input-audit.json');assert audit['Core_hard_XCB_link'] is False and audit['public_platform_headers'] is False
                policy=read(r/'licenses/linux-vulkan-policy.json')
                hashes={i['path']:i['sha256'] for i in m['files']}
                for build in audit['builds']:
                    on=build['policy']=='on';inputs=build['compiler_inputs'];payload=build['installed_payload']
                    assert len(inputs)==(395 if on else 389) and len(payload)==20
                    reviewed=policy['dependency']['reviewed_headers']+(policy['xcb_reviewed_headers'] if on else [])
                    vendor={i['path'].split('/.deps/Vulkan-Headers/')[1]:i['sha256'] for i in inputs if i['category']=='vendor'}
                    assert vendor=={i['path']:i['sha256'] for i in reviewed}
                    for item in inputs:
                        assert item['category'] in ('own','vendor','system')
                        if item['category']=='own':assert any(item['path'].endswith('/'+n) and item['sha256']==h for n,h in hashes.items())
                    for item in payload:
                        if item['path'].startswith('include/'):assert hashes[item['path']]==item['sha256']
                    checked(F/'linux/setup'/('THIRD_PARTY_NOTICES-'+build['policy']+'.txt'),build['notice'])
                    assert 'xcb' not in (F/'linux/setup'/('export-'+build['policy']+'.txt')).read_text().lower()
                    assert not any(line.strip().startswith('U xcb_') for line in (F/'linux/setup'/('undefined-'+build['policy']+'.txt')).read_text().splitlines())
                assert len(audit['runtime_files'])==3 and all(x['redistributed'] is False for x in audit['runtime_files'])
            sys.path.pop(0)
    native=read(F/'native/receipt.json');assert native['returncode']==8 and native['inputs_unchanged']
    log=(F/'native/ctest.log').read_text(encoding='utf-8-sig');assert '2 tests failed out of 60' in log
    for name in ('fence_wait_gpu','fence_wait_validation'):assert name+' (SEGFAULT)' in log
    for name in ('vulkan_presentation','vulkan_presentation_validation','swapchain_guards_gpu','swapchain_guards_validation'):
        assert any(name in l and 'Passed' in l for l in log.splitlines())
    isolated=read(F/'fence-isolated/receipt.json');assert len(isolated)==4 and all(c['returncode']==0xc0000008 for c in isolated)
    identity=read(F/'fence-isolated/source-identities.json');assert identity['current']!=identity['previous']
    assert read(ROOT/identity['previous_source_manifest'])['SDK_source_fingerprint']==identity['previous']
    for directory in ('python-first','python-final','python-ci-linux','python-ci-windows'):
        receipt=read(F/directory/'receipt.json');assert receipt['successful'] and receipt['inputs_unchanged'] and receipt['tests']==len(receipt['test_ids'])==688
        assert not any(receipt[k] for k in ('failures','errors','skipped','unexpected_successes'));checked(F/directory/'unittest.log',receipt['log'])
    assert not list(F.rglob('*.pyc')) and not list(F.rglob('__pycache__'))
    print('PASS WSI-02: exact source ZIPs, eight actual SDK controls, ON/OFF input/payload audit, Python 688; Windows 58/60 and four current/previous fence exceptions preserved')


if __name__=='__main__':main()
