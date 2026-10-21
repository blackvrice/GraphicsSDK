"""Reviewed no-NVAPI Windows D3D12 Forge profile; no competitor payload in SDK ZIP."""
import hashlib
import json
import re
from run_benchmarks import forge_dependencies, forge_fingerprint, forge_runtime


def validate_inputs(paths):
    """Only the Forge-authored fallback helper may be read from the NVAPI tree."""
    for value in paths:
        value=value.replace('\\','/').casefold()
        if '/nvapi/' in value and not value.endswith('/nvapi/nvapihelper.h'):
            raise ValueError('Legacy proprietary Forge NVAPI input is excluded: '+value)


def configure(root, policy, traces, artifacts, projects, directories, generated):
    from audit_licenses import read_trace, component_for
    lock=root/'benchmarks/competitors/forge-build.lock.json'
    review_path=root/'licenses/forge-policy.json'
    deps=forge_dependencies(root); review=json.loads(review_path.read_text(encoding='utf-8'))
    dep=deps['the_forge']; base=root/dep['directory']
    if review['reviewed_commits']['the_forge']!=dep['commit']:
        raise ValueError('Forge pin requires a new license review')
    policy['reviewed_commits'].update(review['reviewed_commits']); policy['components']+=review['components']
    directories['the_forge']=dep['directory']
    for name in ('benchmark','benchmark_core'):
        traces[name]=traces[name].replace('out/bench/','out/bench-forge/')
        artifacts[name]=artifacts[name].replace('out/bench/','out/bench-forge/')
    projects['benchmark']=projects['benchmark'].replace('out/bench/','out/bench-forge/')
    traces['forge']='out/bench-forge/bench_forge.dir/Release'
    artifacts['forge']='out/bench-forge/Release/bench_forge.lib'
    vendor='Common_3/Graphics/ThirdParty/OpenSource/'
    for name,path in {'forge_ags_import':vendor+'ags/ags_lib/lib/amd_ags_x64.lib',
                      'forge_dxc_import':vendor+'DirectXShaderCompiler/lib/x64/dxcompiler.lib',
                      'forge_ags_runtime':vendor+'ags/ags_lib/lib/amd_ags_x64.dll',
                      'forge_dxc_runtime':vendor+'DirectXShaderCompiler/bin/x64/dxcompiler.dll'}.items():
        artifacts[name]=(base/path).relative_to(root).as_posix()
    fingerprint=forge_fingerprint(root)
    for name in ('forge','benchmark'):
        if fingerprint.encode() not in (root/artifacts[name]).read_bytes():
            raise ValueError('Stale Forge library/executable fingerprint: '+name)
    inputs,_=read_trace(root,'forge',traces)
    validate_inputs(inputs)
    for value in inputs:
        relative=value.replace('\\','/').casefold().split('/.deps/theforge/')
        if len(relative)==2 and '/thirdparty/' in relative[1] and component_for(policy,'the_forge',relative[1])['id']=='forge':
            raise ValueError('Unreviewed Forge ThirdParty input: '+relative[1])
    commands=[]
    for name in ('forge','benchmark','benchmark_core'):
        logs=sorted((root/traces[name]).rglob('CL.command.1.tlog'))
        if not logs: raise ValueError('Missing compiler options: '+name)
        for log in logs:
            text=log.read_bytes().decode('utf-16')
            if not all(re.search(flag,text) for flag in (r'/O2(?:\s|$)',r'/MD(?:\s|$)')) or re.search(r'/(?:GL|MTd?)(?:\s|$)',text):
                raise ValueError('Forge profile must use /O2 /MD without LTO: '+name)
            if name=='forge' and ('GFX_FORGE_NO_NVAPI=1' not in text or 'EXTERNAL_CONFIG_FILEPATH' not in text or re.search(r'/D\s+"?(?:USE_PIX|FORGE_DEBUG|FORGE_PROFILE)(?:[="\s])',text)):
                raise ValueError('Unexpected Forge optional vendor/validation/profile configuration')
            commands.append({'path':log.relative_to(root).as_posix(),'sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
    config=root/'out/bench-forge/ForgeApplicationConfig.h'
    original=(base/'Common_3/Application/Config.h').read_text(encoding='utf-8')
    notice=original[:original.index('#pragma once')]
    body=original[original.index('#include <stdint.h>'):].replace('#endif\n\n#define VR_API_OPENXR','\n#define VR_API_OPENXR').replace('#if (_MSC_VER == 1929)','#if (_MSC_VER == 1929) || (_MSC_FULL_VER == 194435228)')
    expected=notice+'// Derived external application config; MSVC whitelist extended by GraphicsSDK.\n#pragma once\n'+body
    if config.read_text(encoding='utf-8')!=expected: raise ValueError('Generated Forge external configuration differs from reviewed recipe')
    generated.add(config.relative_to(root).as_posix().casefold())
    origin_path=root/'licenses/provenance/forge-terms-origin.json'
    origin=json.loads(origin_path.read_text(encoding='utf-8'))
    for record in origin['dxc']['exact_matches']:
        if hashlib.sha256((base/vendor/'DirectXShaderCompiler'/record['release_member']).read_bytes()).hexdigest()!=record['sha256']:
            raise ValueError('Forge DXC differs from exact official-release proof')
    for record in origin['dxc']['terms']:
        if hashlib.sha256((root/record['path']).read_bytes()).hexdigest()!=record['sha256']:
            raise ValueError('DXC terms provenance changed')
    def record(path): return {'path':path.relative_to(root).as_posix(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    # Binary/config inputs are added to the same canonical pin/terms inventory.
    assets=[base/vendor/'ags/ags_lib/lib/amd_ags_x64.lib',base/vendor/'ags/ags_lib/lib/amd_ags_x64.dll',
            base/vendor/'DirectXShaderCompiler/lib/x64/dxcompiler.lib',base/vendor/'DirectXShaderCompiler/bin/x64/dxcompiler.dll',
            base/'Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg',base/'Common_3/OS/Windows/pc_gpu.data']
    return {'dependencies':deps,'review_key':'forge_build_review','shader_sources':assets,
            'link_names':['benchmark','llgl','llgl_d3d12'],
            'evidence':{'lock':record(lock),'policy':record(review_path),'terms_origin':record(origin_path),
                        'compiler_command_evidence':commands,'generated_external_config':record(config),
                        'runtime_sha256':forge_runtime(root,root/artifacts['benchmark']),'build_fingerprint':fingerprint,
                        'note':'Legacy NVAPI headers/library excluded. PIX headers conservatively recorded with unresolved historical primary terms; no PIX binary linked or copied. Own SDK ZIP unchanged; inventory does not clear competitor redistribution.'}}
