# Replay saved native Linux LLGL probe evidence using reviewed repository validators.
import pathlib
import sys
HERE=pathlib.Path(__file__).resolve().parent
ROOT=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import linux_llgl_probe as p

def verify():
    manifest=p.read(HERE/'manifest.json')
    actual={x.relative_to(HERE).as_posix() for x in HERE.rglob('*') if x.is_file() and x.name!='manifest.json' and '__pycache__' not in x.parts}
    p.require(actual=={x['path'] for x in manifest['files']},'Archive inventory changed')
    for item in manifest['files']:
        path=p.safe(HERE,item['path'])
        p.require(path.stat().st_size==item['bytes'] and p.sha(path)==item['sha256'],'Archive file changed: '+item['path'])
    result=p.check(HERE/'native');r=p.read(HERE/'native/receipt.json');s=p.read(HERE/'summary.json');a=p.read(HERE/'native/inputs.json')
    p.require(result==s['result'],'Summary/result differs')
    p.exact(s,dict(schema_version=1,task='P3-LBEN-LLGL-readiness',status='native_software_minimal_probe_verified',source_commit=r['source_commit'],profile_id=p.PROFILE,LLGL_commit=r['LLGL_commit'],probe_fingerprint=r['probe_fingerprint'],selected=r['qualification']['selected'],environment=r['qualification']['environment'],SDK_source_fingerprint=r['qualification']['SDK_source_fingerprint'],SDL_commit=r['qualification']['SDL_commit'],compiler_units=len(a['compile_commands']),compiler_inputs=len(a['files']),compiler_categories={name:sum(x['component']==name for x in a['files']) for name in sorted({x['component'] for x in a['files']})},unmodified_upstream=False,physical_GPU_accepted=False,workload_correctness_accepted=False,performance_accepted=False,performance_samples=0))
    for system in ('windows','linux'):
        ci=p.read(HERE/('python-'+system)/'receipt.json')
        p.exact(ci,dict(source_commit=r['source_commit'],successful=True,inputs_unchanged=True,tests=1281,failures=[],errors=[],skipped=[],unexpected_successes=[],expected_failures=[],native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False))
        p.require(p.sha(HERE/('python-'+system)/'unittest.log')==ci['log']['sha256'],'Python raw log differs')
        tested=next(x for x in ci['inputs'] if x['path']=='tools/linux_llgl_probe.py')
        executed=next(x for x in r['source_inputs'] if x['path']=='tools/linux_llgl_probe.py')
        p.require(tested['sha256']==executed['sha256'],'Tested runner differs from executed archive')
    print('PASS saved Linux LLGL: 8 processes, actual device/queue/callback, 24576 full reference bytes; software minimal probe only')
    return result

if __name__=='__main__':verify()
