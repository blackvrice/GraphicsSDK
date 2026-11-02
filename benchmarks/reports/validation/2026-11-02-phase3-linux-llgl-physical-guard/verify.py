# Replay current validators; execute no archived Python or native code.
import pathlib
import sys
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'tools'))
import linux_llgl_probe as p

def verify():
    manifest=p.read(HERE/'manifest.json')
    actual={x.relative_to(HERE).as_posix() for x in HERE.rglob('*') if x.is_file() and x.name!='manifest.json' and '__pycache__' not in x.parts}
    p.require(actual=={x['path'] for x in manifest['files']},'Archive inventory differs')
    for item in manifest['files']:
        path=p.safe(HERE,item['path'])
        p.require(path.stat().st_size==item['bytes'] and p.sha(path)==item['sha256'],'Archive byte drift')
    result=p.check(HERE/'native');r=p.read(HERE/'native/receipt.json');s=p.read(HERE/'summary.json');a=p.read(HERE/'native/inputs.json')
    p.require(p.same(result,s['result']),'Summary/result differs')
    p.exact(result,dict(processes=4,normal_probes=0,actual_callback_controls=0,rejection_controls=4,verified_bytes=0,performance_samples=0,mode='physical',physical_guard_only=True,physical_probe_qualified=False,P3_BEN_08_completed=False,physical_GPU_accepted=False,workload_correctness_accepted=False))
    p.exact(s,dict(schema_version=1,task='P3-BEN-08-LLGL-physical-guard',status='native_software_rejections_verified',source_commit=r['source_commit'],profile_id=p.PROFILE,LLGL_commit=r['LLGL_commit'],probe_fingerprint=r['probe_fingerprint'],compiler_units=len(a['compile_commands']),compiler_inputs=len(a['files']),compiler_categories={name:sum(x['component']==name for x in a['files']) for name in sorted({x['component'] for x in a['files']})},qualification_environment=r['qualification']['environment'],software_selected=r['qualification']['selected'],physical_success_path_executed=False,physical_GPU_accepted=False,P3_BEN_08_completed=False,performance_samples=0))
    for system in ('windows','linux'):
        ci=p.read(HERE/('python-'+system)/'receipt.json')
        p.exact(ci,dict(source_commit=r['source_commit'],successful=True,inputs_unchanged=True,tests=1369,failures=[],errors=[],skipped=[],expected_failures=[],unexpected_successes=[],native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False))
        p.require(p.sha(HERE/('python-'+system)/'unittest.log')==ci['log']['sha256'],'Python log drift')
        tested=next(x for x in ci['inputs'] if x['path']=='tools/linux_llgl_probe.py')
        executed=next(x for x in r['source_inputs'] if x['path']=='tools/linux_llgl_probe.py')
        p.require(tested['sha256']==executed['sha256'],'Tested and executed runner differ')
    print('PASS physical LLGL profile: four actual Linux software rejections; zero physical qualification/performance acceptance')
    return result

if __name__=='__main__':verify()
