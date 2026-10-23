"""Audit the new executable plus the frozen SDK and SDL static input inventories.

No SDK/SDL rebuild or new binary redistribution is implied by this profile.
"""
import argparse
import json
import pathlib
import audit_licenses as audit
import audit_vulkan
import run_vulkan_draw as upload

ROOT=pathlib.Path(__file__).resolve().parents[1]
BUILD=pathlib.Path('out/vulkan-draw-benchmark-vulkan11')
OWN={'benchmarks/vulkan_draw/main.cpp','benchmarks/vulkan_draw/Frame.hpp','tests/VulkanDrawBenchmarkTests.cpp','tests/fixtures/SpirvFixtures.hpp','benchmarks/vulkan_upload/Identity.hpp','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp'}

def collect(root=ROOT):
    def load(p):return json.loads((root/p).read_text(encoding='utf-8'))
    def checked(r):
        if audit.record_file(root,root/r['path'])!=r:raise audit.AuditError('Frozen input changed: '+r['path'])
        return r
    sdk_path='licenses/generated/windows-sdk-vulkan-presentation-release/inventory.json'
    sdl_path='licenses/generated/windows-benchmarks-release/inventory.json'
    sdk=load(sdk_path);legacy=load(sdl_path);policy=load('licenses/policy.json');pins=load('benchmarks/competitors/sources.lock.json')
    audit.verify_checkout(root,'sdl',pins['sdl']['commit'])
    if pins['sdl']['commit']!=policy['reviewed_commits']['sdl']:raise audit.AuditError('SDL policy pin mismatch')
    vulkan=load('licenses/vulkan-presentation-policy.json')['dependency']
    audit.verify_checkout(root,'vulkan',vulkan['commit'],{'vulkan':vulkan['directory']})
    sdk_source=audit.sdk_fingerprint(root)
    if sdk_source!=sdk['SDK_source_fingerprint']:raise audit.AuditError('SDK sources changed; this profile requires the frozen installed SDK')
    sdk_lib=checked(next(r for r in sdk['artifacts'] if r['path']=='out/vulkan-presentation/Release/GraphicsSDKCore.lib'))
    installed=root/'out/vulkan-presentation-install/lib/GraphicsSDKCore.lib'
    if installed.read_bytes()!=(root/sdk_lib['path']).read_bytes():raise audit.AuditError('Installed SDK library differs')
    sdl_lib=checked(next(r for r in legacy['artifacts'] if r['path']=='out/sdl/Release/SDL3-static.lib'))
    files={r['path']:r for r in sdk['files']}
    for r in sdk['files']:checked({k:r[k] for k in ('path','sha256','bytes')})
    sdl_files=[r for r in legacy['files'] if r['path'].startswith('.deps/SDL/')]
    for r in sdl_files:checked({k:r[k] for k in ('path','sha256','bytes')});files[r['path']]=r
    generated=[checked(r) for r in legacy['generated_build_inputs'] if r['path'].startswith('out/sdl/')]
    generated_names={r['path'].casefold() for r in generated}
    headers={vulkan['directory']+'/'+r['path']:r for r in vulkan['reviewed_headers']}
    traces=sorted(p for p in (root/BUILD).rglob('CL.read.1.tlog') if 'Release' in p.parts)
    links=sorted(p for p in (root/BUILD).rglob('link.read.1.tlog') if 'Release' in p.parts)
    if len(traces)!=2 or len(links)!=2:raise audit.AuditError('Unexpected/missing benchmark build logs')
    system=set();new_files=[]
    for raw in (r for trace in traces for r in audit.parse_tlog(trace.read_bytes())):
        p=pathlib.Path(raw).resolve()
        if p.suffix.casefold() in ('.obj','.pch','.pdb','.tlog'):continue
        if audit_vulkan.system_input(p):system.add(p.as_posix());continue
        if not p.is_relative_to(root):raise audit.AuditError('Unreviewed external input: '+raw)
        relative=p.relative_to(root).as_posix()
        if relative in files:continue
        if relative.casefold() in generated_names:continue
        if relative in OWN:row=dict(**audit.record_file(root,p),component='graphicssdk')
        elif relative.startswith('out/vulkan-presentation-install/include/graphics/'):
            original=root/'include/graphics'/p.name
            if p.read_bytes()!=original.read_bytes():raise audit.AuditError('Changed installed header')
            row=dict(**audit.record_file(root,p),component='graphicssdk')
        elif relative in headers and audit.sha256(p.read_bytes())==headers[relative]['sha256']:
            row=dict(**audit.record_file(root,p),component='vulkan-headers')
        else:raise audit.AuditError('Unreviewed compiler input: '+relative)
        files[relative]=row;new_files.append(row)
    seen=set();allowed={installed.resolve(),(root/sdl_lib['path']).resolve()};link_evidence=[]
    for log in links:
        linked=[]
        for raw in audit.parse_tlog(log.read_bytes()):
            if not raw.casefold().endswith('.lib'):continue
            p=pathlib.Path(raw).resolve()
            if p not in allowed and not audit_vulkan.system_input(p):raise audit.AuditError('Unreviewed linked library: '+raw)
            seen.add(p);linked.append(dict(path=p.as_posix(),sha256=audit.sha256(p.read_bytes()),system_prerequisite=p not in allowed))
        if not linked:raise audit.AuditError('Empty linker evidence')
        link_evidence.append(dict(**audit.record_file(root,log),libraries=linked))
    if not allowed<=seen:raise audit.AuditError('Expected SDK/SDL libraries missing from actual linker reads')
    reused=[checked(r) for r in sdk['compiler_read_evidence']]+[checked(r) for r in legacy['compiler_read_evidence'] if r['path'].startswith('out/sdl/')]
    components={r['id']:r for r in sdk['components']};notices={}
    for r in sdl_files:
        relative=r['path'].removeprefix('.deps/SDL/');component=audit.component_for(policy,'sdl',relative);key=component['id'];components[key]=component
        blocks=notices.setdefault(key,set());blocks.update(audit.source_notices(audit.text_file(root/r['path'])))
        for name in component.get('root_notices',[]):blocks.add(audit.text_file(root/'.deps/SDL'/name).strip())
        for name in component.get('extra_notices',[]):blocks.add(audit.text_file(root/name).strip())
    if 'sdl-khronos-dual' in notices:notices['sdl-khronos-dual'].update(audit.source_notices(audit.text_file(root/'.deps/SDL/include/SDL3/SDL_opengles2_khrplatform.h')))
    text=audit.text_file(root/'licenses/generated/windows-sdk-vulkan-presentation-release/THIRD_PARTY_NOTICES.txt')
    text+='\n\nVULKAN DRAW BENCHMARK: reused SDL static compiler closure (including unused/preprocessed members).\n'
    for key,blocks in sorted(notices.items()):text+='\n'+key+' — selected '+components[key]['selected']+'\n\n'+'\n\n'.join(sorted(blocks))+'\n'
    exe=root/BUILD/'Release/vulkan_draw.exe';fingerprint=upload.fingerprint(root)
    if fingerprint.encode() not in exe.read_bytes() or sdk_source.encode() not in exe.read_bytes():raise audit.AuditError('Executable uses stale benchmark/SDK source identity')
    inventory=dict(schema_version=1,profile='windows-vulkan-draw-reused-sdk-sdl-release',scope='New benchmark compiler/linker reads plus conservative previously frozen SDK and SDL static compiler closures; no native symbol reachability or legal approval implied.',
        SDK_source_fingerprint=sdk_source,benchmark_source_fingerprint=fingerprint,SDK_rebuilt=False,SDL_rebuilt=False,source_dependency_pins=dict(sdl=pins['sdl']['commit'],vulkan_headers=vulkan['commit']),
        policy=audit.record_file(root,root/'licenses/policy.json'),reused_inventories=[audit.record_file(root,root/sdk_path),audit.record_file(root,root/sdl_path)],components=list(components.values()),files=sorted(files.values(),key=lambda r:r['path']),
        new_compiler_read_evidence=[audit.record_file(root,p) for p in traces],reused_compiler_read_evidence=reused,generated_build_inputs=generated,
        linker_read_evidence=link_evidence,projects=[audit.record_file(root,root/BUILD/n) for n in ('vulkan_draw.vcxproj','vulkan_draw_reference.vcxproj')],
        new_system_header_count=len(system),artifacts=[audit.record_file(root,root/BUILD/'Release/vulkan_draw_reference.exe'),audit.record_file(root,exe),audit.record_file(root,installed),sdk_lib,sdl_lib],
        exclusions=['SDK and SDL binaries reused, not rebuilt','Linux and other APIs','Unbuilt LLGL/bgfx/Diligent/Forge Vulkan adapters','System loader/driver/VVL runtime is not redistributed','No new SDK binary package or benchmark binary redistribution'])
    return inventory,text

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);mode=p.add_mutually_exclusive_group(required=True);mode.add_argument('--create',action='store_true');mode.add_argument('--check',action='store_true');a=p.parse_args()
    inventory,notices=collect();audit.write_or_check(a.output,audit.output_files(inventory,notices),a.check)
    print(f"PASS: {len(inventory['files'])} conservative inputs, {len(inventory['components'])} components; two new compiler/linker traces; frozen SDK/SDL reuse verified")
if __name__=='__main__':main()
