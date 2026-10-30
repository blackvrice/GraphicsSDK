# Replay the saved Linux software matrix using repository validators.
import hashlib
import json
import pathlib
import sys
HERE=pathlib.Path(__file__).resolve().parent
ROOT=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import linux_workload_matrix as m

def verify():
    manifest=m.read(HERE/'manifest.json')
    actual={p.relative_to(HERE).as_posix() for p in HERE.rglob('*') if p.is_file() and p.name!='manifest.json' and '__pycache__' not in p.parts}
    m.require(actual=={x['path'] for x in manifest['files']},'Archive file inventory changed')
    for item in manifest['files']:
        path=m.safe(HERE,item['path'])
        m.require(path.stat().st_size==item['bytes'] and m.sha(path)==item['sha256'],'Archived file changed: '+item['path'])
    result=m.check(HERE/'native')
    summary=m.read(HERE/'summary.json');r=m.read(HERE/'native/receipt.json')
    m.require(result==summary['result'],'Summary differs from native replay')
    m.exact(summary,dict(schema_version=1,task='P3-LBEN-readiness',status='SDK_SDL_full_software_correctness_verified',source_commit=r['source_commit'],LLGL_accepted=False,hardware_GPU_accepted=False,performance_accepted=False,performance_samples=0,P3_LBEN_completed=False,diagnostic_timings_excluded=True))
    a=r['readiness']['qualification']
    m.require(summary['selected']==a['selected'] and summary['environment']==a['environment'] and summary['SDK_source_fingerprint']==a['SDK_source_fingerprint'] and summary['SDL_commit']==a['SDL_commit'] and summary['profile_fingerprints']==r['readiness']['profile_fingerprints'],'Summary identity/source binding differs')
    for system in ('windows','linux'):
        ci=m.read(HERE/('python-'+system)/'receipt.json')
        m.exact(ci,dict(source_commit=r['source_commit'],successful=True,inputs_unchanged=True,tests=1264,failures=[],errors=[],skipped=[],unexpected_successes=[],expected_failures=[],native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False))
        m.require(m.sha(HERE/('python-'+system)/'unittest.log')==ci['log']['sha256'],'CI Python raw log changed')
        own=next(x for x in ci['inputs'] if x['path']=='tools/linux_workload_matrix.py')
        executed=next(x for x in r['source_inputs'] if x['path']=='tools/linux_workload_matrix.py')
        m.require(own['sha256']==executed['sha256'],'Tested runner differs from executed source')
    print('PASS saved full Linux SDK/SDL software correctness matrix; 52 processes, 13 cases; no hardware/performance acceptance')
    return result

if __name__=='__main__':verify()
