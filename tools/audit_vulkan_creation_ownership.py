"""Audit actual product/consumer reads and immutable historical inputs across a product fix."""
import argparse
import json
import pathlib
import audit_licenses as audit
import audit_vulkan as system
import vulkan_creation_provenance as history
ROOT=history.ROOT
GENERATED=pathlib.Path('licenses/generated/windows-vulkan-creation-ownership-release')
def collect(root=ROOT):
    def load(n):return json.loads((root/n).read_text(encoding='utf-8'))
    prior_path='licenses/generated/windows-vulkan-buffer-pool-fault-release/inventory.json';prior=load(prior_path);files={};relocated=[]
    for r in prior['files']:
        resolved=history.resolve(r,root)
        if resolved['path']!=r['path']:relocated.append(dict(original={k:r[k] for k in ('path','sha256','bytes')},snapshot=resolved))
        files[resolved['path']]=dict(**resolved,component=r['component'])
    for r in prior['artifacts']:history.resolve(r,root)
    receipt=load('out/vulkan-creation-before-source/receipt.json')
    for r in receipt['files']:history.resolve(r,root)
    assert receipt['SDK_source_fingerprint']==history.OLD
    policy=load('licenses/vulkan-presentation-policy.json')['dependency'];audit.verify_checkout(root,'vulkan',policy['commit'],{'vulkan':policy['directory']})
    lock=load('benchmarks/competitors/sources.lock.json');audit.verify_checkout(root,'sdl',lock['sdl']['commit'])
    folders=['out/vulkan-creation-product','out/vulkan-creation-cpu-before-v2','out/vulkan-creation-cpu-after-v2','out/vulkan-creation-native-before','out/vulkan-creation-native-after'];cs=[];ls=[];projects=[]
    for folder,expected in zip(folders,((28,27),(1,1),(1,1),(3,3),(3,3))):
        c=sorted(p for p in (root/folder).rglob('CL.read.1.tlog') if 'Release' in p.parts);l=sorted(p for p in (root/folder).rglob('link.read.1.tlog') if 'Release'in p.parts)
        if (len(c),len(l))!=expected:raise audit.AuditError('Missing product/consumer read traces: '+folder)
        cs+=c;ls+=l;projects += [audit.record_file(root,p) for p in sorted((root/folder).glob('*.vcxproj')) if p.stem not in ('ALL_BUILD','ZERO_CHECK','INSTALL')]
    own={p.relative_to(root).as_posix() for folder in ('src','include','tests/vulkan_creation_fault') for p in (root/folder).rglob('*') if p.suffix in ('.cpp','.hpp')}|{'tests/VulkanCreationTests.cpp','tests/VulkanBindingSupport.hpp','tests/fixtures/BindingFixtures.hpp'}
    frozen={r['path']:r for r in receipt['files']};system_headers=set()
    for trace in cs:
        for raw in audit.parse_tlog(trace.read_bytes()):
            file=pathlib.Path(raw).resolve()
            if file.suffix.casefold() in ('.obj','.pch','.pdb','.tlog'):continue
            if system.system_input(file):system_headers.add(file.as_posix());continue
            if not file.is_relative_to(root):raise audit.AuditError('Unreviewed external input: '+raw)
            name=file.relative_to(root).as_posix()
            if name in files:
                if audit.record_file(root,file)!={k:files[name][k] for k in ('path','sha256','bytes')}:raise audit.AuditError('Changed reused compiler input: '+name)
                continue
            if name in own:row=dict(**audit.record_file(root,file),component='graphicssdk')
            elif name in frozen:row=dict(**history.resolve(frozen[name],root),component='graphicssdk')
            elif name.startswith('out/vulkan-creation-install/include/graphics/'):
                if file.read_bytes()!=(root/'include/graphics'/file.name).read_bytes():raise audit.AuditError('Installed public header differs')
                row=dict(**audit.record_file(root,file),component='graphicssdk')
            elif any(name==r['path'] for r in prior['generated_build_inputs']):continue
            else:raise audit.AuditError('Unreviewed compiler input: '+name)
            files[name]=row
    registry=root/'.deps/Vulkan-Headers/registry/vk.xml';files[registry.relative_to(root).as_posix()]=dict(**audit.record_file(root,registry),component='vulkan-headers')
    sdk=audit.sdk_fingerprint(root);build=root/'out/vulkan-creation-product/Release/GraphicsSDKCore.lib';installed=root/'out/vulkan-creation-install/lib/GraphicsSDKCore.lib';old=root/'out/vulkan-presentation-install/lib/GraphicsSDKCore.lib'
    if build.read_bytes()!=installed.read_bytes() or sdk.encode() not in build.read_bytes() or history.OLD.encode() not in old.read_bytes():raise audit.AuditError('Wrong installed product variant')
    allowed={build.resolve(),installed.resolve(),old.resolve()};seen=set();link_evidence=[]
    for trace in ls:
        libraries=[]
        for raw in audit.parse_tlog(trace.read_bytes()):
            if not raw.casefold().endswith('.lib'):continue
            file=pathlib.Path(raw).resolve()
            if file not in allowed and not system.system_input(file):raise audit.AuditError('Unreviewed link input: '+raw)
            seen.add(file);libraries.append(dict(path=file.as_posix(),sha256=audit.sha256(file.read_bytes()),system_prerequisite=file not in allowed))
        if not libraries:raise audit.AuditError('Missing linker evidence')
        link_evidence.append(dict(**audit.record_file(root,trace),libraries=libraries))
    if not allowed<=seen:raise audit.AuditError('Before/after libraries not observed')
    artifacts=[audit.record_file(root,p) for folder in folders for p in sorted((root/folder/'Release').iterdir()) if p.suffix.lower() in ('.exe','.dll','.lib','.json')]+[audit.record_file(root,installed)]
    inventory=dict(schema_version=1,profile='windows-vulkan-product-creation-ownership',scope='Actual product and before/after CPU/hardware consumer reads, generator registry input, and conservative preserved SDK/SDL closure. Historical changed product bytes resolved only through the verified pre-fix snapshot.',SDK_source_fingerprint=sdk,prior_SDK_source_fingerprint=history.OLD,product_fix_adopted=True,buffer_pool_adopted=False,components=prior['components'],files=sorted(files.values(),key=lambda r:r['path']),historical_relocated_inputs=relocated,compiler_read_evidence=[audit.record_file(root,p) for p in cs],linker_read_evidence=link_evidence,projects=projects,system_header_count=len(system_headers),prior_source_receipt=audit.record_file(root,root/'out/vulkan-creation-before-source/receipt.json'),reused_inventory=audit.record_file(root,root/prior_path),generated_build_inputs=prior['generated_build_inputs'],artifacts=artifacts)
    notices=(root/'licenses/generated/windows-vulkan-buffer-pool-fault-release/THIRD_PARTY_NOTICES.txt').read_text(encoding='utf-8')+'\n\nProduct Vulkan creation-output ownership fix and own actual-implementation PFN tests. Reviewed Vulkan registry supplies exact signatures; stubs are own generated test bodies. No new dependency or binary redistribution.\n'
    return inventory,notices
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,default=GENERATED);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--create',action='store_true');g.add_argument('--check',action='store_true');a=p.parse_args();i,n=collect();audit.write_or_check(a.output,audit.output_files(i,n),a.check);print(f"PASS {len(i['files'])} conservative inputs / {len(i['components'])} components / {len(i['compiler_read_evidence'])} compiler / {len(i['linker_read_evidence'])} linker / {len(i['historical_relocated_inputs'])} historical paths relocated to verified snapshot")
