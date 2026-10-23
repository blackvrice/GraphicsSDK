"""New SDK and comparison build reads; frozen before/SDL/trial artifacts stay independent."""
import argparse
import json
import pathlib
import audit_licenses as a
import audit_vulkan as av
import run_vulkan_upload as u
import vulkan_upload_experiment as experiment

ROOT=pathlib.Path(__file__).resolve().parents[1]
OWN={'benchmarks/vulkan_upload_profile/main.cpp','benchmarks/vulkan_upload/main.cpp','benchmarks/vulkan_upload/Identity.hpp','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp'}
PROJECTS={'before-profile':('out/vulkan-upload-profile-before','vulkan_upload_profile'),
          'trial-profile':('out/vulkan-upload-profile-trial','vulkan_upload_profile'),
          'after-profile':('out/vulkan-upload-profile-after','vulkan_upload_profile'),
          'after-benchmark':('out/vulkan-upload-memory-bench','vulkan_upload'),
          'memory-probe':('out/wsi-probe/build','probe')}
def collect():
    root=ROOT
    def load(p):return json.loads((root/p).read_text(encoding='utf-8'))
    def rec(p):return a.record_file(root,p)
    sdk_dir=root/'licenses/generated/windows-sdk-vulkan-upload-memory-release';sdk=load('licenses/generated/windows-sdk-vulkan-upload-memory-release/inventory.json')
    candidate,archived=experiment.candidate()
    old=load('licenses/generated/windows-benchmarks-vulkan-upload-release/inventory.json');sdl=load('licenses/generated/windows-benchmarks-release/inventory.json')
    def check(r):
        if r['path'] in archived:
            data=archived[r['path']]
            if a.sha256(data)!=r['sha256'] or len(data)!=r['bytes']:raise a.AuditError('Candidate source changed: '+r['path'])
        elif rec(root/r['path'])!={k:r[k] for k in ('path','sha256','bytes')}:raise a.AuditError('Frozen input changed: '+r['path'])
    if sdk['SDK_source_fingerprint']!=candidate['SDK_source_fingerprint']:raise a.AuditError('Candidate SDK inventory stale')
    pins=load('benchmarks/competitors/sources.lock.json');a.verify_checkout(root,'sdl',pins['sdl']['commit'])
    policy=load('licenses/vulkan-upload-memory-policy.json');a.verify_checkout(root,'vk',policy['dependency']['commit'],{'vk':policy['dependency']['directory']})
    files={r['path']:r for r in sdk['files']}
    for r in sdk['files']:check(r)
    for r in sdl['files']:
        if r['path'].startswith('.deps/SDL/'):check(r);files[r['path']]=r
    generated={r['path'].casefold():r for r in sdl['generated_build_inputs'] if r['path'].startswith('out/sdl/')}
    for r in generated.values():check(r)
    old_records={r['path']:r for r in old['artifacts']}
    for r in old_records.values():check(r)
    trial=load('benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory/profile-trial/artifacts.json')
    for r in trial:check(r)
    libraries={root/'out/vulkan-presentation-install/lib/GraphicsSDKCore.lib',root/'out/vulkan-upload-memory-install/lib/GraphicsSDKCore.lib',root/'out/vulkan-upload-map-trial-install/lib/GraphicsSDKCore.lib',root/'out/sdl/Release/SDL3-static.lib'}
    trial_lib=root/'out/vulkan-upload-map-trial/Release/GraphicsSDKCore.lib'
    if trial_lib.read_bytes()!=(root/'out/vulkan-upload-map-trial-install/lib/GraphicsSDKCore.lib').read_bytes():raise a.AuditError('Trial install differs')
    artifact_paths=set(libraries)|{trial_lib};compiler=[];linker=[];projects=[]
    for name,(folder,target) in PROJECTS.items():
        folder=root/folder;traces=[p for p in folder.rglob('CL.read.1.tlog') if 'Release' in p.parts];links=[p for p in folder.rglob('link.read.1.tlog') if 'Release' in p.parts]
        if len(traces)!=1 or len(links)!=1:raise a.AuditError('Unexpected project trace count '+name)
        compiler.append(rec(traces[0]));projects.append(rec(folder/(target+'.vcxproj')));artifact_paths.add(folder/'Release'/(target+'.exe'))
        for raw in a.parse_tlog(traces[0].read_bytes()):
            p=pathlib.Path(raw).resolve()
            if p.suffix.casefold() in ('.obj','.pch','.pdb','.tlog') or av.system_input(p):continue
            if not p.is_relative_to(root):raise a.AuditError('External compiler input '+str(p))
            relative=p.relative_to(root).as_posix()
            if relative in files:continue
            if relative.casefold() in generated:continue
            if relative in OWN:files[relative]=dict(**rec(p),component='graphicssdk')
            elif any(p.is_relative_to(lib.parent.parent/'include/graphics') for lib in libraries if lib.name=='GraphicsSDKCore.lib'):
                if p.read_bytes()!=(root/'include/graphics'/p.name).read_bytes():raise a.AuditError('Installed header changed')
                files[relative]=dict(**rec(p),component='graphicssdk')
            elif relative=='out/wsi-probe/probe.cpp':
                snapshot=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory/native-memory-probe/probe.cpp'
                if p.read_bytes()!=snapshot.read_bytes():raise a.AuditError('Memory probe source snapshot differs')
                files[relative]=dict(**rec(p),component='graphicssdk',source_snapshot=snapshot.relative_to(root).as_posix())
            else:raise a.AuditError('Unreviewed compiler input '+relative)
        read=[]
        for raw in a.parse_tlog(links[0].read_bytes()):
            if not raw.casefold().endswith('.lib'):continue
            p=pathlib.Path(raw).resolve()
            if p not in libraries and not av.system_input(p):raise a.AuditError('Unreviewed linked library '+str(p))
            read.append(dict(path=p.as_posix(),sha256=a.sha256(p.read_bytes()),system_prerequisite=p not in libraries))
        if not read:raise a.AuditError('Missing linker library evidence')
        linker.append(dict(**rec(links[0]),libraries=read))
    for p in artifact_paths:
        if not p.is_file():raise a.AuditError('Missing artifact '+str(p))
    # Original comparison program and installed baseline are already hash frozen.
    check(next(r for r in old['artifacts'] if r['path']=='out/vulkan-upload-final/Release/vulkan_upload.exe'))
    notice=a.text_file(sdk_dir/'THIRD_PARTY_NOTICES.txt')+'\n\nREUSED SDL STATIC COMPARISON NOTICES (historical text reproduced, no new vendor binaries bundled):\n'+a.text_file(root/'licenses/generated/windows-benchmarks-vulkan-upload-release/THIRD_PARTY_NOTICES.txt')
    inventory=dict(schema_version=1,profile='windows-vulkan-upload-memory-comparison-release',SDK_source_fingerprint=sdk['SDK_source_fingerprint'],benchmark_source_fingerprint=u.fingerprint(),
        scope='Rejected cached-memory SDK candidate plus conservative reused SDL compiler closure and actual before/mapping-trial/cached-candidate profile, candidate benchmark and property probe compiler/linker reads. Candidate sources are hash-verified in their separate archive; production SDK exactly matches the baseline.',
        reused_inventories=[rec(sdk_dir/'inventory.json'),rec(root/'licenses/generated/windows-benchmarks-vulkan-upload-release/inventory.json'),rec(root/'licenses/generated/windows-benchmarks-release/inventory.json')],
        components=old['components'],files=sorted(files.values(),key=lambda r:r['path']),compiler_read_evidence=compiler,linker_read_evidence=linker,projects=projects,artifacts=[rec(p) for p in sorted(artifact_paths)],
        rejected_mapping_trial=True,rejected_cached_memory_candidate=True,candidate_source_receipt=rec(experiment.FOLDER/'receipt.json'),production_SDK_source_fingerprint=a.sdk_fingerprint(root),new_vendor_sources=False,binary_distribution=False,SDK_API_changed=False,exclusions=['GPU hardware profiling/counter attribution','Linux native runtime','Long memory acceptance and historical Intel AV investigation','Whole-SDK ranking'])
    return inventory,notice
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--create',action='store_true');g.add_argument('--check',action='store_true');x=p.parse_args()
    inventory,notices=collect();a.write_or_check(x.output,a.output_files(inventory,notices),x.check)
    print(f"PASS: {len(inventory['files'])} conservative inputs / {len(inventory['components'])} components / 5 compiler and 5 linker logs; distinct baseline/rejected experiment artifacts verified")
