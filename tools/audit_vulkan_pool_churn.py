"""Actual rebased candidate SDK/consumer reads plus preserved conservative closure."""
import argparse
import json
import pathlib
import audit_licenses as audit
import audit_vulkan as system
import audit_vulkan_loader_fault as inherited
import run_vulkan_pool_churn as trial
ROOT=trial.ROOT
GENERATED=pathlib.Path('licenses/generated/windows-vulkan-pool-churn-release')
def collect(root=ROOT):
    require=trial.require;prior,_=inherited.collect(root);files={r['path']:r for r in prior['files']}
    receipt=json.loads((trial.SOURCE/'candidate-receipt.json').read_text(encoding='utf-8'));frozen={r['path']:r for r in receipt['files']}
    for r in receipt['files']+[receipt['patch_tool']]:require(audit.record_file(root,root/r['path'])==r,'Changed frozen candidate source: '+r['path'])
    require(audit.sdk_fingerprint(root)==trial.SDK and audit.sdk_fingerprint(trial.SOURCE)==trial.CANDIDATE and receipt['product_SDK_source_fingerprint']==trial.SDK and receipt['prior_candidate_SDK_source_fingerprint']==trial.prepare.PRIOR,'Reviewed candidate lineage')
    own={p.relative_to(root).as_posix() for p in (root/'experiments/vulkan_pool_churn').iterdir() if p.suffix in ('.cpp','.hpp') or p.name=='CMakeLists.txt'}
    for name in own:files[name]=dict(**audit.record_file(root,root/name),component='graphicssdk')
    cs=[];ls=[];projects=[];artifacts=[];headers=set();seen=set()
    build=root/'out/vulkan-pool-churn-sdk/Release/GraphicsSDKCore.lib';installed=root/'out/vulkan-pool-churn-install/lib/GraphicsSDKCore.lib';product=root/'out/vulkan-creation-install/lib/GraphicsSDKCore.lib'
    require(build.read_bytes()==installed.read_bytes() and trial.CANDIDATE.encode() in installed.read_bytes() and trial.SDK.encode() in product.read_bytes(),'Candidate/product installation identity')
    allowed={build,installed,product}
    for folder,expected in (('out/vulkan-pool-churn-sdk',(28,27)),('out/vulkan-pool-churn-product-v2',(3,3)),('out/vulkan-pool-churn-candidate-v2',(3,3))):
        d=root/folder;c=sorted(p for p in d.rglob('CL.read.1.tlog') if 'Release' in p.parts);l=sorted(p for p in d.rglob('link.read.1.tlog') if 'Release' in p.parts);require((len(c),len(l))==expected,'Missing read traces: '+folder)
        for trace in c:
            for raw in audit.parse_tlog(trace.read_bytes()):
                p=pathlib.Path(raw).resolve()
                if p.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
                if system.system_input(p):headers.add(p.as_posix());continue
                require(p.is_relative_to(root),'Unreviewed external input: '+raw);name=p.relative_to(root).as_posix();row=audit.record_file(root,p)
                if name in files:require(all(row[k]==files[name][k] for k in ('path','sha256','bytes')),'Changed compiler input: '+name)
                elif name in frozen:require(row==frozen[name],'Changed copied source: '+name);files[name]=dict(**row,component='graphicssdk')
                elif name.startswith('out/vulkan-pool-churn-install/include/graphics/'):
                    require(p.read_bytes()==(root/'include/graphics'/p.name).read_bytes(),'Changed public ABI');files[name]=dict(**row,component='graphicssdk')
                else:raise ValueError('Unreviewed compiler input: '+name)
            cs.append(audit.record_file(root,trace))
        for trace in l:
            libraries=[]
            for raw in audit.parse_tlog(trace.read_bytes()):
                if not raw.lower().endswith('.lib'):continue
                p=pathlib.Path(raw).resolve();require(p in allowed or system.system_input(p),'Unreviewed link input: '+raw);seen.add(p);libraries.append(dict(path=p.as_posix(),sha256=audit.sha256(p.read_bytes()),system_prerequisite=p not in allowed))
            require(bool(libraries),'Missing link evidence');ls.append(dict(**audit.record_file(root,trace),libraries=libraries))
        projects.extend(audit.record_file(root,p) for p in sorted(d.glob('*.vcxproj')) if p.stem not in ('ALL_BUILD','ZERO_CHECK','INSTALL'))
        artifacts.extend(audit.record_file(root,p) for p in sorted((d/'Release').iterdir()) if p.suffix.lower() in ('.exe','.dll','.lib','.json'))
    require(allowed<=seen,'Product/candidate links not observed')
    for variant,fp in (('product',trial.SDK),('candidate',trial.CANDIDATE)):
        data=(root/f'out/vulkan-pool-churn-{variant}-v2/Release/vulkan_pool_churn.exe').read_bytes();require(fp.encode() in data and trial.fingerprint().encode() in data,'Wrong consumer identity')
    inventory=dict(schema_version=1,profile='windows-vulkan-rebased-pool-churn',scope='Actual isolated rebased SDK and product/candidate consumer compiler/linker reads plus preserved conservative SDK/SDL closure. Read-only binding metadata and guarded synthetic errors; no binary redistribution or symbol reachability claim.',SDK_source_fingerprint=trial.SDK,candidate_SDK_source_fingerprint=trial.CANDIDATE,prior_candidate_SDK_source_fingerprint=trial.prepare.PRIOR,production_adopted=False,components=prior['components'],files=sorted(files.values(),key=lambda r:r['path']),compiler_read_evidence=cs,linker_read_evidence=ls,projects=projects,system_header_count=len(headers),candidate_source_receipt=audit.record_file(root,trial.SOURCE/'candidate-receipt.json'),reused_inventory=audit.record_file(root,root/inherited.GENERATED/'inventory.json'),generated_build_inputs=prior['generated_build_inputs'],artifacts=artifacts+[audit.record_file(root,installed)])
    notices=(root/inherited.GENERATED/'THIRD_PARTY_NOTICES.txt').read_text(encoding='utf-8')+'\n\nOwn rebased adaptive pool candidate, mixed-size/occupancy/churn oracle and read-only Vulkan binding metadata tests reuse the same pinned declarations and own guarded fault layer. No new dependency, vendor modification or binary redistribution.\n'
    return inventory,notices
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--create',action='store_true');g.add_argument('--check',action='store_true');a=p.parse_args();i,n=collect();audit.write_or_check(ROOT/GENERATED,audit.output_files(i,n),a.check);print(f"PASS {len(i['files'])} conservative inputs / {len(i['components'])} components / {len(i['compiler_read_evidence'])} compiler / {len(i['linker_read_evidence'])} linker traces")
