# Replay saved Linux LLGL PSO correctness using repository validators; execute no ZIP/native code.
import pathlib
import sys
HERE=pathlib.Path(__file__).resolve().parent
ROOT=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools'))
import linux_llgl_pso as u

def verify():
    manifest=u.read(HERE/'manifest.json')
    actual={p.relative_to(HERE).as_posix() for p in HERE.rglob('*') if p.is_file() and p.name!='manifest.json' and '__pycache__' not in p.parts}
    u.require(actual=={x['path'] for x in manifest['files']},'Archive inventory changed')
    for item in manifest['files']:
        path=u.p.safe(HERE,item['path'])
        u.require(path.stat().st_size==item['bytes'] and u.sha(path)==item['sha256'],'Archived file changed: '+item['path'])
    result=u.check(HERE/'native');r=u.read(HERE/'native/receipt.json');s=u.read(HERE/'summary.json');a=u.read(HERE/'native/inputs.json')
    u.require(result==s['result'],'Summary/native result differs')
    u.p.exact(s,dict(public_pipeline_create_release_pairs=72,public_shader_create_release_pairs=144,native_forwarded_draw_calls=72,native_vertices=216,native_pipeline_count_verified=False,GPU_memory_reclamation_verified=False,driver_cache_state_known=False))
    u.p.exact(s,dict(schema_version=1,task='P3-LBEN-03-LLGL-readiness',status='full_software_pso_correctness_verified',source_commit=r['source_commit'],profile_id=u.p.PROFILE,LLGL_commit=r['probe']['LLGL_commit'],source_fingerprint=r['adaptation']['source_fingerprint'],selected=r['probe']['qualification']['selected'],environment=r['probe']['qualification']['environment'],compiler_units=len(a['compile_commands']),compiler_inputs=len(a['files']),compiler_categories={name:sum(x['component']==name for x in a['files']) for name in sorted({x['component'] for x in a['files']})},qualified_LLGL_libraries_reused=True,workload_bodies_changed=False,physical_GPU_accepted=False,P3_LBEN_03_completed=False,performance_accepted=False,performance_samples=0))
    for system in ('windows','linux'):
        ci=u.read(HERE/('python-'+system)/'receipt.json')
        u.p.exact(ci,dict(source_commit=r['source_commit'],successful=True,inputs_unchanged=True,tests=1327,failures=[],errors=[],skipped=[],expected_failures=[],unexpected_successes=[],native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False))
        u.require(u.sha(HERE/('python-'+system)/'unittest.log')==ci['log']['sha256'],'Python log differs')
        for name in ('tools/linux_llgl_pso.py','tools/port_linux_llgl_pso.py'):
            tested=next(x for x in ci['inputs'] if x['path']==name)
            executed=next(x for x in r['source_inputs'] if x['path']==name)
            u.require(tested['sha256']==executed['sha256'],'Tested/executed source differs')
    print('PASS saved Linux LLGL PSO: 17 processes, 16 full changing images, actual core/sync controls; software correctness only')
    return result

if __name__=='__main__':verify()
