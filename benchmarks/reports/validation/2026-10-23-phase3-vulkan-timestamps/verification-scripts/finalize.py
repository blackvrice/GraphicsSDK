import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import zipfile

root=pathlib.Path(__file__).resolve().parents[5]
sys.path.insert(0,str(root/'tools'))
import audit_licenses as audit
import audit_vulkan as av
v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-timestamps'
report=root/'benchmarks/reports/2026-10-23-phase3-vulkan-timestamps'
generated=root/'licenses/generated/windows-sdk-vulkan-timestamps-release'
package=root/'out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-timestamps.zip'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return dict(path=p.relative_to(root).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_bytes((json.dumps(x,indent=2)+'\n').encode())

av.configure_variant('timestamps');inventory,notices=av.collect(root)
required=av.payload(root,notices);inventory['package']=av.package_record(root,package,required)
audit.write_or_check(generated,audit.output_files(inventory,notices),True)
assert len(inventory['files'])==80
for r in inventory['compiler_read_evidence']+inventory['linker_read_evidence']:
    original=root/r['path'];target=v/'build-traces'/original.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,target)
for path in inventory['projects']:
    original=root/path;target=v/'build-traces'/original.relative_to(root/'out');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,target)
for folder in ('vulkan-timestamps','vulkan-timestamps-consumer','vulkan-timestamps-only','vulkan-timestamps-portable'):
    shutil.copyfile(root/'out'/folder/'CMakeCache.txt',v/(folder+'-cache.txt'))
for name,transform in (('missing-notices',lambda p:{k:x for k,x in p.items() if k!='share/GraphicsSDK/THIRD_PARTY_NOTICES.txt'}),
    ('changed-library',lambda p:{**p,'lib/GraphicsSDKCore.lib':b'changed'}),('extra-library',lambda p:{**p,'lib/unreviewed.lib':b'extra'})):
    negative=root/'out'/('vulkan-timestamps-negative-'+name+'.zip')
    with zipfile.ZipFile(negative,'w') as z:
        for k,data in transform(required).items():z.writestr(k,data)
    try:av.package_record(root,negative,required)
    except audit.AuditError:pass
    else:raise AssertionError(name)
(v/'package-controls.log').write_bytes(b'PASS: missing notices, changed library and extra library rejected\n')
previous=root/'benchmarks/reports/2026-10-23-phase3-vulkan-depth/evidence.json';prior=load(previous)
preserved={r['path']:r for r in prior['files']+prior['preserved_inputs']+prior['local_artifacts']+
    [prior[k] for k in ('summary','source_archive','previous_evidence')]+[rec(previous)]}
assert all(sha(root/r['path'])==r['sha256'] for r in preserved.values())
historical=[rec(p) for p in sorted((root/'out').glob('GraphicsSDK*.zip')) if p!=package];assert len(historical)==20
assert all(sha(root/r['path'])==r['sha256'] for r in prior['historical_packages']+[prior['SDK_package']])
assert sha(root/'tools/memory_stress_policy.json')=='377e0136d26f15f2262d624ca4cee0d1fb629fff4ebee7d34ba031f5919233ba'
assert all(sha(root/r['path'])==r['sha256'] for r in prior['source_files'] if r['path'].startswith('src/backends/d3d12/'))
assert load(v/'runtime-receipt.json')==load(root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-validation/runtime-receipt.json')
for name in ('.gitattributes','README.md','docs/ROADMAP.md','docs/BUILD.md','docs/TESTING.md','docs/VULKAN.md','docs/BACKENDS.md',
    'docs/PUBLIC_API.md','docs/DEPENDENCY_LICENSES.md','docs/PHASE3_VULKAN_TIMESTAMP_REPORT.md','docs/SHADER_PIPELINE.md','tools/audit_vulkan.py'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
report.mkdir(exist_ok=False)
write(report/'summary.json',dict(SDK_source_fingerprint=inventory['SDK_source_fingerprint'],production_SDK_changed=True,
    Windows_CTests=dict(integrated_registered=55,integrated_selected=52,integrated_deferred_memory_excluded=3,Vulkan_only=27,no_backend=10,selected_skipped=0),
    Python_tests=208,SPIRV_interface_assertions=181,SPIRV_vertex_assertions=61,SPIRV_binding_assertions=43,timestamp_math_assertions=3775,full_semantic_validation_in_SDK=False,externally_validated_shader_input_required=True,
    own_shader_modules_assembled_and_validated=0,shader_validation_rerun=False,existing_depth_shader_modules_reused=4,fixture_tool_receipt=rec(v/'reused-shader-receipt.json'),
    timestamp_suite=dict(assertions=311,measured_pairs=52,dropped_query_recordings=6,draws=6,images=18,depth_images=6,verified_texel_bytes=331852960,verified_buffer_bytes=75644964,padded_images=6,adapters=2),
    depth_suite=dict(assertions=883,passes=206,draws=258,images=278,depth_images=126,verified_texel_bytes=334855104,padded_images=262,adapters=2),
    binding_suite=dict(assertions=573,draws=156,snapshots=258,images=80,verified_texel_bytes=249889536,padded_images=72,adapters=2),
    vertex_suite=dict(assertions=771,draws=150,indexed_draws=78,images=146,verified_texel_bytes=250813720,padded_images=138,adapters=2),
    draw_suite=dict(assertions=637,draws=62,images=66,verified_texel_bytes=249491416,padded_images=58,adapters=2),
    final_validation_measured_pairs=156,final_validation_images=54,final_validation_depth_images=18,final_validation_texel_bytes=995558880,final_validation_buffer_bytes=226934892,
    final_total_measured_pairs=260,final_total_images=90,final_total_depth_images=30,final_total_texel_bytes=1659264800,final_total_buffer_bytes=378224820,
    color_pass_suite=dict(assertions=593,passes=128,images=138,verified_texel_bytes=747063512,padded_images=114,adapters=2),
    texture_suite=dict(assertions=835,roundtrips=74,verified_texel_bytes=499371832,padded_roundtrips=40,adapters=2),
    buffer_suite=dict(assertions=717,transfers=48,verified_bytes=57042546,adapters=2),
    native_probe=dict(assertions=274,adapters=2,native_core_errors=64,native_sync_hazards=2,synthetic_errors=2050,invalid_GPU_submissions=0),
    normal_SDK_validation_errors=0,required_Vulkan_tests_do_not_skip=True,installed_consumers_verified=True,local_runtime_reused=True,
    external_runtime_bundled=False,public_API_changed=False,public_ABI_changed=False,ShaderBinary_layout_changed=False,all_consumers_rebuilt=True,
    initial_SPIRV_drawing_implemented=True,vertex_input_implemented=True,indexed_drawing_implemented=True,DXC_compilation_implemented=False,bindings_implemented=True,depth_attachments_implemented=True,depth_transfers_implemented=True,depth_sampling_implemented=False,GPU_timestamps_implemented=True,calibrated_timestamps_implemented=False,performance_baseline=False,
    GPU_assisted_validation=False,Linux_native_verified=False,Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False,
    deferred_memory_investigation_unchanged=True,historical_packages_preserved=20,compiler_inputs=80,components=2,compiler_logs=34,linker_logs=33,
    inventory=rec(generated/'inventory.json'),SDK_package=inventory['package']))
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl','.spvasm'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|=set((root/'docs').glob('*.md'))|set((root/'licenses').glob('*.json'))|set((root/'benchmarks/competitors').glob('*.json'))
paths|=set((v/'verification-scripts').glob('*.py'))|set(generated.iterdir())|{root/'licenses/provenance/vulkan-headers-Apache-2.0.txt'}
sources=[rec(p) for p in sorted(paths)];digest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f"benchmarks/reports/sources/vulkan-timestamps-{inventory['SDK_source_fingerprint']}-{digest[:16]}.zip"
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=inventory['SDK_source_fingerprint'],source_manifest_sha256=digest,files=sources),indent=2)+'\n')
files=[rec(p) for p in sorted(v.rglob('*')) if p.is_file()]+[rec(p) for p in sorted(generated.iterdir())]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.pdb','.lib','.dmp','.etl') for r in files+sources)
tool=load(v/'reused-shader-receipt.json')
assert tool==load(root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-depth/shader-tools/receipt.json')
tool_artifacts=[tool['tool'],tool['prerequisite_library']]+[f['binary'] for f in tool['fixtures']]+tool['license_files']
assert all(sha(root/r['path'])==r['sha256'] for r in tool_artifacts+[tool['header']]+[f['assembly'] for f in tool['fixtures']])
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
    SDK_source_fingerprint=inventory['SDK_source_fingerprint'],summary=rec(report/'summary.json'),source_archive=rec(archive),source_files=sources,
    files=files,preserved_inputs=list(preserved.values()),historical_packages=historical,previous_evidence=rec(previous),SDK_package=rec(package),
    local_artifacts=inventory['artifacts']+load(v/'runtime-receipt.json')['artifacts']+tool_artifacts))
print(f'PASS: {len(files)} evidence/audit files, {len(sources)} source archive members; history/20 packages preserved')
