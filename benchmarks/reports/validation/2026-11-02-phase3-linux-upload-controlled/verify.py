# Execute current repository validators; all ZIP source/native files remain data.
import hashlib
import io
import pathlib
import sys
import tempfile
import zipfile
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'tools'))
import linux_readiness_report as bundle
import linux_upload_campaign as c
p=c.p


def replay_bundle():
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='luc-',dir=ROOT/'out') as td:
        root=pathlib.Path(td);bundle.unpack(HERE/'evidence.zip',p.read(HERE/'inventory.json'),root)
        p.require({x.name for x in root.iterdir()}=={'upload'},'Unexpected campaign evidence root')
        folder=root/'upload';value=c.check(folder);r=p.read(folder/'receipt.json');a=p.read(folder/'inputs.json')
        c.build_policy.scope(c.qualification(r).get('controlled_build'),('sdk','sdl','probe'))
        c.build_policy.scope(r['readiness'].get('controlled_build'),('consumer',))
        p.exact(value,dict(processes=42,rejection_controls=12,core_sync_controls=2,measurement_processes=0,measurement_samples=0,
            verified_images=96,verified_bytes=2588083680,statistics_rows=0,performance_accepted=False,physical_GPU_accepted=False,P3_LBEN_01_completed=False))
        p.exact(r,dict(mode='software-diagnostic',campaign_kind='software-smoke',diagnostic_rows=72,groups=[],SDK_SDL_measurement_module_snapshot_observed=False))
        with zipfile.ZipFile(folder/'source.zip') as source:
            tested_sources={name:hashlib.sha256(source.read(name)).hexdigest() for name in ('tools/linux_upload_campaign.py','tools/port_linux_upload_campaign.py','tools/linux_build_policy.py','tools/linux_comparison.py','tools/linux_workloads.py')}
        return dict(source_commit=r['source_commit'],result=value,environment=c.qualification(r)['environment'],selected=c.qualification(r)['selected'],
            LLGL_upload_fingerprint=r['adaptation']['source_fingerprint'],compiler_units=len(a['compile_commands']),compiler_inputs=len(a['files']),
            compiler_categories={k:sum(x['component']==k for x in a['files']) for k in sorted({x['component'] for x in a['files']})},
            runtime_file_count=len(r['runtime_inputs']),frozen_file_count=len(r['frozen_inputs']),tested_sources=tested_sources,controlled_build_name=c.build_policy.NAME,controlled_compile_units={project:len(p.read(folder/'readiness/qualification/controlled'/(project+'-commands.json'))) for project in ('sdk','sdl','probe')},consumer_compile_units=len(p.read(folder/'readiness/controlled/consumer-commands.json')))


def verify():
    manifest=p.read(HERE/'manifest.json')
    actual={x.relative_to(HERE).as_posix() for x in HERE.rglob('*') if x.is_file() and x!=HERE/'manifest.json' and '__pycache__' not in x.parts}
    p.require(actual=={x['path'] for x in manifest['files']},'Archive inventory drift')
    for item in manifest['files']:
        path=p.safe(HERE,item['path']);p.require(path.stat().st_size==item['bytes'] and p.sha(path)==item['sha256'],'Archive byte drift')
    v=replay_bundle();s=p.read(HERE/'summary.json');p.require(p.same(v,s['replay']),'Summary/replay drift')
    p.exact(s,dict(schema_version=1,task='P3-LBEN-01-upload-controlled-smoke',source_commit=v['source_commit'],physical_success_path_executed=False,
        performance_accepted=False,physical_GPU_accepted=False,P3_LBEN_01_completed=False,statistics_exported=False,Phase3_completed=False))
    for system in ('windows','linux'):
        ci=p.read(HERE/('python-'+system)/'receipt.json')
        p.exact(ci,dict(source_commit=v['source_commit'],successful=True,inputs_unchanged=True,tests=1403,
            failures=[],errors=[],skipped=[],expected_failures=[],unexpected_successes=[],native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False))
        p.require(p.sha(HERE/('python-'+system)/'unittest.log')==ci['log']['sha256'],'Python log drift')
        tested={x['path']:x['sha256'] for x in ci['inputs']};p.require(all(tested[name]==digest for name,digest in v['tested_sources'].items()),'Tested/executed runner or adapter drift')
    print('PASS controlled O2 Linux upload campaign: 42 native software processes, 96 full images; zero measurement/statistics/physical acceptance')
    return v


if __name__=='__main__':verify()
