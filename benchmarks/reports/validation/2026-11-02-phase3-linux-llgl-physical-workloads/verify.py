# Replays current repository validators only; source ZIPs are data, never executed.
import hashlib
import importlib
import pathlib
import sys
import tempfile
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[3]
sys.path.insert(0,str(ROOT/'tools'))
import linux_readiness_report as io
import linux_llgl_workload_mode as policy
p=policy.p


def replay_bundle():
    inventory=p.read(HERE/'inventory.json')
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lpw-',dir=ROOT/'out') as td:
        folder=pathlib.Path(td);io.unpack(HERE/'evidence.zip',inventory,folder)
        p.require({x.name for x in folder.iterdir()}==set(policy.PROFILES),'Missing/extra physical workload')
        receipts={};components=[];last=0
        for profile in policy.PROFILES:
            module=importlib.import_module('linux_llgl_'+profile)
            result=module.check(folder/profile);r=p.read(folder/profile/'receipt.json');receipts[profile]=r
            p.exact(result,dict(processes=4,normal_processes=0,core_sync_controls=0,rejection_controls=4,
                verified_images=0,verified_bytes=0,performance_samples=0,physical_GPU_accepted=False,
                mode='physical',physical_guard_only=True,P3_BEN_08_completed=False,
                **{f'physical_{profile}_correctness_verified':False}))
            p.exact(r,dict(physical_guard_only=True,**{f'software_{profile}_correctness_verified':False}))
            for row in r['runs']:
                p.require(last<=row['started'],'Cross-consumer process overlap/order differs');last=row['finished']
            a=p.read(folder/profile/'inputs.json')
            components.append(dict(profile=profile,result=result,source_fingerprint=r['adaptation']['source_fingerprint'],
                compiler_units=len(a['compile_commands']),compiler_inputs=len(a['files'])))
        first=receipts['upload']
        p.require(all(r['source_commit']==first['source_commit'] and p.same(r['probe'],first['probe']) for r in receipts.values()),'Mixed source/probe/qualification')
        bindings=io.merge_records([r['frozen_inputs']+r['runtime_inputs'] for r in receipts.values()])
        p.require(sum(x['compiler_units'] for x in components)==7,'Physical consumer compile coverage differs')
        return dict(source_commit=first['source_commit'],components=components,processes=16,normal_processes=0,
            core_sync_controls=0,rejection_controls=16,verified_images=0,verified_buffers=0,verified_bytes=0,
            performance_samples=0,physical_success_path_executed=False,physical_GPU_accepted=False,
            P3_BEN_08_completed=False,Phase3_completed=False,shared_probe_sha256=first['probe_sha256'],
            qualification=first['probe']['qualification'],frozen_runtime_file_union=len(bindings))


def verify():
    manifest=p.read(HERE/'manifest.json')
    actual={x.relative_to(HERE).as_posix() for x in HERE.rglob('*') if x.is_file() and x.name!='manifest.json' and '__pycache__' not in x.parts}
    p.require(actual=={x['path'] for x in manifest['files']},'Archive inventory changed')
    for item in manifest['files']:
        path=p.safe(HERE,item['path'])
        p.require(path.stat().st_size==item['bytes'] and p.sha(path)==item['sha256'],'Archive bytes changed')
    result=replay_bundle();s=p.read(HERE/'summary.json')
    p.require(p.same(s['result'],result),'Summary/replay differs')
    p.exact(s,dict(schema_version=1,task='P3-BEN-08-LLGL-physical-workload-guards',source_commit=result['source_commit'],
        physical_success_path_executed=False,physical_GPU_accepted=False,performance_samples=0,Phase3_completed=False))
    # Four source ZIPs have already been checked above. Bind the tested Python to
    # each actually archived source file without importing any archived module.
    import zipfile
    with zipfile.ZipFile(HERE/'evidence.zip') as bundle:
        import io as stdio
        sources={}
        for profile in policy.PROFILES:
            with zipfile.ZipFile(stdio.BytesIO(bundle.read(profile+'/source.zip'))) as source:
                for name in (policy.SOURCE,'tools/linux_llgl_probe.py','tools/linux_llgl_'+profile+'.py','tools/port_linux_llgl_'+profile+'.py'):
                    digest=hashlib.sha256(source.read(name)).hexdigest()
                    p.require(name not in sources or sources[name]==digest,'Shared executed policy changed')
                    sources[name]=digest
    for system in ('windows','linux'):
        ci=p.read(HERE/('python-'+system)/'receipt.json')
        p.exact(ci,dict(source_commit=result['source_commit'],successful=True,inputs_unchanged=True,tests=1382,
            failures=[],errors=[],skipped=[],expected_failures=[],unexpected_successes=[],
            native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False))
        p.require(p.sha(HERE/('python-'+system)/'unittest.log')==ci['log']['sha256'],'Python log changed')
        tested={x['path']:x['sha256'] for x in ci['inputs']}
        p.require(all(tested[name]==digest for name,digest in sources.items()),'Tested and executed Python sources differ')
    print('PASS physical LLGL workloads: four real Linux builds, sixteen software rejections, zero physical/performance acceptance')
    return result


if __name__=='__main__':verify()
