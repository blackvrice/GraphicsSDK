"""Replay archived CAP-02 contracts without Vulkan, binaries or a network."""
import hashlib
import importlib.util
import json
import pathlib
import sys
import tempfile
import zipfile

sys.dont_write_bytecode = True
F = pathlib.Path(__file__).resolve().parent
ROOT = F.parents[3]
COMMIT = '5d8af5c5632edbffa93bd8ab3c93ef4474c108e9'


def read(p):
    return json.loads(p.read_text(encoding='utf-8-sig'))


def sha(b):
    return hashlib.sha256(b).hexdigest()


def checked(p, record):
    raw = p.read_bytes()
    assert len(raw) == record['bytes'] and sha(raw) == record['sha256'], str(p)


def main():
    for item in read(F/'inventory.json')['files']:
        checked(ROOT/item['path'], item)
    for artifact in read(F/'ci/artifacts.json')['artifacts']:
        assert sha((F/'ci'/artifact['name']).read_bytes()) == artifact['sha256']
    for kind, campaign_dir in [('windows', 'windows'), ('linux-git', 'linux')]:
        source = read(F/('source-manifest-'+kind+'.json'))
        checked(ROOT/source['archive']['path'], source['archive'])
        assert source['implementation_commit'] == COMMIT
        with tempfile.TemporaryDirectory(prefix='gfx-recovery-replay-') as temporary:
            restored = pathlib.Path(temporary)
            with zipfile.ZipFile(ROOT/source['archive']['path']) as z:
                assert set(z.namelist()) == {i['path'] for i in source['files']} | {'source-receipt.json'}
                for name in z.namelist():
                    p = pathlib.PurePosixPath(name)
                    assert not p.is_absolute() and '..' not in p.parts and '__pycache__' not in p.parts
                    assert p.suffix.lower() not in ('.pyc','.exe','.dll','.lib','.pdb','.dmp','.so','.a')
                receipt = json.loads(z.read('source-receipt.json'))
                assert receipt == {k:v for k,v in source.items() if k != 'archive'}
                z.extractall(restored)
            for item in source['files']:
                checked(restored/item['path'], item)
            sys.path.insert(0, str(restored/'tools'))
            for name in ('audit_licenses', 'run_vulkan_recovery'):
                sys.modules.pop(name, None)
            spec = importlib.util.spec_from_file_location('run_vulkan_recovery', restored/'tools/run_vulkan_recovery.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            assert module.sdk_fingerprint(restored) == source['SDK_source_fingerprint']
            campaign = read(F/campaign_dir/'campaign.json')
            assert campaign['status'] == 'passed' and campaign['inputs_unchanged'] and campaign['serialized']
            assert campaign['SDK_source_fingerprint'] == source['SDK_source_fingerprint']
            assert [c['name'] for c in campaign['cases']] == (['normal','validation'] if kind == 'windows' else ['normal'])
            source_hashes = {i['path']:i['sha256'] for i in source['files']}
            frozen = {n.replace('\\','/'):h for n,h in campaign['frozen_inputs'].items()}
            for n in ('tools/run_vulkan_recovery.py','tests/VulkanRecoveryTests.cpp','tests/fixtures/SpirvFixtures.hpp','tests/vulkan_recovery/Layer.cpp','tests/vulkan_recovery/Control.hpp','tests/vulkan_recovery/CMakeLists.txt'):
                assert any(p.endswith('/'+n) and h == source_hashes[n] for p,h in frozen.items()), n
            for case in campaign['cases']:
                assert case['returncode'] == 0
                rows = [json.loads(l) for l in (F/campaign_dir/(case['name']+'.stdout')).read_text().splitlines()]
                result = module.validate_rows(rows, source['SDK_source_fingerprint'], case['name']=='validation', kind=='linux-git')
                assert result == case['result']
                assert result['cycles'] == (7 if kind=='windows' else 4)
                assert result['window_images'] == (4 if kind=='windows' else 0)
            sys.path.pop(0)
    binary = read(F/'binary-identities.json')
    frozen = read(F/'windows/campaign.json')['frozen_inputs']
    for item in binary:
        if 'observer/Release/' in item['path'] or 'install/lib/' in item['path']:
            assert any(n.replace('\\','/').endswith('/'+item['path']) and h==item['sha256'] for n,h in frozen.items())
    native = read(F/'native/command.json')
    assert native['returncode'] == 0
    assert native['command'][-1] == '^memory_reclamation_(gpu|validation)$'
    assert '100% tests passed, 0 tests failed out of 60' in (F/'native/ctest.log').read_text(encoding='utf-8-sig')
    assert 'vulkan_recovery_contracts_validation' in (F/'native/LastTest.log').read_text()
    for directory in ('python-local','python-final','python-ci-linux','python-ci-windows'):
        receipt = read(F/directory/'receipt.json')
        assert receipt['successful'] and receipt['inputs_unchanged']
        assert not any(receipt[k] for k in ('failures','errors','skipped','unexpected_successes'))
        assert len(receipt['test_ids']) == 673
        checked(F/directory/'unittest.log', receipt['log'])
        if directory.startswith('python-ci-'):
            assert receipt['source_commit'] == COMMIT
    runs = read(F/'ci/runs.json')
    assert all(r['head_sha'] == COMMIT for r in runs)
    for name in ('Vulkan generation recovery (Linux software ICD)','Build and correctness','Linux Vulkan compile'):
        assert any(r['name']==name and r['conclusion']=='success' for r in runs)
    assert not list(F.rglob('*.pyc')) and not list(F.rglob('__pycache__'))
    print('PASS CAP-02: exact two source archives; Windows/Linux recovery; failed retries, GPU references and closure; native 60; Python 673 on four receipts')


if __name__ == '__main__':
    main()
