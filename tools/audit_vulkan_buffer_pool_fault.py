"""Actual new SDK/fault-consumer inputs plus the preserved adaptive/SDK/SDL closure."""
import argparse
import json
import pathlib
import audit_licenses as audit
import audit_vulkan as system
import run_vulkan_buffer_pool_fault as trial
ROOT=trial.ROOT
GENERATED=pathlib.Path('licenses/generated/windows-vulkan-buffer-pool-fault-release')
def collect(root=ROOT):
    def load(n):return json.loads((root/n).read_text(encoding='utf-8'))
    def checked(r):
        if audit.record_file(root,root/r['path'])!={k:r[k] for k in ('path','sha256','bytes')}:raise audit.AuditError('Changed frozen input: '+r['path'])
        return r
    prior_path='licenses/generated/windows-vulkan-adaptive-buffer-pool-candidate-release/inventory.json';prior=load(prior_path)
    for r in prior['files']+prior['artifacts']:checked(r)
    files={r['path']:r for r in prior['files']};receipt=load('out/vulkan-buffer-pool-fault-source/candidate-receipt.json')
    for r in receipt['files']:checked(r)
    checked(receipt['patch_tool']);source_map={r['path']:r for r in receipt['files']}
    sdk=audit.sdk_fingerprint(trial.SOURCE)
    if sdk!=receipt['candidate_SDK_source_fingerprint'] or receipt['prior_candidate_SDK_source_fingerprint']!=trial.PRIOR:raise audit.AuditError('Candidate identity mismatch')
    lock=load('benchmarks/competitors/sources.lock.json');audit.verify_checkout(root,'sdl',lock['sdl']['commit'])
    policy=load('licenses/vulkan-presentation-policy.json')['dependency'];audit.verify_checkout(root,'vulkan',policy['commit'],{'vulkan':policy['directory']})
    folders=['out/vulkan-buffer-pool-fault-sdk','out/vulkan-buffer-pool-fault-prior','out/vulkan-buffer-pool-fault-fixed'];traces=[];links=[];projects=[]
    for folder,expected in zip(folders,((29,28),(3,3),(3,3))):
        cs=sorted(p for p in (root/folder).rglob('CL.read.1.tlog') if 'Release' in p.parts);ls=sorted(p for p in (root/folder).rglob('link.read.1.tlog') if 'Release' in p.parts)
        if (len(cs),len(ls))!=expected:raise audit.AuditError('Incomplete compiler/linker reads: '+folder)
        traces+=cs;links+=ls;projects += [audit.record_file(root,p) for p in sorted((root/folder).glob('*.vcxproj')) if p.stem not in ('ALL_BUILD','ZERO_CHECK','INSTALL')]
    own={p.relative_to(root).as_posix() for p in (root/'experiments/vulkan_buffer_pool_fault').iterdir() if p.suffix in ('.hpp','.cpp')};system_headers=set()
    for p in traces:
        for raw in audit.parse_tlog(p.read_bytes()):
            file=pathlib.Path(raw).resolve()
            if file.suffix.casefold() in ('.obj','.pch','.pdb','.tlog'):continue
            if system.system_input(file):system_headers.add(file.as_posix());continue
            if not file.is_relative_to(root):raise audit.AuditError('External compiler input: '+raw)
            name=file.relative_to(root).as_posix()
            if name in files:checked(files[name]);continue
            if name in source_map:row=dict(**checked(source_map[name]),component='graphicssdk')
            elif name in own:row=dict(**audit.record_file(root,file),component='graphicssdk')
            elif name.startswith('out/vulkan-buffer-pool-fault-install/include/graphics/'):
                if file.read_bytes()!=(root/'include/graphics'/file.name).read_bytes():raise audit.AuditError('Public header ABI changed')
                row=dict(**audit.record_file(root,file),component='graphicssdk')
            elif any(name==r['path'] for r in prior['generated_build_inputs']):continue
            else:raise audit.AuditError('Unreviewed compiler input: '+name)
            files[name]=row
    build=root/'out/vulkan-buffer-pool-fault-sdk/Release/GraphicsSDKCore.lib';installed=root/'out/vulkan-buffer-pool-fault-install/lib/GraphicsSDKCore.lib';old=root/'out/vulkan-adaptive-buffer-pool-install/lib/GraphicsSDKCore.lib'
    if build.read_bytes()!=installed.read_bytes() or sdk.encode() not in build.read_bytes() or trial.PRIOR.encode() not in old.read_bytes():raise audit.AuditError('Wrong linked SDK variant')
    allowed={build.resolve(),installed.resolve(),old.resolve()};seen=set();link_evidence=[]
    for p in links:
        libraries=[]
        for raw in audit.parse_tlog(p.read_bytes()):
            if not raw.casefold().endswith('.lib'):continue
            file=pathlib.Path(raw).resolve()
            if file not in allowed and not system.system_input(file):raise audit.AuditError('Unreviewed link input: '+raw)
            seen.add(file);libraries.append(dict(path=file.as_posix(),sha256=audit.sha256(file.read_bytes()),system_prerequisite=file not in allowed))
        if not libraries:raise audit.AuditError('Missing linker input evidence')
        link_evidence.append(dict(**audit.record_file(root,p),libraries=libraries))
    if not allowed<=seen:raise audit.AuditError('Prior and hardened SDKs not observed in linker reads')
    artifacts=[audit.record_file(root,p) for folder in folders for p in sorted((root/folder/'Release').iterdir()) if p.suffix.lower() in ('.exe','.dll','.lib','.json')]+[audit.record_file(root,installed)]
    inventory=dict(schema_version=1,profile='windows-vulkan-buffer-pool-guarded-faults',scope='Own synthetic error layer/client and isolated hardened SDK actual compiler/linker inputs, with preserved adaptive/SDK/SDL closure. No symbol reachability or binary redistribution.',SDK_source_fingerprint=audit.sdk_fingerprint(root),candidate_SDK_source_fingerprint=sdk,prior_candidate_SDK_source_fingerprint=trial.PRIOR,production_adopted=False,components=prior['components'],files=sorted(files.values(),key=lambda r:r['path']),compiler_read_evidence=[audit.record_file(root,p) for p in traces],linker_read_evidence=link_evidence,projects=projects,system_header_count=len(system_headers),candidate_source_receipt=audit.record_file(root,trial.SOURCE/'candidate-receipt.json'),reused_inventory=audit.record_file(root,root/prior_path),generated_build_inputs=prior['generated_build_inputs'],artifacts=artifacts)
    notices=(root/'licenses/generated/windows-vulkan-adaptive-buffer-pool-candidate-release/THIRD_PARTY_NOTICES.txt').read_text(encoding='utf-8')+'\n\nOwn guarded Vulkan synthetic-failure layer and isolated output-ownership fix. No new third-party dependency or candidate binary redistribution.\n'
    return inventory,notices
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,default=GENERATED);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--create',action='store_true');g.add_argument('--check',action='store_true');a=p.parse_args();i,n=collect();audit.write_or_check(a.output,audit.output_files(i,n),a.check);print(f"PASS {len(i['files'])} conservative inputs / {len(i['components'])} components / {len(i['compiler_read_evidence'])} compiler / {len(i['linker_read_evidence'])} linker traces")
