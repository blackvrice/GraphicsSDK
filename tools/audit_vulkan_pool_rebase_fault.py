"""Actual installed rebased SDK consumers and preserved reviewed dependency closure."""
import argparse
import pathlib
import audit_licenses as audit
import audit_vulkan as system
import audit_vulkan_pool_churn as prior
import run_vulkan_pool_rebase_fault as trial
ROOT=trial.ROOT
GENERATED=pathlib.Path('licenses/generated/windows-vulkan-pool-rebase-fault-release')
def collect(root=ROOT):
    inventory,_=prior.collect(root);files={r['path']:r for r in inventory['files']};require=trial.require
    for name in ('experiments/vulkan_pool_rebase_fault/main.cpp','experiments/vulkan_pool_rebase_fault/CMakeLists.txt'):files[name]=dict(**audit.record_file(root,root/name),component='graphicssdk')
    cs=[];ls=[];projects=[];artifacts=[];headers=set();seen=False;installed=root/'out/vulkan-pool-churn-install/lib/GraphicsSDKCore.lib'
    require(trial.audit.sdk_fingerprint(root)==trial.SDK and trial.audit.sdk_fingerprint(trial.SOURCE)==trial.CANDIDATE and trial.CANDIDATE.encode() in installed.read_bytes(),'Reviewed candidate/product identity')
    for kind,expected in (('regression',(3,3)),('mixed',(2,2))):
        folder=root/f'out/vulkan-pool-rebase-fault-{kind}';c=sorted(p for p in folder.rglob('CL.read.1.tlog') if 'Release' in p.parts);l=sorted(p for p in folder.rglob('link.read.1.tlog') if 'Release' in p.parts);require((len(c),len(l))==expected,'Missing consumer traces')
        for trace in c:
            for raw in audit.parse_tlog(trace.read_bytes()):
                p=pathlib.Path(raw).resolve()
                if p.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
                if system.system_input(p):headers.add(p.as_posix());continue
                require(p.is_relative_to(root),'External compiler input: '+raw);name=p.relative_to(root).as_posix();row=audit.record_file(root,p)
                require(name in files and all(row[k]==files[name][k] for k in ('path','sha256','bytes')),'Unreviewed/changed compiler input: '+name)
            cs.append(audit.record_file(root,trace))
        for trace in l:
            libs=[]
            for raw in audit.parse_tlog(trace.read_bytes()):
                if not raw.lower().endswith('.lib'):continue
                p=pathlib.Path(raw).resolve();require(p==installed or system.system_input(p),'Unreviewed library: '+raw);seen=seen or p==installed;libs.append(dict(path=p.as_posix(),sha256=audit.sha256(p.read_bytes()),system_prerequisite=p!=installed))
            require(bool(libs),'Missing library evidence');ls.append(dict(**audit.record_file(root,trace),libraries=libs))
        projects.extend(audit.record_file(root,p) for p in sorted(folder.glob('*.vcxproj')) if p.stem not in ('ALL_BUILD','ZERO_CHECK','RUN_TESTS'))
        artifacts.extend(audit.record_file(root,p) for p in sorted((folder/'Release').iterdir()) if p.suffix.lower() in ('.exe','.dll','.lib','.json'))
        exe=folder/'Release'/('vulkan_buffer_fault.exe' if kind=='regression' else 'vulkan_rebase_fault.exe');data=exe.read_bytes();fp=trial.regression.fingerprint() if kind=='regression' else trial.fingerprint();require(trial.CANDIDATE.encode() in data and fp.encode() in data,'Wrong consumer/SDK identity')
    require(seen,'Installed rebased SDK not observed')
    inventory.update(profile='windows-vulkan-rebased-allocation-faults',scope='New full historical fault consumer and mixed-context binding snapshot consumer linked to the unchanged rebased SDK; actual read traces plus conservative preserved closure. Synthetic OOM only; no timing, physical-memory or binary redistribution claim.',files=sorted(files.values(),key=lambda r:r['path']),new_compiler_read_evidence=cs,new_linker_read_evidence=ls,new_projects=projects,new_system_header_count=len(headers),new_artifacts=artifacts+[audit.record_file(root,installed)],reused_churn_inventory=audit.record_file(root,root/prior.GENERATED/'inventory.json'),new_source_fingerprint=trial.fingerprint(),regression_source_fingerprint=trial.regression.fingerprint())
    notices=(root/prior.GENERATED/'THIRD_PARTY_NOTICES.txt').read_text(encoding='utf-8')+'\n\nOwn mixed-context allocation-failure/fallback consumer uses the unchanged rebased SDK and existing own guarded layer/binding observer. No new third-party dependency or vendor modification; no binaries redistributed.\n'
    return inventory,notices
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--create',action='store_true');g.add_argument('--check',action='store_true');a=p.parse_args();i,n=collect();audit.write_or_check(ROOT/GENERATED,audit.output_files(i,n),a.check);print(f"PASS {len(i['files'])} conservative inputs / {len(i['components'])} components / 5 new compiler / 5 new linker traces")
