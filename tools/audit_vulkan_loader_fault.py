"""Conservative inherited product inventory plus actual loader-test compiler/linker reads."""
import argparse
import json
import pathlib
import audit_licenses as audit
import audit_vulkan as system
import audit_vulkan_creation_ownership as prior
import run_vulkan_loader_fault as native
ROOT=native.ROOT
GENERATED=pathlib.Path('licenses/generated/windows-vulkan-loader-fault-release')
def collect(root=ROOT):
    inventory,_=prior.collect(root)
    require=native.require
    require(audit.sdk_fingerprint(root)==native.SDK,'Product changed')
    files={r['path']:r for r in inventory['files']};cs=[];ls=[];projects=[];headers=set();artifacts=[]
    own={p.relative_to(root).as_posix() for p in (root/'tests/vulkan_loader_fault').iterdir() if p.suffix in ('.cpp','.hpp') or p.name=='CMakeLists.txt'}
    own|={'benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp'}
    for name in own:files[name]=dict(**audit.record_file(root,root/name),component='graphicssdk')
    # Same verified Vulkan-Headers pin/Apache-2.0 component. The layer needs the
    # public aggregator and Windows declarations, reviewed explicitly here.
    for name in ('.deps/Vulkan-Headers/include/vulkan/vulkan.h','.deps/Vulkan-Headers/include/vulkan/vulkan_win32.h'):
        row=dict(**audit.record_file(root,root/name),component='vulkan-headers')
        if name in files:require(files[name]==row,'Changed pinned Windows Vulkan declarations')
        files[name]=row
    allowed={root/'out/vulkan-creation-install/lib/GraphicsSDKCore.lib',root/'out/vulkan-presentation-install/lib/GraphicsSDKCore.lib'};seen=set()
    for variant in ('before','after'):
        folder=root/f'out/vulkan-loader-fault-{variant}-v4'
        c=sorted(p for p in folder.rglob('CL.read.1.tlog') if 'Release' in p.parts);l=sorted(p for p in folder.rglob('link.read.1.tlog') if 'Release' in p.parts)
        require((len(c),len(l))==(3,3),'Missing test consumer traces')
        for trace in c:
            for raw in audit.parse_tlog(trace.read_bytes()):
                p=pathlib.Path(raw).resolve()
                if p.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
                if system.system_input(p):headers.add(p.as_posix());continue
                require(p.is_relative_to(root),'External compiler input: '+raw);name=p.relative_to(root).as_posix();r=audit.record_file(root,p)
                require(name in files,'Unreviewed compiler input: '+name)
                require(all(r[k]==files[name][k] for k in ('path','sha256','bytes')),'Changed compiler input: '+name)
            cs.append(audit.record_file(root,trace))
        for trace in l:
            libraries=[]
            for raw in audit.parse_tlog(trace.read_bytes()):
                if not raw.lower().endswith('.lib'):continue
                p=pathlib.Path(raw).resolve();require(p in allowed or system.system_input(p),'Unreviewed library: '+raw)
                seen.add(p);libraries.append(dict(path=p.as_posix(),sha256=audit.sha256(p.read_bytes()),system_prerequisite=p not in allowed))
            require(bool(libraries),'Missing linker inputs');ls.append(dict(**audit.record_file(root,trace),libraries=libraries))
        projects.extend(audit.record_file(root,p) for p in sorted(folder.glob('*.vcxproj')) if p.stem not in ('ALL_BUILD','ZERO_CHECK'))
        artifacts.extend(audit.record_file(root,p) for p in sorted((folder/'Release').iterdir()) if p.suffix.lower() in ('.exe','.dll','.lib','.json'))
    require(allowed<=seen,'Before/after installed SDK not observed')
    for variant,fp in (('before',native.OLD),('after',native.SDK)):
        binary=(root/f'out/vulkan-loader-fault-{variant}-v4/Release/vulkan_loader_fault.exe').read_bytes();require(fp.encode() in binary and native.fingerprint().encode() in binary,'Wrong consumer/source identity')
    inventory.update(profile='windows-vulkan-loader-fault',scope='Unmodified product and preserved conservative closure; new before/after installed SDK consumers and own explicit loader-test layer. All new non-system compiler inputs must match the reviewed inventory.',files=sorted(files.values(),key=lambda r:r['path']),new_compiler_read_evidence=cs,new_linker_read_evidence=ls,new_projects=projects,new_system_header_count=len(headers),new_artifacts=artifacts,loader_source_fingerprint=native.fingerprint(),product_changed=False,reused_creation_inventory=audit.record_file(root,root/prior.GENERATED/'inventory.json'))
    notices=(root/prior.GENERATED/'THIRD_PARTY_NOTICES.txt').read_text(encoding='utf-8')+'\n\nOwn loader-failure layer/client and CPU plan contracts use the same reviewed Vulkan-Headers declarations and Windows system prerequisites. No new third-party source or binary redistribution. Synthetic test handles never reach driver cleanup.\n'
    return inventory,notices
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--create',action='store_true');g.add_argument('--check',action='store_true');a=p.parse_args();i,n=collect();audit.write_or_check(ROOT/GENERATED,audit.output_files(i,n),a.check);print(f"PASS {len(i['files'])} conservative inputs / {len(i['components'])} components / 6 new compiler / 6 new linker traces")
