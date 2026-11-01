# Replay repository validators only; execute no archived Python/native code.
import pathlib
import sys
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'tools'))
import linux_readiness_report as r

def verify():
    manifest=r.read(HERE/'manifest.json')
    actual={p.relative_to(HERE).as_posix() for p in HERE.rglob('*') if p.is_file() and p.name!='manifest.json' and '__pycache__' not in p.parts}
    r.require(actual=={x['path'] for x in manifest['files']},'Archive inventory changed')
    for item in manifest['files']:
        path=HERE/item['path']
        r.require(path.resolve().is_relative_to(HERE.resolve()) and path.stat().st_size==item['bytes'] and r.sha(path)==item['sha256'],'Archive file changed')
    result=r.check(HERE/'bundle');s=r.read(HERE/'summary.json');v=r.read(HERE/'bundle/report.json');receipt=r.read(HERE/'bundle/receipt.json')
    r.require(result==s['result'],'Summary differs from replay')
    r.p.exact(s,dict(schema_version=1,task='P3-LBEN-software-integration',native_source_commit=v['native_source_commit'],collector_checkout_head=receipt['collector_checkout_head'],collector_source_sha256=receipt['collector_source_sha256'],environment=v['environment'],selected=v['selected'],coverage_rows=78,component_count=5,evidence_files=len(r.read(HERE/'bundle/inventory.json')),diagnostic_timings_excluded=True,handoff_executed=False,performance_samples=0,physical_GPU_accepted=False,Phase3_completed=False))
    r.require(receipt['live_inputs_verified'] is True and receipt['collector_checkout_head']==v['native_source_commit'],'CI collector/native revision differs')
    for system in ('windows','linux'):
        ci=r.read(HERE/('python-'+system)/'receipt.json')
        r.p.exact(ci,dict(source_commit=v['native_source_commit'],successful=True,inputs_unchanged=True,tests=1361,failures=[],errors=[],skipped=[],expected_failures=[],unexpected_successes=[],native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False))
        r.require(r.sha(HERE/('python-'+system)/'unittest.log')==ci['log']['sha256'],'Python log drift')
        tested=next(x for x in ci['inputs'] if x['path']=='tools/linux_readiness_report.py')
        r.require(tested['sha256']==receipt['collector_source_sha256'],'Tested/executed collector source differs')
    print('PASS saved Linux three-SDK integration: 78 normal + 46 control processes; software correctness only')
    return result

if __name__=='__main__':verify()
